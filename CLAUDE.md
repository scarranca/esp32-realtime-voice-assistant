# MiniBot V5 - AI Voice Assistant (OpenAI Realtime API)

## Project Overview
ESP32-S3 push-to-talk AI voice assistant using OpenAI's Realtime API. Press button, speak, release, hear AI response. Single model handles STT + LLM + TTS with continuous audio streaming — no sentence splitting, no gaps.

**Pipeline:** Button press → Record mic → Stream PCM to server via WebSocket → OpenAI Realtime API (STT+LLM+TTS) → Stream PCM audio back → Play on speaker

## Hardware

| Component | Model | Interface | Pins |
|-----------|-------|-----------|------|
| MCU | ESP32-S3-DevKitC-1 (8MB Flash, no PSRAM) | - | - |
| Microphone | INMP441 | I2S0 (RX) | BCLK=4, WS=5, DIN=6 |
| Amplifier | MAX98357A | I2S1 (TX) | BCLK=16, LRC=17, DOUT=15 |
| Button | External pushbutton (active LOW, INPUT_PULLUP) | GPIO | Pin 2 |
| Display | SSD1306 OLED 0.96" (128x64) — available but unused | I2C | SDA=8, SCL=9 |

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
- Library: ArduinoWebsockets 0.5.4 (only dependency)

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
| `main.cpp` | Setup, button handling, I2S start/stop switching |
| `config.h` | All pin definitions, sample rates, buffer sizes |
| `mic.cpp/h` | INMP441 mic via I2S0, 32-bit read → 16-bit conversion, WS send |
| `lib_speaker.cpp/h` | MAX98357A amp via I2S1, direct `i2s_write(portMAX_DELAY)` |
| `lib_websocket.cpp/h` | WSS connection to server, binary audio in/out, reconnect |
| `lib_wifi.cpp/h` | WiFi credentials and connection |
| `lib_button.h` | Button debounce (active LOW with INPUT_PULLUP) |
| `utils.cpp/h` | Memory allocation helper |

### Key Design Decisions
- **Direct i2s_write from WebSocket callback** — no ring buffer, no pre-buffer, no dedicated audio task. Blocking `i2s_write(portMAX_DELAY)` provides natural backpressure and timing.
- **Mic task on Core 0, loop on Core 1** — true parallelism, mic reads don't block WebSocket polling.
- **I2S stop/start between modes** — clean state switching prevents mic/speaker interference.
- **24kHz sample rate** — matches OpenAI Realtime API native format, no resampling needed.

### RAM Budget
- RAM: 13.9% (45KB of 320KB)
- Flash: 26.0%

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
ESP32 ←WSS→ This Server ←WSS→ OpenAI Realtime API (gpt-4o-realtime-preview)
```

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
| `index.js` | Express + WS server, OpenAI Realtime API bridge |
| `package.json` | Dependencies: express, ws, dotenv |
| `.env.example` | Environment variable template |

### Environment Variables
- `OPENAI_API_KEY` — required
- `REALTIME_MODEL` — default: `gpt-4o-realtime-preview`
- `REALTIME_VOICE` — default: `alloy`
- `SYSTEM_PROMPT` — default: helpful voice assistant, responds in user's language
- `PORT` — set by Railway

## Common Issues & Fixes

| Issue | Cause | Fix |
|-------|-------|-----|
| IDE red errors for ESP-IDF headers | Clang doesn't understand ESP-IDF | Ignore, use `pio run` for real compilation |
| "Server unreachable" after clean build | TLS patch lost | Re-apply ArduinoWebsockets patches (see above) |
| Mic produces garbage audio | 16-bit I2S read mode | Use 32-bit read + `>> 16` (INMP441 is 24-bit in 32-bit frames) |
| Audio distortion / low volume | `use_apll=true` | Keep `use_apll=false` on ESP32-S3 |
| Button triggers on boot | GPIO 0 USB interference | Use GPIO 2 |
| Wrong language response | Missing language instruction | System prompt says "respond in user's language" |
| Serial port busy during flash | VS Code serial monitor | Close serial monitor before flashing |

## Deploy Commands

**Server (Railway):**
```bash
cd server
npm install
git add -A && git commit -m "description" && git push
```

**Firmware (ESP32):**
```bash
cd esp32
pio run -t upload
```
