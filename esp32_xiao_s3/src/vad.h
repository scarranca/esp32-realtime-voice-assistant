#ifndef VAD_H
#define VAD_H

#include <Arduino.h>

// VAD states
enum VadState {
    VAD_IDLE,       // Waiting for speech
    VAD_SPEAKING,   // Speech detected, streaming audio
    VAD_TRAILING    // Speech ended, waiting for silence timeout
};

void vadInit();
VadState vadProcess(int16_t *samples, size_t numSamples);
VadState vadGetState();
void vadReset();

#endif
