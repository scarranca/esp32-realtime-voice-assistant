#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include "mic.h"
#include "vad.h"
#include "lib_websocket.h"

// Playback flag - when true, mic reads are discarded to avoid echo
static volatile bool isPlayingBack = false;

void setPlayingBack(bool playing)
{
    isPlayingBack = playing;
}

void setupMicrophone()
{
#ifdef USE_INMP441_MIC
    // External INMP441 I2S microphone
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

    Serial.println("[Mic] INMP441 I2S initialized (24kHz, 32-bit)");

#else
    // Built-in PDM microphone (XIAO ESP32-S3 Sense)
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
        .sample_rate = MIC_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
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
        .bck_io_num = I2S_PIN_NO_CHANGE,
        .ws_io_num = PDM_MIC_CLK,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PDM_MIC_DATA
    };

    esp_err_t err = i2s_driver_install(I2S_PORT_MIC, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[Mic] PDM driver install failed: %s\n", esp_err_to_name(err));
        return;
    }

    err = i2s_set_pin(I2S_PORT_MIC, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("[Mic] PDM pin config failed: %s\n", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_PORT_MIC);
        return;
    }

    Serial.println("[Mic] PDM mic initialized (24kHz, 16-bit)");
#endif

    vadInit();
}

void micTask(void *parameter)
{
#ifdef USE_INMP441_MIC
    // Read buffer: 32-bit samples from INMP441
    int32_t rawBuffer[MIC_BUFFER_SAMPLES];
#endif
    // Output buffer: 16-bit PCM
    int16_t pcmBuffer[MIC_BUFFER_SAMPLES];

    while (true) {
        // During playback, keep reading but discard to prevent echo
        if (isPlayingBack) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        size_t bytesRead = 0;

#ifdef USE_INMP441_MIC
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
#else
        // PDM mic reads directly as 16-bit
        esp_err_t result = i2s_read(
            I2S_PORT_MIC,
            pcmBuffer,
            MIC_BUFFER_SAMPLES * sizeof(int16_t),
            &bytesRead,
            portMAX_DELAY
        );

        if (result != ESP_OK || bytesRead == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        size_t numSamples = bytesRead / sizeof(int16_t);
#endif

        // Run VAD on the PCM data
        VadState vadState = vadProcess(pcmBuffer, numSamples);

        if (vadState == VAD_SPEAKING) {
            // Stream audio to server during speech
            sendBinaryData(pcmBuffer, numSamples * sizeof(int16_t));
        }
        else if (vadState == VAD_TRAILING) {
            // Speech ended - tell server to process
            sendEndAudio();
            vadReset();
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
