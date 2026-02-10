import { StructuredTool } from "@langchain/core/tools";
import path from "path";
import zodToJsonSchema from "zod-to-json-schema";
import { AudioManager } from "./audio";
import { createStreamFromWebsocket, base64EncodeAudio, mergeStreams, appendToBuffer, convertAudioToPCM16 } from "./utils";
import fs from 'fs';
import decodeAudio from 'audio-decode';
import WebSocket from "ws";
import { OpenAIWebSocketConnection } from "./connections";
import { VoiceToolExecutor } from "./executor";

// Constants
const EVENTS_TO_IGNORE = [
    "response.function_call_arguments.delta",
    "rate_limits.updated",
    "response.audio_transcript.delta",
    "response.created",
    "response.content_part.added",
    "response.content_part.done",
    "conversation.item.created",
    "response.audio.done",
    "session.created",
    "session.updated",
    "response.done",
    "response.output_item.done",
];

// Interfaces
export interface AudioConfig {
    sampleRate: number;
    channels: number;
    bitDepth: number;
}

export interface AudioHandler {
    buffer: Uint8Array;
    BUFFER_SIZE: number;
    ws: WebSocket;
}

export interface OpenAIVoiceReactAgentOptions {
    model: string;
    apiKey?: string;
    instructions?: string;
    tools?: StructuredTool[];
    url?: string;
    audioConfig?: AudioConfig;
}

export class OpenAIVoiceReactAgent {
    // Protected properties
    protected connection: OpenAIWebSocketConnection;
    protected instructions?: string;
    protected tools: StructuredTool[];
    protected BUFFER_SIZE = 4800;

    // Public properties
    public buffer = new Uint8Array();
    public audioBuffer: Buffer | undefined;

    // Private properties
    private audioManager: AudioManager;
    private recording: boolean = false;

    constructor(params: OpenAIVoiceReactAgentOptions) {
        this.audioManager = new AudioManager();
        this.connection = new OpenAIWebSocketConnection({
            url: params.url,
            apiKey: params.apiKey,
            model: params.model,
            audioConfig: params.audioConfig,
            audioManager: this.audioManager
        });
        this.instructions = params.instructions;
        this.tools = params.tools ?? [];
    }

    public startRecordingSession(): void {
        // Make sure we're not already recording
        if (this.recording) {
            this.audioManager.resetRecording();
        }
        this.recording = true;
        this.audioManager.startRecording();
        console.log('Started new recording session');
    }

    public async stopRecordingAndProcessAudio(): Promise<void> {
        if (!this.recording) {
            console.log('No active recording to stop');
            return;
        }

        // Set recording flag first to prevent new data
        this.recording = false;

        try {
            // Make sure to process any remaining audio
            this.audioManager.closeFile();

            // Get the buffer after closing to ensure all data is written
            const buffer = this.audioManager.getCurrentBuffer();
            if (buffer.length === 0) {
                console.log('No audio data captured');
                return;
            }

            const base64 = convertAudioToPCM16(buffer);
            await this.sendAudioEvent(base64);

        } catch (error) {
            console.error('Error processing audio:', error);
        } finally {
            // Always reset the audio manager
            this.audioManager.resetRecording();
            console.log('Recording session ended and cleaned up');
        }
    }

    private async sendAudioEvent(base64Audio: string): Promise<void> {
        if (!base64Audio) {
            console.log('No audio data to send');
            return;
        }

        const eventAudio = {
            type: 'conversation.item.create',
            item: {
                type: 'message',
                role: 'user',
                content: [{
                    type: 'input_audio',
                    audio: base64Audio
                }]
            }
        };

        try {
            this.connection.sendEvent(eventAudio);
            // Wait for the audio event to be processed
            await new Promise(resolve => setTimeout(resolve, 2000));
            this.connection.sendEvent({
                type: 'response.create'
            });
        } catch (error) {
            console.error('Error sending audio event:', error);
            throw error;
        }
    }

    // WebSocket Connection Methods
    async connect(
        websocketOrStream: AsyncGenerator<string> | WebSocket,
        sendOutputChunk: (chunk: string) => void | Promise<void>
    ): Promise<void> {
        let inputStream = await this.setupWebSocketConnection(websocketOrStream);
        const toolsByName = this.createToolsMap();
        const toolExecutor = new VoiceToolExecutor(toolsByName);

        await this.initializeConnection(toolsByName);
        await this.handleStreamEvents(inputStream, toolExecutor, sendOutputChunk);
    }

    private async setupWebSocketConnection(websocketOrStream: AsyncGenerator<string> | WebSocket) {
        if ("next" in websocketOrStream) {
            return websocketOrStream;
        }

        await this.waitForWebSocketOpen(websocketOrStream);
        websocketOrStream.binaryType = 'arraybuffer';

        this.setupBinaryMessageHandler(websocketOrStream);
        return createStreamFromWebsocket(websocketOrStream);
    }

    private async waitForWebSocketOpen(ws: WebSocket): Promise<void> {
        if (ws.readyState === WebSocket.OPEN) return;

        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error("WebSocket connection timed out after 10 seconds"));
            }, 10000);

            ws.once("connect", () => {
                clearTimeout(timeout);
                resolve();
            });

            ws.once("error", (error) => {
                clearTimeout(timeout);
                reject(error);
            });
        });
    }

    private setupBinaryMessageHandler(ws: WebSocket): void {
        ws.on('message', async (data) => {
            const buffer = Buffer.isBuffer(data)
                ? data
                : data instanceof ArrayBuffer
                    ? Buffer.from(data)
                    : Array.isArray(data)
                        ? Buffer.concat(data)
                        : null;
            if (buffer) {
                console.log('===Received binary message:', {
                    type: 'Buffer',
                    size: buffer.length
                });
                await this.connection.handleIncomingAudio(buffer);
            }
        });
    }

    private createToolsMap(): Record<string, StructuredTool> {
        return this.tools.reduce((acc: Record<string, StructuredTool>, tool) => {
            acc[tool.name] = tool;
            return acc;
        }, {});
    }

    private async initializeConnection(toolsByName: Record<string, StructuredTool>): Promise<void> {
        await this.connection.connect();

        const toolDefs = Object.values(toolsByName).map((tool) => ({
            type: "function",
            name: tool.name,
            description: tool.description,
            parameters: zodToJsonSchema(tool.schema),
        }));

        this.connection.sendEvent({
            type: "session.update",
            session: {
                instructions: this.instructions,
                input_audio_transcription: { model: "whisper-1" },
                tools: toolDefs,
                input_audio_format: "pcm16",
                output_audio_format: "pcm16",
            },
        });
    }

    private async handleStreamEvents(
        inputStream: AsyncGenerator<string> | AsyncGenerator<any>,
        toolExecutor: VoiceToolExecutor,
        sendOutputChunk: (chunk: string) => void | Promise<void>
    ): Promise<void> {
        const modelReceiveStream = this.connection.eventStream();

        for await (const [streamKey, dataRaw] of mergeStreams({
            input_mic: inputStream,
            output_speaker: modelReceiveStream,
            tool_outputs: toolExecutor.outputIterator(),
        })) {
            await this.processStreamEvent(streamKey, dataRaw, toolExecutor, sendOutputChunk);
        }
    }

    private async processStreamEvent(
        streamKey: string,
        dataRaw: any,
        toolExecutor: VoiceToolExecutor,
        sendOutputChunk: (chunk: string) => void | Promise<void>
    ): Promise<void> {
        const data = typeof dataRaw === "string" ? dataRaw : dataRaw;

        if (data === "START_RECORD") {
            this.startRecordingSession();
        } else if (data === "STOP_RECORD") {
            await this.stopRecordingAndProcessAudio();
        } else {
            await this.handleStreamOutput(streamKey, data, toolExecutor, sendOutputChunk);
        }
    }

    private async handleStreamOutput(
        streamKey: string,
        data: any,
        toolExecutor: VoiceToolExecutor,
        sendOutputChunk: (chunk: string) => void | Promise<void>
    ): Promise<void> {
        switch (streamKey) {
            case "tool_outputs":
                this.connection.sendEvent(data);
                this.connection.sendEvent({ type: "response.create", response: {} });
                break;

            case "output_speaker":
                await this.handleSpeakerOutput(data, toolExecutor, sendOutputChunk);
                break;
        }
    }

    private async handleSpeakerOutput(
        data: any,
        toolExecutor: VoiceToolExecutor,
        sendOutputChunk: (chunk: string) => void | Promise<void>
    ): Promise<void> {
        const { type } = data;

        if (type === "response.audio.delta" || type === "response.audio_buffer.speech_started") {
            await sendOutputChunk(JSON.stringify(data));
        } else if (type === "error") {
            console.error("error:", data);
        } else if (type === "response.function_call_arguments.done") {
            toolExecutor.addToolCall(data);
        } else if (type === "response.audio_transcript.done") {
            console.log("model:", data.transcript);
        } else if (type === "conversation.item.input_audio_transcription.completed") {
            console.log("user:", data.transcript);
        } else if (!EVENTS_TO_IGNORE.includes(type)) {
            console.log(type);
        }
    }
}