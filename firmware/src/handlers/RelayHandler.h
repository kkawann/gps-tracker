#ifndef RELAY_HANDLER_H
#define RELAY_HANDLER_H

#include <Arduino.h>
#include <Preferences.h> // ⚠️ جدید - برای ذخیره‌سازی پایدار
#include "config.h"

class RelayHandler
{
public:
    RelayHandler();
    void begin();
    void turnOn();
    void turnOff();
    void toggle();
    void handleCommand(const char *action);
    bool getState() const { return _state; }

private:
    bool _state;
    unsigned long _lastChange;
    Preferences _prefs; // ⚠️ جدید

    void saveState(); // ⚠️ جدید - ذخیره در NVS
    void loadState(); // ⚠️ جدید - بارگذاری از NVS
};

#endif