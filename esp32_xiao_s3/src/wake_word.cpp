#include "wake_word.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"

static const esp_wn_iface_t *wakenet = NULL;
static model_iface_data_t *wn_handle = NULL;
static int frameSize = 0;

bool wakeWordInit()
{
    // Load models from the "model" flash partition
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) {
        Serial.println("[WakeWord] Failed to init SR models from partition");
        return false;
    }

    // Find the WakeNet model (e.g., wn9_hiesp)
    char *wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
    if (!wn_name) {
        Serial.println("[WakeWord] No WakeNet model found");
        return false;
    }
    Serial.printf("[WakeWord] Found model: %s\n", wn_name);

    // Get the WakeNet interface
    wakenet = (const esp_wn_iface_t *)esp_wn_handle_from_name(wn_name);
    if (!wakenet) {
        Serial.println("[WakeWord] Failed to get WakeNet handle");
        return false;
    }

    // Create model instance (DET_MODE_90 = 90% detection threshold)
    wn_handle = wakenet->create(wn_name, DET_MODE_90);
    if (!wn_handle) {
        Serial.println("[WakeWord] Failed to create WakeNet instance");
        return false;
    }

    frameSize = wakenet->get_samp_chunksize(wn_handle);
    int sampleRate = wakenet->get_samp_rate(wn_handle);
    Serial.printf("[WakeWord] Ready! Frame: %d samples, Rate: %d Hz\n", frameSize, sampleRate);

    return true;
}

bool wakeWordDetect(int16_t *samples)
{
    if (!wakenet || !wn_handle) return false;

    int result = wakenet->detect(wn_handle, samples);
    return (result > 0);
}

int wakeWordFrameSize()
{
    return frameSize;
}

int wakeWordSampleRate()
{
    if (!wakenet || !wn_handle) return 16000;
    return wakenet->get_samp_rate(wn_handle);
}
