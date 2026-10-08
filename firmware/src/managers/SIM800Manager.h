#ifndef SIM800_MANAGER_H
#define SIM800_MANAGER_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include "SIM800Client.h"
#include "config.h"

#define SIM800MGR_READY_TIMEOUT_MS 15000
#define INIT_TIMEOUT_MS 30000

class SIM800Manager
{
public:
    SIM800_t *getSIM() { return &_sim; }
    SIM800Manager(HardwareSerial *serial);

    void begin(HardwareSerial *serial);
    void loop();

    bool sendRawAT(const char *cmd, char *response, size_t responseSize, unsigned long timeoutMs = 5000);
    bool isReady();
    bool isGprsConnected();
    int getSignalQuality();
    const char *getIP();
    bool connectGPRS(const char *apn, const char *user = "", const char *pass = "");

    // SMS
    bool sendSMS(const char *number, const char *text);
    bool sendUnicodeSMS(const char *number, const char *utf8Text);
    bool isSmsBusy();

    // Escalation
    bool isModemResponding();
    bool isSimCardPresent();
    bool verifyGprsAlive();
    bool softReinit();
    bool hardReset();

    void printStatus();

    // ⚠️ پابلیک شد چون از خارج کلاس (مثلاً main.cpp و سناریوهای escalation) صدا زده می‌شه
    void pauseProcessing(bool pause) { _processingPaused = pause; }

    static SIM800Manager *_instance;

private:
    HardwareSerial *_serial;
    SIM800_t _sim;
    bool _initialized;
    bool _gprsReady;
    unsigned long _lastProcessTime;
    unsigned long _lastStatusCheck;
    bool _processingPaused;

    // SMS Unicode
    bool containsNonAscii(const char *text);
    bool utf8ToUcs2Hex(const char *utf8, char *outHex, size_t outSize);
    bool waitForChar(char target, unsigned long timeoutMs);

    // Callback signatures must match SIM800_Arduino.h typedefs
    static void initCallback(bool success);
    static void gprsCallback(bool connected);
    static void smsCallback(const char *text, const char *sender);
};

#endif // SIM800_MANAGER_H