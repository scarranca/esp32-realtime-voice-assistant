#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include "mic.h"
#include "lib_websocket.h"

static volatile bool isRecording = false;

void setRecording(bool recording)
{
    isRecording = recording;
}

void setupMicrophone()
{
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = MIC_SAMPLE_RATE,
        .bits_per_sample = MIC_I2S_BITS,  // 32-bit for INMP441
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = MIC_DMA_BUF_COUNT,
        .dma_buf_len = MIC_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_MIC_BCLK,
        .ws_io_num = I2S_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_MIC_DIN
    };

    esp_err_t err = i2s_driver_install(I2S_PORT_MIC, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[Mic] Driver install failed: %s\n", esp_err_to_name(err));
        return;
    }

    err = i2s_set_pin(I2S_PORT_MIC, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("[Mic] Pin config failed: %s\n", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_PORT_MIC);
        return;
    }

    Serial.println("[Mic] I2S initialized (24kHz, 32-bit INMP441)");
}

void micTask(void *parameter)
{
    // Read buffer: 32-bit samples from INMP441
    int32_t rawBuffer[MIC_BUFFER_SAMPLES];
    // Output buffer: 16-bit PCM for WebSocket
    int16_t pcmBuffer[MIC_BUFFER_SAMPLES];

    while (true) {
        if (!isRecording) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t bytesRead = 0;
        esp_err_t result = i2s_read(
            I2S_PORT_MIC,
            rawBuffer,
            MIC_BUFFER_SAMPLES * sizeof(int32_t),
            &bytesRead,
            portMAX_DELAY
        );

        if (result != ESP_OK || bytesRead == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        // Convert 32-bit INMP441 samples to 16-bit PCM
        size_t numSamples = bytesRead / sizeof(int32_t);
        for (size_t i = 0; i < numSamples; i++) {
            pcmBuffer[i] = (int16_t)(rawBuffer[i] >> 16);
        }

        // Send 16-bit PCM binary over WebSocket
        sendBinaryData(pcmBuffer, numSamples * sizeof(int16_t));

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
