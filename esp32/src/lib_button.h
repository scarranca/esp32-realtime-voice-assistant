#ifndef LIB_BUTTON_H
#define LIB_BUTTON_H

#include <esp32-hal-gpio.h>
#include "config.h"

class ButtonChecker {
public:
    ButtonChecker() {
        pinMode(BUTTON_PIN, INPUT_PULLUP);
    }

    void loop() {
        lastTickState = thisTickState;
        thisTickState = !digitalRead(BUTTON_PIN);  // Active LOW
    }

    bool justPressed() {
        return thisTickState && !lastTickState;
    }

    bool justReleased() {
        return !thisTickState && lastTickState;
    }

    bool isPressed() {
        return thisTickState;
    }

private:
    bool lastTickState = false;
    bool thisTickState = false;
};

#endif
