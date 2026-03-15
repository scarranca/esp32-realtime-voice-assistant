#ifndef LIB_WEBSOCKET_H
#define LIB_WEBSOCKET_H

#include <stdint.h>

void connectToWebSocket();
void loopWebsocket();
void sendMessage(const char *message);
void sendBinaryData(const int16_t *buffer, size_t bytes);
void sendEndAudio();

#endif
