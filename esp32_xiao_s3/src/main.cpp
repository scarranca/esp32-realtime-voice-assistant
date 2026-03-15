#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"
#include "lib_wifi.h"
#include "lib_websocket.h"
#include "lib_speaker.h"
#include "mic.h"
#include "wake_word.h"

// LED indicator (active LOW on XIAO)
void ledOn()  { digitalWrite(LED_PIN, LOW); }
void ledOff() { digitalWrite(LED_PIN, HIGH); }

void setup()
{
    Serial.begin(115200);
    Serial.println("\n[XIAO Voice " FIRMWARE_VERSION "] Starting...");

    // LED setup
    pinMode(LED_PIN, OUTPUT);
    ledOff();

    // Enable PSRAM allocation for large buffers
    #ifdef BOARD_HAS_PSRAM
    heap_caps_malloc_extmem_enable(4096);
    Serial.printf("[Setup] PSRAM: %d bytes free\n", ESP.getFreePsram());
    #endif

    // Initialize speaker
    setupSpeaker();
    delay(100);

    // Initialize microphone (16kHz for WakeNet)
    setupMicrophone();
    delay(100);

    // Initialize WakeNet9 "Hi ESP" wake word engine
    Serial.println("[Setup] Loading WakeNet9 model...");
    if (!wakeWordInit()) {
        Serial.println("[Setup] WakeWord init FAILED! Check model partition.");
        Serial.println("[Setup] Falling back to VAD-only mode.");
    } else {
        Serial.println("[Setup] WakeNet9 'Hi ESP' ready!");
    }

    // Connect to network
    Serial.println("[Setup] Connecting WiFi...");
    connectToWiFi();
    Serial.println("[Setup] Connecting WebSocket...");
    connectToWebSocket();

    // Start mic task on Core 0 (loop runs on Core 1)
    // Increased stack for WakeNet processing
    xTaskCreatePinnedToCore(micTask, "micTask", 16384, NULL, 1, NULL, 0);

    ledOn();
    Serial.println("[XIAO Voice " FIRMWARE_VERSION "] Ready! Say 'Hi ESP' to talk.");
}

void loop()
{
    loopWebsocket();
}
