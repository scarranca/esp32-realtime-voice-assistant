import "dotenv/config";
import { WebSocket } from "ws";
import { serve } from "@hono/node-server";
import { Hono } from "hono";
import { createNodeWebSocket } from "@hono/node-ws";
import { serveStatic } from "@hono/node-server/serve-static";

import { INSTRUCTIONS } from "./prompt";
import { TOOLS } from "./tools";
import { createAsyncIterableFromWebSocket } from "./lib/utils";
import { OpenAIVoiceReactAgent } from "./lib/agent";

const app = new Hono();
const WS_PORT = 8888;
const connectedClients = new Set<WebSocket>();

const { injectWebSocket, upgradeWebSocket } = createNodeWebSocket({ app });

app.use("/", serveStatic({ path: "./static/index.html" }));
app.use("/static/*", serveStatic({ root: "./" }));

app.get(
  "/device",
  upgradeWebSocket((c) => ({
    onOpen: async (c, ws) => {
      if (!process.env.OPENAI_API_KEY) {
        return ws.close();
      }

      const rawWs = ws.raw as WebSocket;
      connectedClients.add(rawWs);

      const broadcastToClients = (data: string) => {
        connectedClients.forEach(client => {
          if (client.readyState === WebSocket.OPEN) {
            // console.log('Broadcasting to client:', data);
            // Convert base64 to buffer if data is a base64 string
            try {
              const parsed = JSON.parse(data);
              if (parsed.type === "response.audio.delta" && parsed.delta) {
                const audioBuffer = Buffer.from(parsed.delta, 'base64');
                // Send audio data in chunks that ESP32 can handle
                const CHUNK_SIZE = 1024; // ESP32 friendly chunk size
                for (let i = 0; i < audioBuffer.length; i += CHUNK_SIZE) {
                    const chunk = audioBuffer.slice(i, i + CHUNK_SIZE);
                    client.send(chunk);
                }
                return;
              }
            } catch (e) {
              // If parsing fails, send original data
            }
            // client.send(data);
            // client.send(data);
          }
        });
      };

      const agent = new OpenAIVoiceReactAgent({
        instructions: INSTRUCTIONS,
        tools: TOOLS,
        model: "gpt-4o-realtime-preview",
        audioConfig: {
          sampleRate: 24000,  // Match ESP32 sample rate
          channels: 1,
          bitDepth: 16
        }
      });

      // Wait 100ms before connecting to allow WebSocket setup
      await new Promise(resolve => setTimeout(resolve, 1000));
      await agent.connect(rawWs, broadcastToClients);
    },
    onClose: (c, ws) => {
      const rawWs = ws.raw as WebSocket;
      connectedClients.delete(rawWs);
      console.log("Client disconnected");
    },
  }))
);

const server = serve({
  fetch: app.fetch,
  port: WS_PORT,
});

injectWebSocket(server);

console.log(`Server is running on port ${WS_PORT}`);