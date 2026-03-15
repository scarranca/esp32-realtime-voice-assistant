#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include "mic.h"
#include "wake_word.h"
#include "vad.h"
#include "lib_websocket.h"

static volatile MicState micState = MIC_LISTENING;

void setMicState(MicState state)
{
    micState = state;
}

MicState getMicState()
{
    return micState;
}

// Resample 16kHz PCM16 → 24kHz PCM16 using linear interpolation
// Ratio 3:2 — for every 2 input samples, produces 3 output samples
static void resample16to24(const int16_t *in, size_t inSamples,
                           int16_t *out, size_t *outSamples)
{
    size_t numOut = (inSamples * 3) / 2;
    for (size_t j = 0; j < numOut; j++) {
        // Input position = j * 2/3 (fixed-point: j*2 / 3)
        uint32_t pos2 = j * 2;
        uint32_t idx = pos2 / 3;
        uint32_t frac = pos2 % 3;  // 0, 1, or 2

        if (idx + 1 < inSamples) {
            out[j] = (int16_t)(((3 - frac) * (int32_t)in[idx] +
                                 frac * (int32_t)in[idx + 1]) / 3);
        } else {
            out[j] = in[idx < inSamples ? idx : inSamples - 1];
        }
    }
    *outSamples = numOut;
}

void setupMicrophone()
{
#ifdef USE_INMP441_MIC
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = MIC_SAMPLE_RATE,  // 16kHz for WakeNet
        .bits_per_sample = MIC_I2S_BITS,
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
    Serial.println("[Mic] INMP441 initialized (16kHz, 32-bit read)");

#else
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
    Serial.println("[Mic] PDM mic initialized (16kHz, 16-bit)");
#endif
}

void micTask(void *parameter)
{
#ifdef USE_INMP441_MIC
    int32_t rawBuffer[MIC_BUFFER_SAMPLES];
#endif
    int16_t pcmBuffer[MIC_BUFFER_SAMPLES];

    // Resampled buffer: 480 * 3/2 = 720 samples max
    int16_t resampledBuffer[MIC_BUFFER_SAMPLES * 2];

    // VAD state for detecting end of speech
    unsigned long lastSpeechTime = 0;
    unsigned long speechStartTime = 0;
    bool speechActive = false;

    while (true) {
        if (micState == MIC_PLAYBACK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        size_t bytesRead = 0;

#ifdef USE_INMP441_MIC
        esp_err_t result = i2s_read(
            I2S_PORT_MIC, rawBuffer,
            MIC_BUFFER_SAMPLES * sizeof(int32_t),
            &bytesRead, portMAX_DELAY
        );
        if (result != ESP_OK || bytesRead == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        size_t numSamples = bytesRead / sizeof(int32_t);
        for (size_t i = 0; i < numSamples; i++) {
            pcmBuffer[i] = (int16_t)(rawBuffer[i] >> 16);
        }
#else
        esp_err_t result = i2s_read(
            I2S_PORT_MIC, pcmBuffer,
            MIC_BUFFER_SAMPLES * sizeof(int16_t),
            &bytesRead, portMAX_DELAY
        );
        if (result != ESP_OK || bytesRead == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        size_t numSamples = bytesRead / sizeof(int16_t);
#endif

        if (micState == MIC_LISTENING) {
            // Feed audio to WakeNet for wake word detection
            if (wakeWordDetect(pcmBuffer)) {
                Serial.println("[Mic] >>> Wake word 'Hi ESP' detected! <<<");
                micState = MIC_STREAMING;
                speechActive = true;
                speechStartTime = millis();
                lastSpeechTime = millis();
            }
        }
        else if (micState == MIC_STREAMING) {
            // Resample 16kHz → 24kHz and send to server
            size_t outSamples = 0;
            resample16to24(pcmBuffer, numSamples, resampledBuffer, &outSamples);
            sendBinaryData(resampledBuffer, outSamples * sizeof(int16_t));

            // Simple energy-based VAD for end-of-speech detection
            uint64_t energy = 0;
            for (size_t i = 0; i < numSamples; i++) {
                int32_t s = pcmBuffer[i];
                energy += (uint64_t)(s * s);
            }
            energy /= numSamples;

            bool isSpeech = energy > ((uint32_t)VAD_ENERGY_THRESHOLD * VAD_ENERGY_THRESHOLD);
            unsigned long now = millis();

            if (isSpeech) {
                lastSpeechTime = now;
            }

            // End of speech: silence for VAD_SILENCE_TIMEOUT_MS
            if ((now - lastSpeechTime) > VAD_SILENCE_TIMEOUT_MS) {
                unsigned long duration = now - speechStartTime;
                if (duration >= VAD_MIN_SPEECH_MS) {
                    Serial.printf("[Mic] Speech ended (%lums), sending to AI\n", duration);
                    sendEndAudio();
                    micState = MIC_PLAYBACK;
                    speechActive = false;
                } else {
                    // Too short, go back to listening
                    Serial.println("[Mic] Too short, back to listening");
                    micState = MIC_LISTENING;
                    speechActive = false;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
