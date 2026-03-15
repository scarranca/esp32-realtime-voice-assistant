#ifndef WAKE_WORD_H
#define WAKE_WORD_H

#include <Arduino.h>

// Initialize WakeNet9 "Hi ESP" model from flash partition
bool wakeWordInit();

// Feed 16kHz PCM16 audio frame to WakeNet. Returns true if wake word detected.
// Buffer must contain exactly wakeWordFrameSize() samples.
bool wakeWordDetect(int16_t *samples);

// Get required frame size (samples per detect call)
int wakeWordFrameSize();

// Get required sample rate (should be 16000)
int wakeWordSampleRate();

#endif
