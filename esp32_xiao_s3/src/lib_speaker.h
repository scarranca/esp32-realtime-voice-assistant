#ifndef LIB_SPEAKER_H
#define LIB_SPEAKER_H

#include <Arduino.h>

void setupSpeaker();
void speakerPlay(uint8_t *payload, uint32_t len);

#endif
