#ifndef SIM800_CLIENT_H
#define SIM800_CLIENT_H

#include <Arduino.h>
#include <Client.h>
#include "SIM800_Arduino.h"

class SIM800Client : public Client
{
private:
    SIM800_t *_sim;
    uint8_t _rxBuffer[512];
    uint16_t _rxLen;
    uint16_t _rxPos;
    bool _connected;
    unsigned long _timeout;
    bool _needsHardReset;
    uint8_t _consecutiveTcpTimeouts;
    static const uint8_t MAX_CONSECUTIVE_TCP_TIMEOUTS = 2;
    static SIM800Client *_instance;

    static void tcpDataCallback(const uint8_t *data, uint16_t len);
    static void tcpConnectCallback(bool connected);

    int attemptCipStart(const char *host, uint16_t port, bool *wasFastRejection);
    bool getIpStackState(char *outState, size_t outSize, unsigned long timeoutMs = 5000);
    bool recoverDeadPdpContext();

    // ⚠️ جدید - صبر برای idle شدن SMS state machine قبل از فرستادن هر AT
    // مستقیم دیگه (CIPSEND/CIPSTART/CIPCLOSE/...) روی همون UART مشترک.
    // اگه SMS در حال ارسال باشه (منتظر پرامپت '>' برای متن یا Ctrl+Z)،
    // فرستادن یک AT command دیگه همون وسط باعث می‌شه پرامپت‌ها بین دو
    // "مکالمه" قاطی بشن و کل پارسر AT مودم دچار desync بشه.
    bool waitForSmsIdle(unsigned long timeoutMs = 4000);

public:
    explicit SIM800Client(SIM800_t *sim);

    virtual int connect(IPAddress ip, uint16_t port) override;
    virtual int connect(const char *host, uint16_t port) override;
    virtual size_t write(uint8_t b) override;
    virtual size_t write(const uint8_t *buf, size_t size) override;
    virtual int available() override;
    virtual int read() override;
    virtual int read(uint8_t *buf, size_t size) override;
    virtual int peek() override;
    virtual void flush() override;
    virtual void stop() override;
    virtual uint8_t connected() override;
    virtual operator bool() override;
    bool needsHardReset() const { return _needsHardReset; }
    void clearNeedsHardReset() { _needsHardReset = false; }
    void setTimeout(unsigned long timeout);
    void loop();
};

#endif