# MiniBot V5 - AI Voice Assistant (OpenAI Realtime API)

## Project Overview
ESP32-S3 push-to-talk AI voice assistant using OpenAI's Realtime API. Press button, speak, release, hear AI response. Single model handles STT + LLM + TTS with continuous audio streaming — no sentence splitting, no gaps. Supports web search via Tavily function calling.

**Pipeline:** Button press → Record mic → Stream PCM to server via WebSocket → OpenAI Realtime API (STT+LLM+TTS) → Stream PCM audio back → Play on speaker

## Hardware

| Component | Model | Interface | Pins |
|-----------|-------|-----------|------|
| MCU | ESP32-S3-DevKitC-1 (8MB Flash, no PSRAM) | - | - |
| Microphone | INMP441 | I2S0 (RX) | BCLK=4, WS=5, DIN=6 |
| Amplifier | MAX98357A | I2S1 (TX) | BCLK=16, LRC=17, DOUT=15 |
| Button | External pushbutton (active LOW, INPUT_PULLUP) | GPIO | Pin 2 |
| Display | SSD1306 OLED 0.96" (128x64) | I2C | SDA=8, SCL=9 |

**Important hardware notes:**
- INMP441 outputs 24-bit data in 32-bit I2S frames. Must use `I2S_BITS_PER_SAMPLE_32BIT` and `>> 16` to get 16-bit PCM. Using 16-bit directly produces garbage.
- MAX98357A LR pin is wired to GND (left channel).
- GPIO 0 is unreliable due to USB DTR/RTS interference. Use GPIO 2 for button.
- Do NOT use `use_apll = true` on ESP32-S3 — causes distorted audio.

## Build System
- **PlatformIO** with Arduino framework
- Board: `esp32-s3-devkitc-1`
- Build flags: `-DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1`
- Monitor: 115200 baud, `monitor_dtr=0`, `monitor_rts=0`
- Libraries: ArduinoWebsockets 0.5.4, Adafruit SSD1306 2.5.x, Adafruit GFX 1.12.x

**Build command:** `pio run` (path: `~/.platformio/penv/bin/pio`)
**Flash command:** `pio run -t upload`
**Monitor:** `pio device monitor`

## Firmware Architecture (`esp32/src/`)

### Audio Flow
```
Button Press:                          Button Release:
  stop speaker I2S                       stop mic I2S
  start mic I2S                          start speaker I2S
  setRecording(true)                     sendEndAudio() → server

Recording (micTask on Core 0):        Playback (WS callback on Core 1):
  i2s_read 32-bit                        receive binary WS frame
  >> 16 to 16-bit PCM                    i2s_write(portMAX_DELAY)
  sendBinary via WebSocket               blocks until DMA has room
```

### Files
| File | Purpose |
|------|---------|
| `main.cpp` | Setup, button handling, I2S start/stop switching, OLED display |
| `config.h` | All pin definitions, sample rates, buffer sizes, firmware version |
| `mic.cpp/h` | INMP441 mic via I2S0, 32-bit read → 16-bit conversion, WS send |
| `lib_speaker.cpp/h` | MAX98357A amp via I2S1, direct `i2s_write(portMAX_DELAY)` |
| `lib_websocket.cpp/h` | WSS connection to server, binary audio in/out, reconnect |
| `lib_wifi.cpp/h` | WiFi credentials, connection, power saving disabled |
| `lib_button.h` | Button debounce (active LOW with INPUT_PULLUP) |
| `utils.cpp/h` | Memory allocation helper |

### Key Design Decisions
- **Direct i2s_write from WebSocket callback** — no ring buffer, no pre-buffer, no dedicated audio task. Blocking `i2s_write(portMAX_DELAY)` provides natural backpressure and timing.
- **Mic task on Core 0, loop on Core 1** — true parallelism, mic reads don't block WebSocket polling.
- **I2S stop/start between modes** — clean state switching prevents mic/speaker interference.
- **24kHz sample rate** — matches OpenAI Realtime API native format, no resampling needed.
- **WiFi power saving disabled** (`WIFI_PS_NONE`) — prevents random latency spikes during audio streaming.
- **OLED display** — shows firmware version and status during boot (MiniBot V5.0).
- **Speaker DMA buffers** — 16x512 for smoother playback with lower latency.

### RAM Budget
- RAM: 14.1% (46KB of 320KB)
- Flash: 26.9%

## ArduinoWebsockets TLS Patch (CRITICAL)
The library's `setInsecure()` is broken on ESP32. After `pio run -t clean` or fresh library install, must patch two files in `.pio/libdeps/esp32-s3-devkitc-1/ArduinoWebsockets/src/`:

1. **`tiny_websockets/network/esp32/esp32_tcp.hpp`** — Add to `SecuredEsp32TcpClient`:
   ```cpp
   void setInsecure() { this->client.setInsecure(); }
   ```

2. **`websockets_client.cpp`** — In `upgradeToSecuredConnection()` ESP32 block, after the `setPrivateKey` check, add:
   ```cpp
   if(!this->_optional_ssl_ca_cert && !this->_optional_ssl_client_ca && !this->_optional_ssl_private_key) {
       client->setInsecure();
   }
   ```

## Backend Server (`server/`)

Deployed on **Railway**: `https://esp32-voice-bot-production.up.railway.app`

### Architecture
```
ESP32 ←WSS→ This Server ←WSS→ OpenAI Realtime API
                         ├──→ Tavily Search API (web_search tool)
                         └──→ (more tools can be added)
```

### Tools (Function Calling)
The server registers tools with the OpenAI Realtime API session. When the model decides to use a tool, it sends a function call event. The server executes the tool and feeds the result back, then OpenAI generates an audio response incorporating the result.

| Tool | API | Description |
|------|-----|-------------|
| `web_search` | Tavily | Real-time web search for current events, weather, news, prices |

To add a new tool: define it in the `TOOLS` array, add execution logic in `executeTool()`.

### Protocol
| Direction | Type | Content |
|-----------|------|---------|
| ESP32 → Server | Binary | Raw PCM16 mic audio (24kHz mono) |
| ESP32 → Server | Text | `{"type":"end_audio"}` — commit audio, request response |
| Server → ESP32 | Text | `{"type":"metadata","transcript":"...","response_text":"..."}` |
| Server → ESP32 | Binary | PCM16 audio chunks (1024 bytes each, 24kHz mono) |
| Server → ESP32 | Text | `{"type":"end_response"}` — playback complete |
| Server → ESP32 | Text | `{"type":"error","message":"..."}` |

### Files
| File | Purpose |
|------|---------|
| `index.js` | Express + WS server, OpenAI Realtime API bridge, tool execution |
| `package.json` | Dependencies: express, ws, dotenv |
| `.env.example` | Environment variable template |

### Environment Variables
- `OPENAI_API_KEY` — required
- `TAVILY_API_KEY` — optional, enables web search tool
- `REALTIME_MODEL` — default: `gpt-4o-realtime-preview` (also: `gpt-realtime-mini`)
- `REALTIME_VOICE` — default: `alloy`
- `SYSTEM_PROMPT` — default: helpful voice assistant, responds in user's language
- `PORT` — set by Railway

## Server LangChain (`server_langchain/`)

Alternative server using LangChain for advanced agent features. Not currently deployed. Uses Hono + WebSocket, LangChain tools (Tavily, math), audio recording to WAV files.

**TODO:** Align with ESP32 protocol (`/ws/voice` path, binary PCM forwarding, `end_audio` messages) to make it a drop-in replacement for the simple server.

**Requirements:** Node.js, Yarn 3.x with `nodeLinker: node-modules` (required for Node.js v24 compatibility).

## Deploy Commands

**Server (Railway):**
```bash
cd server
railway up
```

**Change model/voice/prompt:**
```bash
railway variables --set "REALTIME_MODEL=gpt-realtime-mini"
railway variables --set "REALTIME_VOICE=alloy"
railway variables --set "SYSTEM_PROMPT=Your custom prompt here"
```

**Firmware (ESP32):**
```bash
cd esp32
~/.platformio/penv/bin/pio run -t upload
```

**Git (push to fork):**
```bash
git add -A && git commit -m "description" && git push fork main
```

## Common Issues & Fixes

| Issue | Cause | Fix |
|-------|-------|-----|
| IDE red errors for ESP-IDF headers | Clang doesn't understand ESP-IDF | Ignore, use `pio run` for real compilation |
| "Server unreachable" after clean build | TLS patch lost | Re-apply ArduinoWebsockets patches (see above) |
| Mic produces garbage audio | 16-bit I2S read mode | Use 32-bit read + `>> 16` (INMP441 is 24-bit in 32-bit frames) |
| Audio distortion / low volume | `use_apll=true` | Keep `use_apll=false` on ESP32-S3 |
| Button triggers on boot | GPIO 0 USB interference | Use GPIO 2 |
| Wrong language response | SYSTEM_PROMPT missing language instruction | Add "Always respond in the same language the user speaks" |
| Serial port busy during flash | VS Code serial monitor or python process | Close monitor / `kill <PID>` before flashing |
| Tavily 401 error | Old API auth format | Use Bearer token in Authorization header, not api_key in body |
| server_langchain won't start on Node 24 | Yarn PnP incompatible | Use `.yarnrc.yml` with `nodeLinker: node-modules` |
