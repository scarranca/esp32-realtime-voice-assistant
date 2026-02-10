require('dotenv').config();

const http = require('http');
const express = require('express');
const { WebSocketServer, WebSocket } = require('ws');

const app = express();
const PORT = process.env.PORT || 3000;
const server = http.createServer(app);

// ============================================================================
// Config
// ============================================================================

const OPENAI_API_KEY = process.env.OPENAI_API_KEY;
const TAVILY_API_KEY = process.env.TAVILY_API_KEY;
const REALTIME_MODEL = process.env.REALTIME_MODEL || 'gpt-4o-realtime-preview';
const REALTIME_VOICE = process.env.REALTIME_VOICE || 'alloy';
const SYSTEM_PROMPT = process.env.SYSTEM_PROMPT ||
  'You are a helpful voice assistant. Keep responses concise and conversational, under 3 sentences unless asked for more detail. Always respond in the same language the user speaks.';

// ============================================================================
// Tools — web search via Tavily
// ============================================================================

const TOOLS = [];

if (TAVILY_API_KEY) {
  TOOLS.push({
    type: 'function',
    name: 'web_search',
    description: 'Search the web for current information. Use this when the user asks about recent events, news, weather, prices, facts you are unsure about, or anything that requires up-to-date information.',
    parameters: {
      type: 'object',
      properties: {
        query: {
          type: 'string',
          description: 'The search query',
        },
      },
      required: ['query'],
    },
  });
  console.log('[Tools] Web search enabled (Tavily)');
} else {
  console.log('[Tools] Web search disabled (no TAVILY_API_KEY)');
}

async function executeWebSearch(query) {
  console.log(`[Tavily] Searching: "${query}"`);
  const res = await fetch('https://api.tavily.com/search', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'Authorization': `Bearer ${TAVILY_API_KEY}`,
    },
    body: JSON.stringify({
      query,
      max_results: 3,
      include_answer: true,
    }),
  });

  if (!res.ok) {
    const err = await res.text();
    console.error(`[Tavily] Error: ${res.status} ${err}`);
    return `Search failed: ${res.status}`;
  }

  const data = await res.json();
  let result = '';

  if (data.answer) {
    result += `Answer: ${data.answer}\n\n`;
  }

  if (data.results) {
    for (const r of data.results.slice(0, 3)) {
      result += `- ${r.title}: ${r.content?.slice(0, 200)}\n`;
    }
  }

  console.log(`[Tavily] Got ${data.results?.length || 0} results`);
  return result || 'No results found.';
}

async function executeTool(name, args) {
  switch (name) {
    case 'web_search':
      return executeWebSearch(args.query);
    default:
      return `Unknown tool: ${name}`;
  }
}

// ============================================================================
// Health check
// ============================================================================

app.get('/health', (_req, res) => {
  res.json({ status: 'ok', timestamp: new Date().toISOString() });
});

// ============================================================================
// WebSocket voice endpoint — OpenAI Realtime API bridge
// ============================================================================
//
// Architecture:
//   ESP32 ←WebSocket→ This Server ←WebSocket→ OpenAI Realtime API
//
// OpenAI handles STT + LLM + TTS in a single model, producing
// continuous PCM16 audio at 24kHz — no sentence splitting, no gaps.
//
// ESP32 Protocol:
//   Client → Server:
//     Binary frames: raw PCM audio (24kHz/16-bit/mono)
//     Text frame:    {"type": "end_audio"}
//
//   Server → Client:
//     Text frame:    {"type": "metadata", "transcript": "...", "response_text": "..."}
//     Binary frames: raw PCM audio (24kHz/16-bit/mono, 1024-byte chunks)
//     Text frame:    {"type": "end_response"}
//     Text frame:    {"type": "error", "message": "..."}
//

const wss = new WebSocketServer({ server, path: '/ws/voice' });

wss.on('connection', (espWs) => {
  console.log('[ESP32] Client connected');

  let openaiWs = null;
  let sessionReady = false;

  // Per-turn state (reset each button press)
  let inputTranscript = '';
  let responseTranscript = '';
  let metadataSent = false;
  let totalAudioBytes = 0;
  let turnStart = 0;

  // ── Connect to OpenAI Realtime API ──────────────────────────────────

  function connectToOpenAI() {
    if (!OPENAI_API_KEY) {
      console.error('[OpenAI] OPENAI_API_KEY not set');
      sendToEsp({ type: 'error', message: 'OPENAI_API_KEY not set' });
      return;
    }

    const url = `wss://api.openai.com/v1/realtime?model=${REALTIME_MODEL}`;
    console.log(`[OpenAI] Connecting to ${REALTIME_MODEL}...`);

    openaiWs = new WebSocket(url, {
      headers: {
        'Authorization': `Bearer ${OPENAI_API_KEY}`,
        'OpenAI-Beta': 'realtime=v1',
      },
    });

    openaiWs.on('open', () => {
      console.log('[OpenAI] Connected, configuring session...');
      openaiWs.send(JSON.stringify({
        type: 'session.update',
        session: {
          modalities: ['text', 'audio'],
          instructions: SYSTEM_PROMPT,
          voice: REALTIME_VOICE,
          input_audio_format: 'pcm16',
          output_audio_format: 'pcm16',
          input_audio_transcription: { model: 'whisper-1' },
          turn_detection: null,  // Manual turn control (push-to-talk)
          tools: TOOLS,
        },
      }));
    });

    openaiWs.on('message', (data) => {
      try {
        const event = JSON.parse(data.toString());
        handleOpenAIEvent(event);
      } catch (err) {
        console.error('[OpenAI] Parse error:', err.message);
      }
    });

    openaiWs.on('close', (code) => {
      console.log(`[OpenAI] Disconnected (code ${code})`);
      openaiWs = null;
      sessionReady = false;
    });

    openaiWs.on('error', (err) => {
      console.error('[OpenAI] WS error:', err.message);
    });
  }

  // ── Handle OpenAI Realtime events ───────────────────────────────────

  function handleOpenAIEvent(event) {
    switch (event.type) {
      case 'session.created':
      case 'session.updated':
        sessionReady = true;
        console.log(`[OpenAI] ${event.type} — voice: ${REALTIME_VOICE}`);
        break;

      // User's speech transcript
      case 'conversation.item.input_audio_transcription.completed':
        inputTranscript = event.transcript || '';
        console.log(`[OpenAI] User: "${inputTranscript}"`);
        if (metadataSent) {
          sendToEsp({ type: 'metadata', transcript: inputTranscript, response_text: responseTranscript || '...' });
        }
        break;

      // Audio response chunks — forward to ESP32
      case 'response.audio.delta':
        if (event.delta) {
          const audioBuffer = Buffer.from(event.delta, 'base64');
          totalAudioBytes += audioBuffer.length;

          if (!metadataSent) {
            const latencyMs = Date.now() - turnStart;
            console.log(`[OpenAI] First audio in ${latencyMs}ms`);
            sendToEsp({
              type: 'metadata',
              transcript: inputTranscript || '...',
              response_text: '...',
            });
            metadataSent = true;
          }

          // Forward audio as binary frames (1024 bytes each)
          const CHUNK_SIZE = 1024;
          for (let i = 0; i < audioBuffer.length; i += CHUNK_SIZE) {
            if (espWs.readyState !== WebSocket.OPEN) break;
            const end = Math.min(i + CHUNK_SIZE, audioBuffer.length);
            espWs.send(audioBuffer.subarray(i, end));
          }
        }
        break;

      // Response text transcript
      case 'response.audio_transcript.delta':
        responseTranscript += (event.delta || '');
        break;

      case 'response.audio.done':
        console.log(`[OpenAI] Audio done: ${(totalAudioBytes / (24000 * 2)).toFixed(1)}s`);
        break;

      // Function call — execute tool and send result back
      case 'response.function_call_arguments.done':
        handleToolCall(event);
        break;

      // Response complete — signal ESP32
      case 'response.done': {
        // Only send end_response if we actually sent audio (not a tool-only response)
        if (totalAudioBytes > 0) {
          const totalMs = Date.now() - turnStart;
          console.log(`[OpenAI] ─── Turn complete ───`);
          console.log(`[OpenAI]   Audio: ${(totalAudioBytes / (24000 * 2)).toFixed(1)}s (${totalAudioBytes} bytes)`);
          console.log(`[OpenAI]   Total: ${totalMs}ms`);
          console.log(`[OpenAI]   Response: "${responseTranscript}"`);
          sendToEsp({ type: 'end_response' });
        }
        break;
      }

      case 'error':
        console.error(`[OpenAI] API error:`, event.error);
        sendToEsp({ type: 'error', message: event.error?.message || 'OpenAI Realtime error' });
        break;
    }
  }

  // ── Tool execution ──────────────────────────────────────────────────

  async function handleToolCall(event) {
    const { call_id, name, arguments: argsStr } = event;
    console.log(`[Tool] Calling ${name}(${argsStr})`);

    let args;
    try {
      args = JSON.parse(argsStr);
    } catch (e) {
      args = {};
    }

    const result = await executeTool(name, args);
    console.log(`[Tool] ${name} result: ${result.slice(0, 100)}...`);

    // Send tool result back to OpenAI
    if (isOpenAIReady()) {
      openaiWs.send(JSON.stringify({
        type: 'conversation.item.create',
        item: {
          type: 'function_call_output',
          call_id,
          output: result,
        },
      }));

      // Request a new response that incorporates the tool result
      openaiWs.send(JSON.stringify({ type: 'response.create' }));
    }
  }

  // ── Helpers ─────────────────────────────────────────────────────────

  function sendToEsp(obj) {
    if (espWs.readyState === WebSocket.OPEN) {
      espWs.send(JSON.stringify(obj));
    }
  }

  function isOpenAIReady() {
    return openaiWs && openaiWs.readyState === WebSocket.OPEN && sessionReady;
  }

  // ── Start OpenAI connection ─────────────────────────────────────────

  connectToOpenAI();

  // ── Handle ESP32 messages ───────────────────────────────────────────

  espWs.on('message', (data, isBinary) => {
    if (isBinary) {
      // Forward mic audio to OpenAI (base64 encoded PCM16 at 24kHz)
      if (isOpenAIReady()) {
        openaiWs.send(JSON.stringify({
          type: 'input_audio_buffer.append',
          audio: Buffer.from(data).toString('base64'),
        }));
      }
      return;
    }

    // Text message from ESP32
    try {
      const msg = JSON.parse(data.toString());

      if (msg.type === 'end_audio') {
        console.log('[ESP32] End audio — requesting response');

        // Reset per-turn state
        inputTranscript = '';
        responseTranscript = '';
        metadataSent = false;
        totalAudioBytes = 0;
        turnStart = Date.now();

        if (isOpenAIReady()) {
          openaiWs.send(JSON.stringify({ type: 'input_audio_buffer.commit' }));
          openaiWs.send(JSON.stringify({ type: 'response.create' }));
        } else {
          sendToEsp({ type: 'error', message: 'OpenAI not connected' });
          if (!openaiWs) connectToOpenAI();
        }
      }
    } catch (err) {
      console.error('[ESP32] Parse error:', err.message);
    }
  });

  // ── Cleanup ─────────────────────────────────────────────────────────

  espWs.on('close', () => {
    console.log('[ESP32] Client disconnected');
    if (openaiWs) {
      openaiWs.close();
      openaiWs = null;
    }
  });

  espWs.on('error', (err) => {
    console.error('[ESP32] Error:', err.message);
  });
});

// ============================================================================
// Start server
// ============================================================================

server.listen(PORT, () => {
  console.log(`[Server] MiniBot V5 (OpenAI Realtime) on port ${PORT}`);
  console.log(`[Server] Model: ${REALTIME_MODEL}, Voice: ${REALTIME_VOICE}`);
  console.log(`[Server] Tools: ${TOOLS.map(t => t.name).join(', ') || 'none'}`);
  console.log(`[Server] WS   /ws/voice  — WebSocket voice endpoint`);
  console.log(`[Server] GET  /health    — Health check`);
});
