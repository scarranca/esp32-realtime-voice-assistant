# XIAO ESP32-S3 Wake Word Voice Assistant — Setup Guide

Step-by-step instructions for completing the ESP-SR WakeNet9 "Hi ESP" wake word integration on the XIAO ESP32-S3. This guide is designed to be followed by a Claude session or a developer.

## Current State

The firmware code is written and compiles structurally, but needs:
1. Platform validation (pioarduino + ESP-SR build)
2. WakeNet model file (`srmodels.bin`) obtained and flashed
3. ArduinoWebsockets TLS patch applied
4. Full build + flash test

## Step-by-Step Tasks

### Step 1: Validate pioarduino platform build

The `platformio.ini` uses the pioarduino platform (`https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip`) which bundles ESP-SR support for Arduino framework.

**Action:** Run `cd esp32_xiao_s3 && ~/.platformio/penv/bin/pio run` and fix any build errors.

**Known potential issues:**
- ESP-SR headers (`esp_wn_iface.h`, `esp_wn_models.h`) may not be found if the pioarduino stable release doesn't include them. If so, try the develop branch: `https://github.com/pioarduino/platform-espressif32.git#develop`
- If ESP-SR is not bundled with pioarduino, we may need to add it as an ESP-IDF component. Create `esp32_xiao_s3/src/idf_component.yml`:
  ```yaml
  dependencies:
    espressif/esp-sr:
      version: "^2.1.5"
  ```
  And switch to `framework = arduino, espidf` in platformio.ini
- Build flags like `-DCONFIG_SR_WN_MODEL_WN9_HIESP=y` may need adjusting based on which ESP-SR version is available
- The pioarduino platform may use different ESP-SR APIs — check if `esp_srmodel_init()` is the correct entry point or if it changed

### Step 2: Obtain the WakeNet9 "Hi ESP" model binary

The `srmodels.bin` file must be flashed to the `model` partition (offset `0x310000`, size 2MB).

**Option A — Extract from ESP-SR component:**
1. Clone or download `https://github.com/espressif/esp-sr`
2. The model binary is in `esp-sr/model/` or generated during an ESP-IDF build
3. Look for `wn9_hiesp` model files

**Option B — Build with ESP-IDF example:**
1. Use the ESP-IDF `esp-sr` example project to build with menuconfig
2. Select WakeNet9 + "Hi ESP" wake word
3. Extract the `srmodels.bin` from the build output

**Option C — Use the jahrulnr/ESP32-WakeWord project:**
1. Clone `https://github.com/jahrulnr/ESP32-WakeWord`
2. The `model/srmodels.bin` in that repo contains a pre-built model
3. NOTE: It may contain extra models (picoTTS, etc.) — verify it includes `wn9_hiesp`

**Flashing the model:**
```bash
# Using esptool (adjust port as needed)
esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash 0x310000 srmodels.bin
```

Or create a `tools/partition_manager.py` PlatformIO extra script to automate this (see jahrulnr/ESP32-WakeWord `tools/partition_manager.py` for reference).

### Step 3: Apply ArduinoWebsockets TLS patch

After `pio run` downloads the library, patch these two files in `.pio/libdeps/seeed_xiao_esp32s3/ArduinoWebsockets/src/`:

**File 1:** `tiny_websockets/network/esp32/esp32_tcp.hpp`
Add to `SecuredEsp32TcpClient` class:
```cpp
void setInsecure() { this->client.setInsecure(); }
```

**File 2:** `websockets_client.cpp`
In `upgradeToSecuredConnection()` ESP32 block, after the `setPrivateKey` check, add:
```cpp
if(!this->_optional_ssl_ca_cert && !this->_optional_ssl_client_ca && !this->_optional_ssl_private_key) {
    client->setInsecure();
}
```

### Step 4: Update WiFi credentials

Edit `esp32_xiao_s3/src/lib_wifi.cpp`:
```cpp
const char* WIFI_SSID = "YOUR_WIFI_SSID";       // ← change
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"; // ← change
```

### Step 5: Build, flash, and test

```bash
cd esp32_xiao_s3

# Build
~/.platformio/penv/bin/pio run

# Flash firmware
~/.platformio/penv/bin/pio run -t upload

# Flash model to model partition (if not done in step 2)
esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash 0x310000 model/srmodels.bin

# Monitor serial output
~/.platformio/penv/bin/pio device monitor
```

**Expected boot log:**
```
[XIAO Voice V1.1-XIAO-WW] Starting...
[Setup] PSRAM: 8388608 bytes free
[Speaker] I2S initialized (24kHz, 16-bit mono)
[Mic] INMP441 initialized (16kHz, 32-bit read)
[Setup] Loading WakeNet9 model...
[WakeWord] Found model: wn9_hiesp
[WakeWord] Ready! Frame: 480 samples, Rate: 16000 Hz
[Setup] WakeNet9 'Hi ESP' ready!
[WiFi] Connected, IP: 192.168.x.x
[WS] Connected!
[XIAO Voice V1.1-XIAO-WW] Ready! Say 'Hi ESP' to talk.
```

### Step 6: Debug common issues

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `Failed to init SR models from partition` | Model not flashed or wrong offset | Re-flash `srmodels.bin` to `0x310000` |
| `No WakeNet model found` | `srmodels.bin` doesn't contain wn9_hiesp | Rebuild model with correct wake word |
| Build error: `esp_wn_iface.h not found` | ESP-SR not in pioarduino | Switch to develop branch or add `idf_component.yml` |
| `PSRAM: 0 bytes free` | PSRAM not enabled | Check `board_build.arduino.memory_type = qio_opi` and `-DBOARD_HAS_PSRAM` |
| Audio garbled to OpenAI | Resampling issue | Check `resample16to24()` output, verify 24kHz PCM16 |
| Wake word never triggers | Mic not at 16kHz or model wrong | Verify `MIC_SAMPLE_RATE=16000`, check model |
| Wake word triggers too easily | Detection threshold too low | Change `DET_MODE_90` to `DET_MODE_95` in `wake_word.cpp` |
| Stack overflow in micTask | WakeNet needs more stack | Increase `16384` in `xTaskCreatePinnedToCore` call |

## Architecture Overview

```
                      XIAO ESP32-S3
┌─────────────────────────────────────────────┐
│                                             │
│  Core 0: micTask                            │
│  ┌─────────────────────────────────────┐    │
│  │ I2S Read (16kHz, INMP441/PDM)       │    │
│  │         │                           │    │
│  │    ┌────▼─────┐                     │    │
│  │    │ LISTENING │──► WakeNet9 detect  │    │
│  │    └────┬─────┘    "Hi ESP"?        │    │
│  │         │ yes                       │    │
│  │    ┌────▼──────┐                    │    │
│  │    │ STREAMING │──► resample 16→24k │    │
│  │    │           │──► send via WS     │    │
│  │    │           │──► VAD silence?    │    │
│  │    └────┬──────┘                    │    │
│  │         │ silence                   │    │
│  │    send end_audio                   │    │
│  │    ┌────▼─────┐                     │    │
│  │    │ PLAYBACK │  (mic muted)        │    │
│  │    └────┬─────┘                     │    │
│  │         │ end_response              │    │
│  │         └──► back to LISTENING      │    │
│  └─────────────────────────────────────┘    │
│                                             │
│  Core 1: loop (WebSocket)                   │
│  ┌─────────────────────────────────────┐    │
│  │ WS poll → receive audio → i2s_write │    │
│  └─────────────────────────────────────┘    │
└─────────────────────────────────────────────┘
         │                    │
         │ WSS               │ WSS
         ▼                    ▼
   ┌──────────┐      ┌──────────────┐
   │  Server  │─────►│ OpenAI RT API│
   └──────────┘      └──────────────┘
```

## File Map

| File | Purpose |
|------|---------|
| `platformio.ini` | pioarduino platform, ESP-SR + PSRAM flags |
| `partitions.csv` | 8MB flash: 3MB app, 2MB model, 2.9MB spiffs |
| `src/config.h` | Pins, sample rates (16kHz mic, 24kHz speaker), LED |
| `src/main.cpp` | Setup: speaker, mic, WakeNet, WiFi, WS |
| `src/wake_word.cpp/h` | WakeNet9 init + detection (ESP-SR API) |
| `src/mic.cpp/h` | State machine, I2S read, 16→24kHz resample, VAD |
| `src/lib_speaker.cpp/h` | MAX98357A I2S output (24kHz) |
| `src/lib_websocket.cpp/h` | WSS connection, audio send/receive, state sync |
| `src/lib_wifi.cpp/h` | WiFi credentials, connection, power saving off |
