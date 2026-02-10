#include <Arduino.h>
#include "utils.h"

void *audio_malloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr) {
        Serial.println("[Utils] malloc failed");
    }
    return ptr;
}
