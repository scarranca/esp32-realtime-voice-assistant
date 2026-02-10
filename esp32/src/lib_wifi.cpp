#include <WiFi.h>
#include <esp_wifi.h>
#include <Arduino.h>
#include "config.h"
#include "lib_wifi.h"

const char* WIFI_SSID = "CAHDZ";
const char* WIFI_PASSWORD = "CarrancA172428";
const char* WEBSOCKET_HOST = "esp32-voice-bot-production.up.railway.app";

void connectToWiFi()
{
    Serial.println("[WiFi] Connecting...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.print("[WiFi] Connected, IP: ");
    Serial.println(WiFi.localIP());

    // Disable WiFi power saving to prevent random latency spikes
    esp_wifi_set_ps(WIFI_PS_NONE);
    Serial.println("[WiFi] Power saving disabled");
}
