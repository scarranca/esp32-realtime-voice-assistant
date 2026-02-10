#include <Arduino.h>
#include <ArduinoWebsockets.h>
#include "config.h"
#include "lib_websocket.h"
#include "lib_speaker.h"

using namespace websockets;

static WebsocketsClient client;

void onMessageCallback(WebsocketsMessage message)
{
    if (message.isBinary()) {
        // Binary = audio data from server → play directly on speaker
        uint8_t *payload = (uint8_t *)message.c_str();
        size_t length = message.length();
        if (length > 0) {
            speakerPlay(payload, length);
        }
        return;
    }

    // Text message from server (metadata, end_response, error)
    Serial.print("[WS] ");
    Serial.println(message.data());
}

void onEventsCallback(WebsocketsEvent event, String data)
{
    switch (event) {
        case WebsocketsEvent::ConnectionOpened:
            Serial.println("[WS] Connected");
            break;
        case WebsocketsEvent::ConnectionClosed:
            Serial.println("[WS] Disconnected");
            break;
        case WebsocketsEvent::GotPing:
            Serial.println("[WS] Ping");
            break;
        case WebsocketsEvent::GotPong:
            Serial.println("[WS] Pong");
            break;
    }
}

void connectToWebSocket()
{
    client.onMessage(onMessageCallback);
    client.onEvent(onEventsCallback);

    // Build WSS URL for Railway
    String wsUrl = String("wss://") + WEBSOCKET_HOST + WS_PATH;
    Serial.print("[WS] Connecting to ");
    Serial.println(wsUrl);

    while (!client.connect(wsUrl)) {
        Serial.println("[WS] Connection failed, retrying in 2s...");
        delay(2000);
    }

    Serial.println("[WS] Connected!");
}

void loopWebsocket()
{
    if (!client.available()) {
        Serial.println("[WS] Lost connection, reconnecting...");
        connectToWebSocket();
    }
    client.poll();
}

void sendMessage(const char *message)
{
    if (client.available()) {
        client.send(message);
    }
}

void sendBinaryData(const int16_t *buffer, size_t bytes)
{
    if (client.available()) {
        client.sendBinary((const char *)buffer, bytes);
    }
}

void sendEndAudio()
{
    sendMessage("{\"type\":\"end_audio\"}");
}
