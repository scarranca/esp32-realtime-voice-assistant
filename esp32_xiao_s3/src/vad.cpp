#include "vad.h"
#include "config.h"

static VadState state = VAD_IDLE;
static int speechFrameCount = 0;
static unsigned long speechStartTime = 0;
static unsigned long lastSpeechTime = 0;

void vadInit()
{
    state = VAD_IDLE;
    speechFrameCount = 0;
    speechStartTime = 0;
    lastSpeechTime = 0;
}

void vadReset()
{
    vadInit();
}

VadState vadGetState()
{
    return state;
}

// Calculate RMS energy of audio samples
static uint32_t calculateEnergy(int16_t *samples, size_t numSamples)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < numSamples; i++) {
        int32_t s = samples[i];
        sum += (uint64_t)(s * s);
    }
    return (uint32_t)(sum / numSamples);
}

VadState vadProcess(int16_t *samples, size_t numSamples)
{
    uint32_t energy = calculateEnergy(samples, numSamples);
    bool isSpeech = energy > ((uint32_t)VAD_ENERGY_THRESHOLD * VAD_ENERGY_THRESHOLD);
    unsigned long now = millis();

    switch (state) {
        case VAD_IDLE:
            if (isSpeech) {
                speechFrameCount++;
                if (speechFrameCount >= VAD_SPEECH_FRAMES) {
                    state = VAD_SPEAKING;
                    speechStartTime = now;
                    lastSpeechTime = now;
                    Serial.println("[VAD] Speech detected!");
                }
            } else {
                speechFrameCount = 0;
            }
            break;

        case VAD_SPEAKING:
            if (isSpeech) {
                lastSpeechTime = now;
            } else {
                // Check if silence has lasted long enough
                if ((now - lastSpeechTime) > VAD_SILENCE_TIMEOUT_MS) {
                    unsigned long speechDuration = now - speechStartTime;
                    if (speechDuration >= VAD_MIN_SPEECH_MS) {
                        state = VAD_TRAILING;
                        Serial.printf("[VAD] Speech ended (duration: %lums)\n", speechDuration);
                    } else {
                        // Too short, was probably noise
                        Serial.println("[VAD] Too short, ignoring");
                        state = VAD_IDLE;
                        speechFrameCount = 0;
                    }
                }
            }
            break;

        case VAD_TRAILING:
            // Caller should handle this state and reset
            break;
    }

    return state;
}
