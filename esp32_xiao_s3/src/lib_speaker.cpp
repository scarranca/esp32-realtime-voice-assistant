#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include "lib_speaker.h"

void setupSpeaker()
{
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SPEAKER_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = SPEAKER_DMA_BUF_COUNT,
        .dma_buf_len = SPEAKER_DMA_BUF_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SPEAKER_BCLK,
        .ws_io_num = I2S_SPEAKER_LRC,
        .data_out_num = I2S_SPEAKER_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    esp_err_t err = i2s_driver_install(I2S_PORT_SPEAKER, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[Speaker] Driver install failed: %s\n", esp_err_to_name(err));
        return;
    }

    err = i2s_set_pin(I2S_PORT_SPEAKER, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("[Speaker] Pin config failed: %s\n", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_PORT_SPEAKER);
        return;
    }

    Serial.println("[Speaker] I2S initialized (24kHz, 16-bit mono)");
}

void speakerPlay(uint8_t *payload, uint32_t len)
{
    size_t bytesWritten;
    i2s_write(I2S_PORT_SPEAKER, payload, len, &bytesWritten, portMAX_DELAY);
}
