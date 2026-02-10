#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>
#include "config.h"
#include "lib_wifi.h"
#include "lib_websocket.h"
#include "lib_speaker.h"
#include "mic.h"
#include "lib_button.h"

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
ButtonChecker button;

void displayStatus(const char* line1, const char* line2 = nullptr)
{
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println(line1);
    if (line2) display.println(line2);
    display.display();
}

void setup()
{
    Serial.begin(115200);
    Serial.println("\n[MiniBot V5] Starting...");

    // Initialize OLED display
    Wire.begin(OLED_SDA, OLED_SCL);
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        displayStatus("MiniBot " FIRMWARE_VERSION, "Booting...");
    } else {
        Serial.println("[OLED] Init failed");
    }

    // Initialize audio I/O
    setupSpeaker();
    delay(100);
    setupMicrophone();
    delay(100);

    // Connect to network
    displayStatus("MiniBot " FIRMWARE_VERSION, "Connecting WiFi...");
    connectToWiFi();
    displayStatus("MiniBot " FIRMWARE_VERSION, "Connecting WS...");
    connectToWebSocket();

    // Start mic task on Core 0 (loop runs on Core 1)
    setRecording(false);
    xTaskCreatePinnedToCore(micTask, "micTask", 8192, NULL, 1, NULL, 0);

    displayStatus("MiniBot " FIRMWARE_VERSION, "Ready! Press to talk");
    Serial.println("[MiniBot V5] Ready! Press button to talk.");
}

void loop()
{
    button.loop();

    if (button.justPressed()) {
        Serial.println("[Button] Pressed - Recording...");

        // Stop speaker, clear DMA, then start mic
        i2s_stop(I2S_PORT_SPEAKER);
        i2s_zero_dma_buffer(I2S_PORT_SPEAKER);
        delay(50);

        i2s_start(I2S_PORT_MIC);
        delay(50);

        setRecording(true);
    }
    else if (button.justReleased()) {
        Serial.println("[Button] Released - Processing...");

        setRecording(false);

        // Stop mic, clear DMA, then start speaker
        i2s_stop(I2S_PORT_MIC);
        i2s_zero_dma_buffer(I2S_PORT_MIC);
        delay(50);

        i2s_start(I2S_PORT_SPEAKER);
        delay(50);

        // Tell server to commit audio and get response
        sendEndAudio();
    }

    loopWebsocket();
}
