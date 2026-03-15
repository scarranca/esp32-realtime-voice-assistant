#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include "lib_wifi.h"
#include "lib_websocket.h"
#include "lib_speaker.h"
#include "mic.h"
#include "vad.h"

// LED indicator for status
void ledOn()  { digitalWrite(LED_PIN, LOW); }   // Active LOW on XIAO
void ledOff() { digitalWrite(LED_PIN, HIGH); }

void setup()
{
    Serial.begin(115200);
    Serial.println("\n[XIAO Voice " FIRMWARE_VERSION "] Starting...");

    // Setup LED indicator
    pinMode(LED_PIN, OUTPUT);
    ledOff();

    // Initialize audio I/O
    setupSpeaker();
    delay(100);
    setupMicrophone();
    delay(100);

    // Connect to network
    Serial.println("[Setup] Connecting WiFi...");
    connectToWiFi();
    Serial.println("[Setup] Connecting WebSocket...");
    connectToWebSocket();

    // Start mic task on Core 0 (loop runs on Core 1)
    // Mic runs continuously - VAD handles speech detection
    xTaskCreatePinnedToCore(micTask, "micTask", 8192, NULL, 1, NULL, 0);

    ledOn();
    Serial.println("[XIAO Voice " FIRMWARE_VERSION "] Ready! Say 'hi' to start talking.");
}

void loop()
{
    loopWebsocket();
}
