#ifndef CONFIG_H
#define CONFIG_H

#include <driver/i2s.h>

// ── Firmware version ───────────────────────────────────────────────────────
#define FIRMWARE_VERSION "V5.0"

// ── WiFi credentials ────────────────────────────────────────────────────────
extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;

// ── WebSocket server ────────────────────────────────────────────────────────
extern const char* WEBSOCKET_HOST;
#define WEBSOCKET_PORT 443
#define WS_PATH "/ws/voice"

// ── I2S Microphone pins (INMP441) ──────────────────────────────────────────
#define I2S_MIC_BCLK 4    // Bit Clock
#define I2S_MIC_WS   5    // Word Select (LRCLK)
#define I2S_MIC_DIN  6    // Serial Data In

// ── I2S Speaker pins (MAX98357A, LR=GND → left channel) ───────────────────
#define I2S_SPEAKER_BCLK 16   // Bit Clock
#define I2S_SPEAKER_LRC  17   // Left Right Clock
#define I2S_SPEAKER_DOUT 15   // Data Out

// ── OLED Display (SSD1306 128x64) ─────────────────────────────────────────
#define OLED_SDA 8
#define OLED_SCL 9
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_ADDR 0x3C

// ── Button (active LOW, INPUT_PULLUP) ──────────────────────────────────────
#define BUTTON_PIN 2

// ── I2S ports ───────────────────────────────────────────────────────────────
#define I2S_PORT_MIC     I2S_NUM_0
#define I2S_PORT_SPEAKER I2S_NUM_1

// ── Audio configuration ─────────────────────────────────────────────────────
// 24kHz matches OpenAI Realtime API native format (PCM16 24kHz mono)
#define MIC_SAMPLE_RATE     24000
#define SPEAKER_SAMPLE_RATE 24000

// INMP441 outputs 24-bit data in 32-bit I2S frames
// Must read as 32-bit and shift >> 16 to get 16-bit samples
#define MIC_I2S_BITS I2S_BITS_PER_SAMPLE_32BIT

// ── DMA buffer configuration ────────────────────────────────────────────────
#define MIC_DMA_BUF_COUNT     8
#define MIC_DMA_BUF_LEN       256
#define SPEAKER_DMA_BUF_COUNT 16
#define SPEAKER_DMA_BUF_LEN   512

// ── Mic read buffer (32-bit samples) ────────────────────────────────────────
#define MIC_BUFFER_SAMPLES 512

#endif
