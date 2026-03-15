#ifndef MIC_H
#define MIC_H

#include <Arduino.h>

// Mic states
enum MicState {
    MIC_LISTENING,   // Feeding audio to WakeNet, waiting for "Hi ESP"
    MIC_STREAMING,   // Wake word detected, streaming audio to server
    MIC_PLAYBACK     // AI is responding, mic muted
};

void setupMicrophone();
void micTask(void *parameter);
void setMicState(MicState state);
MicState getMicState();

#endif
