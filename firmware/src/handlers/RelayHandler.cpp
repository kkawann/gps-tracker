#include "RelayHandler.h"

RelayHandler::RelayHandler()
{
    _state = false;
    _lastChange = 0;
}

void RelayHandler::begin()
{
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(LED_STATUS, OUTPUT);

    // ✅ FIX: قبل از تنظیم پیش‌فرض، وضعیت قبلی رو از NVS بخون
    _prefs.begin("relay", false); // namespace: "relay", readonly: false
    loadState();

    // حالا پین رو طبق وضعیت بازیابی‌شده تنظیم کن، نه همیشه LOW
    digitalWrite(RELAY_PIN, _state ? HIGH : LOW);
    digitalWrite(LED_STATUS, LOW);

    Serial.printf("[RELAY] Initialized - وضعیت بازیابی‌شده: %s\n",
                  _state ? "روشن" : "خاموش");
}

void RelayHandler::turnOn()
{
    if (_state)
        return; // اگه از قبل روشنه، هیچ‌کاری نکن

    digitalWrite(RELAY_PIN, HIGH);
    _state = true;
    _lastChange = millis();
    saveState(); // ⚠️ جدید - ذخیره در NVS

    // بلینک LED (غیربلاکینگ ترجیحاً، ولی در اینجا موقتاً همونطور میذاریم)
    for (int i = 0; i < 3; i++)
    {
        digitalWrite(LED_STATUS, HIGH);
        delay(100);
        digitalWrite(LED_STATUS, LOW);
        delay(100);
    }

    Serial.println(F("[RELAY] ✓ ENGINE START (ذخیره شد)"));
}

void RelayHandler::turnOff()
{
    if (!_state)
        return; // اگه از قبل خاموشه، هیچ‌کاری نکن

    digitalWrite(RELAY_PIN, LOW);
    _state = false;
    _lastChange = millis();
    saveState(); // ⚠️ جدید - ذخیره در NVS

    // بلینک سریع
    for (int i = 0; i < 5; i++)
    {
        digitalWrite(LED_STATUS, HIGH);
        delay(80);
        digitalWrite(LED_STATUS, LOW);
        delay(80);
    }

    Serial.println(F("[RELAY] ✗ ENGINE KILL (ذخیره شد)"));
}

void RelayHandler::toggle()
{
    if (_state)
    {
        turnOff();
    }
    else
    {
        turnOn();
    }
}

void RelayHandler::handleCommand(const char *action)
{
    if (strcmp(action, "start") == 0)
    {
        turnOn();
    }
    else if (strcmp(action, "kill") == 0)
    {
        turnOff();
    }
    else if (strcmp(action, "toggle") == 0)
    {
        toggle();
    }
}

// ═══════════════════════════════════════════════════════════════
// ═══ PERSISTENCE (NVS) ═══
// ═══════════════════════════════════════════════════════════════

void RelayHandler::saveState()
{
    // ✅ فقط اگه واقعاً تغییر کرده بنویس (کاهش wear روی flash)
    bool currentSaved = _prefs.getBool("state", false);
    if (currentSaved != _state)
    {
        _prefs.putBool("state", _state);
        Serial.printf("[RELAY] وضعیت در NVS ذخیره شد: %d\n", _state);
    }
}

void RelayHandler::loadState()
{
    // پیش‌فرض: false (خاموش) - اگه هیچ‌وقت ذخیره نشده باشه
    _state = _prefs.getBool("state", false);
    Serial.printf("[RELAY] وضعیت از NVS بارگذاری شد: %d\n", _state);
}