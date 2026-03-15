#ifndef CONFIG_H
#define CONFIG_H

#include <driver/i2s.h>

// ── Firmware version ───────────────────────────────────────────────────────
#define FIRMWARE_VERSION "V1.0-XIAO"

// ── WiFi credentials ────────────────────────────────────────────────────────
extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;

// ── WebSocket server ────────────────────────────────────────────────────────
extern const char* WEBSOCKET_HOST;
#define WEBSOCKET_PORT 443
#define WS_PATH "/ws/voice"

// ── Board selection ─────────────────────────────────────────────────────────
// Uncomment ONE of the following to select your mic type:
#define USE_INMP441_MIC       // External INMP441 I2S mic
// #define USE_PDM_MIC        // Built-in PDM mic (XIAO ESP32-S3 Sense only)

// ── I2S Microphone pins (INMP441 - external) ──────────────────────────────
// XIAO ESP32-S3 pin mapping: D0=GPIO1, D1=GPIO2, D2=GPIO3,
//   D3=GPIO4, D4=GPIO5, D5=GPIO6, D6=GPIO43, D7=GPIO44,
//   D8=GPIO7, D9=GPIO8, D10=GPIO9
#define I2S_MIC_BCLK 1    // D0 - Bit Clock
#define I2S_MIC_WS   2    // D1 - Word Select (LRCLK)
#define I2S_MIC_DIN  3    // D2 - Serial Data In

// ── PDM Microphone pins (built-in on XIAO ESP32-S3 Sense) ─────────────────
#define PDM_MIC_CLK  42   // Internal PDM clock
#define PDM_MIC_DATA 41   // Internal PDM data

// ── I2S Speaker pins (MAX98357A) ──────────────────────────────────────────
#define I2S_SPEAKER_BCLK 4    // D3 - Bit Clock
#define I2S_SPEAKER_LRC  5    // D4 - Left Right Clock
#define I2S_SPEAKER_DOUT 6    // D5 - Data Out

// ── I2S ports ───────────────────────────────────────────────────────────────
#define I2S_PORT_MIC     I2S_NUM_0
#define I2S_PORT_SPEAKER I2S_NUM_1

// ── Audio configuration ─────────────────────────────────────────────────────
// 24kHz matches OpenAI Realtime API native format (PCM16 24kHz mono)
#define MIC_SAMPLE_RATE     24000
#define SPEAKER_SAMPLE_RATE 24000

// INMP441 outputs 24-bit data in 32-bit I2S frames
// Must read as 32-bit and shift >> 16 to get 16-bit samples
#ifdef USE_INMP441_MIC
#define MIC_I2S_BITS I2S_BITS_PER_SAMPLE_32BIT
#else
#define MIC_I2S_BITS I2S_BITS_PER_SAMPLE_16BIT
#endif

// ── DMA buffer configuration ────────────────────────────────────────────────
#define MIC_DMA_BUF_COUNT     8
#define MIC_DMA_BUF_LEN       256
#define SPEAKER_DMA_BUF_COUNT 16
#define SPEAKER_DMA_BUF_LEN   512

// ── Mic read buffer (samples per read) ──────────────────────────────────────
#define MIC_BUFFER_SAMPLES 512

// ── Voice Activity Detection (VAD) ──────────────────────────────────────────
// Energy threshold to detect speech (adjust based on environment noise)
// Higher = less sensitive, Lower = more sensitive
#define VAD_ENERGY_THRESHOLD    500

// Number of consecutive frames above threshold to confirm speech start
#define VAD_SPEECH_FRAMES       3

// Silence duration (ms) after speech to trigger end of utterance
#define VAD_SILENCE_TIMEOUT_MS  1500

// Minimum speech duration (ms) to avoid triggering on short noises
#define VAD_MIN_SPEECH_MS       200

// How many samples to use for energy calculation per frame
#define VAD_FRAME_SAMPLES       256

// ── LED indicator (built-in on XIAO ESP32-S3) ──────────────────────────────
#define LED_PIN 21   // Built-in user LED (active LOW on XIAO)

#endif
