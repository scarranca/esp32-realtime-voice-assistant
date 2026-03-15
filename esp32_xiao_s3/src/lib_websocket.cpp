#include <Arduino.h>
#include <ArduinoWebsockets.h>
#include "config.h"
#include "lib_websocket.h"
#include "lib_speaker.h"
#include "mic.h"

using namespace websockets;

static WebsocketsClient client;
static volatile bool receivingAudio = false;

void onMessageCallback(WebsocketsMessage message)
{
    if (message.isBinary()) {
        uint8_t *payload = (uint8_t *)message.c_str();
        size_t length = message.length();
        if (length > 0) {
            if (!receivingAudio) {
                receivingAudio = true;
                setMicState(MIC_PLAYBACK);
                Serial.println("[WS] Audio playback started");
            }
            speakerPlay(payload, length);
        }
        return;
    }

    String data = message.data();
    Serial.print("[WS] ");
    Serial.println(data);

    // When AI response ends, go back to listening for wake word
    if (data.indexOf("\"end_response\"") >= 0) {
        receivingAudio = false;
        setMicState(MIC_LISTENING);
        Serial.println("[WS] Response complete, listening for 'Hi ESP'...");
    }
}

void onEventsCallback(WebsocketsEvent event, String data)
{
    switch (event) {
        case WebsocketsEvent::ConnectionOpened:
            Serial.println("[WS] Connected");
            break;
        case WebsocketsEvent::ConnectionClosed:
            Serial.println("[WS] Disconnected");
            receivingAudio = false;
            setMicState(MIC_LISTENING);
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
        receivingAudio = false;
        setMicState(MIC_LISTENING);
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
