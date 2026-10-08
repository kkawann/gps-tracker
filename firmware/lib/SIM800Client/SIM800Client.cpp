#include "SIM800Client.h"
#include "config.h"
#include <esp_task_wdt.h> // ⚠️ جدید - برای feed کردن واچ‌داگ در طول انتظارهای بلاکینگ این فایل
SIM800Client *SIM800Client::_instance = nullptr;

SIM800Client::SIM800Client(SIM800_t *sim)
{
    _sim = sim;
    _rxLen = 0;
    _rxPos = 0;
    _connected = false;
    _timeout = 30000;
    _needsHardReset = false;
    _consecutiveTcpTimeouts = 0; // ⚠️ جدید

    _instance = this;
}
static const uint32_t SOFT_REINIT_TIMEOUT_MS = 15000;
static const uint32_t HARD_RESET_INIT_TIMEOUT_MS = 30000;

// ─────────────────────────────────────────────
// Static Callbacks
// ─────────────────────────────────────────────

void SIM800Client::tcpConnectCallback(bool connected)
{
    if (!_instance)
        return;

    _instance->_connected = connected;

    Serial.print(F("[SIM800Client] TCP "));
    Serial.println(connected ? F("Connected ✓") : F("Disconnected ✗"));

    if (!connected)
    {
        _instance->_rxLen = 0;
        _instance->_rxPos = 0;
    }
}

void SIM800Client::tcpDataCallback(const uint8_t *data, uint16_t len)
{
    if (!_instance || len == 0)
        return;

    // ✅ جمع کردن بایت‌ها در بافر (نه log کردن هر بایت)
    uint16_t space = sizeof(_instance->_rxBuffer) - _instance->_rxLen;
    uint16_t toCopy = (len < space) ? len : space;

    if (toCopy > 0)
    {
        memcpy(_instance->_rxBuffer + _instance->_rxLen, data, toCopy);
        _instance->_rxLen += toCopy;
    }
}

// ─────────────────────────────────────────────
// ⚠️ جدید - چک خام وضعیت IP stack (AT+CIPSTATUS)
// ─────────────────────────────────────────────
bool SIM800Client::getIpStackState(char *outState, size_t outSize, unsigned long timeoutMs)
{
    if (!outState || outSize == 0)
        return false;
    outState[0] = '\0';

    while (_sim->serial->available())
        _sim->serial->read();

    _sim->serial->print("AT+CIPSTATUS\r\n");

    char resp[128];
    size_t idx = 0;
    resp[0] = '\0';
    unsigned long start = millis();

    while (millis() - start < timeoutMs)
    {
        esp_task_wdt_reset();
        while (_sim->serial->available() && idx < sizeof(resp) - 1)
        {
            resp[idx++] = (char)_sim->serial->read();
        }
        resp[idx] = '\0';

        if ((strstr(resp, "OK") && strstr(resp, "STATE:")) || strstr(resp, "ERROR"))
            break;

        delay(20);
    }

    const char *statePos = strstr(resp, "STATE:");
    if (!statePos)
        return false;

    statePos += 6;
    while (*statePos == ' ')
        statePos++;

    size_t i = 0;
    while (*statePos && *statePos != '\r' && *statePos != '\n' && i < outSize - 1)
    {
        outState[i++] = *statePos++;
    }
    outState[i] = '\0';

    return i > 0;
}

// ─────────────────────────────────────────────
// ⚠️ جدید - ترمیم PDP context مرده
// ─────────────────────────────────────────────
bool SIM800Client::recoverDeadPdpContext()
{
    char state[48];
    bool gotState = getIpStackState(state, sizeof(state));

    Serial.print(F("[SIM800Client] CIPSTATUS: "));
    Serial.println(gotState ? state : "(بدون پاسخ)");

    if (gotState &&
        (strstr(state, "GPRSACT") || strstr(state, "STATUS") ||
         strstr(state, "INITIAL") || strstr(state, "START")))
    {
        Serial.println(F("[SIM800Client] وضعیت IP stack سالم به نظر می‌رسه - نیازی به ری‌ست PDP نیست"));
        return true;
    }

    Serial.println(F("[SIM800Client] ⚠⚠ PDP context مرده به نظر می‌رسه - تلاش نرم: CIPSHUT + اتصال مجدد GPRS"));

    SIM800_GprsDisconnect(_sim);
    unsigned long t = millis();
    while (_sim->gprs_state != GPRS_IDLE && millis() - t < 10000)
    {
        SIM800_Process(_sim);
        esp_task_wdt_reset();
        delay(20);
    }
    _sim->gprs_state = GPRS_IDLE;

    SIM800_Result_t gres = SIM800_GprsConnect(_sim, GPRS_APN, GPRS_USER, GPRS_PASS);
    if (gres != SIM800_OK)
    {
        Serial.println(F("[SIM800Client] شروع اتصال مجدد GPRS ناموفق بود"));
        _needsHardReset = true;
        return false;
    }

    unsigned long gt = millis();
    while (!SIM800_GprsIsConnected(_sim) && (millis() - gt < 60000))
    {
        SIM800_Process(_sim);
        esp_task_wdt_reset();
        delay(100);
    }

    bool ok = SIM800_GprsIsConnected(_sim);

    if (ok)
    {
        Serial.println(F("[SIM800Client] ✓ ترمیم نرم موفق بود - GPRS دوباره کامل وصل شد"));
    }
    else
    {
        Serial.println(F("[SIM800Client] ✗ ترمیم نرم هم ناموفق بود - hard reset فیزیکی درخواست شد"));
        _needsHardReset = true;
    }

    return ok;
}

// ─────────────────────────────────────────────
// ⚠️ جدید - یک تلاش CIPSTART
// ─────────────────────────────────────────────
int SIM800Client::attemptCipStart(const char *host, uint16_t port, bool *wasFastRejection)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",\"%d\"", host, port);

    _sim->tcp_state = TCP_CONNECTING;

    if (SIM800_SendCommand(_sim, cmd, "CONNECT", 15000) != SIM800_OK)
    {
        Serial.println(F("[SIM800Client] Failed to send CIPSTART"));
        if (wasFastRejection)
            *wasFastRejection = false;
        return 0;
    }

    Serial.print(F("[SIM800Client] Waiting"));
    unsigned long start = millis();

    while (millis() - start < _timeout)
    {
        SIM800_Process(_sim);
        esp_task_wdt_reset();

        if (_sim->tcp_state == TCP_CONNECTED)
        {
            _connected = true;
            _consecutiveTcpTimeouts = 0;
            Serial.println(F(" OK"));
            if (wasFastRejection)
                *wasFastRejection = false;
            return 1;
        }

        if (_sim->tcp_state != TCP_CONNECTED &&
            _sim->cmd_state == CMD_IDLE &&
            millis() - start > 2000)
        {
            unsigned long elapsed = millis() - start;
            Serial.println(F(" REJECTED/TIMEOUT"));
            _sim->tcp_state = TCP_IDLE;

            bool fast = (elapsed < 4000);
            if (wasFastRejection)
                *wasFastRejection = fast;

            if (!fast)
            {
                _consecutiveTcpTimeouts++;
                Serial.print(F("[SIM800Client] ⚠ CIPSTART timeout کامل - شمارنده: "));
                Serial.println(_consecutiveTcpTimeouts);

                if (_consecutiveTcpTimeouts >= MAX_CONSECUTIVE_TCP_TIMEOUTS)
                {
                    Serial.println(F("[SIM800Client] ⚠⚠ چند CIPSTART timeout کامل پشت‌سرهم - hard reset فوری درخواست شد"));
                    _needsHardReset = true;
                }
            }
            return 0;
        }

        delay(10);
        if ((millis() - start) % 1000 < 10)
            Serial.print(F("."));
    }

    Serial.println(F(" TIMEOUT"));
    _sim->tcp_state = TCP_IDLE;
    if (wasFastRejection)
        *wasFastRejection = false;
    return 0;
}

// ─────────────────────────────────────────────
// ⚠️ جدید - صبر برای idle شدن SMS قبل از AT بعدی
// ─────────────────────────────────────────────
bool SIM800Client::waitForSmsIdle(unsigned long timeoutMs)
{
    if (_sim->sms_state == SMS_IDLE)
        return true;

    Serial.println(F("[SIM800Client] ⏳ SMS در حال ارسال - صبر قبل از AT بعدی روی همون UART..."));
    unsigned long start = millis();
    while (_sim->sms_state != SMS_IDLE && millis() - start < timeoutMs)
    {
        SIM800_Process(_sim);
        esp_task_wdt_reset();
        delay(20);
    }

    bool idle = (_sim->sms_state == SMS_IDLE);
    if (!idle)
    {
        Serial.println(F("[SIM800Client] ⚠ SMS بعد از timeout هنوز idle نشد - با احتیاط ادامه می‌دیم"));
    }
    return idle;
}

// ─────────────────────────────────────────────
// write (توابع ولید و غیراضافی)
// ─────────────────────────────────────────────

size_t SIM800Client::write(uint8_t b)
{
    return write(&b, 1);
}

size_t SIM800Client::write(const uint8_t *buf, size_t size)
{
    if (!_connected || size == 0)
        return 0;

    // ⚠️ جدید - قبل از هر چیز صبر کن SMS تموم بشه تا سریال قاطی نکنه
    waitForSmsIdle();

    Serial.print(F("[SIM800Client] TX: "));
    Serial.print(size);
    Serial.println(F("B"));

    unsigned long t = millis();
    while (_sim->tcp_state != TCP_CONNECTED && millis() - t < 2000)
    {
        SIM800_Process(_sim);
        esp_task_wdt_reset();
        delay(10);
    }

    if (_sim->tcp_state != TCP_CONNECTED)
    {
        Serial.println(F("[SIM800Client] Not connected!"));
        return 0;
    }

    SIM800_Result_t res = SIM800_TcpSend(_sim, buf, (uint16_t)size);
    if (res != SIM800_OK)
    {
        Serial.println(F("[SIM800Client] TcpSend failed"));
        return 0;
    }

    t = millis();
    while ((_sim->tcp_state == TCP_WAIT_SEND_PROMPT ||
            _sim->tcp_state == TCP_SENDING) &&
           millis() - t < 10000)
    {
        SIM800_Process(_sim);
        esp_task_wdt_reset();
        delay(5);
    }

    bool sendOk = (_sim->tcp_state == TCP_CONNECTED);

    if (sendOk)
    {
        _consecutiveTcpTimeouts = 0;
    }
    else
    {
        _consecutiveTcpTimeouts++;
        Serial.print(F("[SIM800Client] ⚠ CIPSEND timeout - شمارنده: "));
        Serial.println(_consecutiveTcpTimeouts);

        if (_consecutiveTcpTimeouts >= MAX_CONSECUTIVE_TCP_TIMEOUTS)
        {
            Serial.println(F("[SIM800Client] ⚠⚠ چند CIPSEND timeout پشت‌سرهم - hard reset فوری درخواست شد"));
            _needsHardReset = true;
        }
    }

    return sendOk ? size : 0;
}

// ─────────────────────────────────────────────
// connect()
// ─────────────────────────────────────────────

int SIM800Client::connect(IPAddress ip, uint16_t port)
{
    char host[16];
    snprintf(host, sizeof(host), "%d.%d.%d.%d",
             ip[0], ip[1], ip[2], ip[3]);
    return connect(host, port);
}

int SIM800Client::connect(const char *host, uint16_t port)
{
    SIM800_SetTcpConnectCallback(_sim, tcpConnectCallback);
    SIM800_SetTcpDataCallback(_sim, tcpDataCallback);

    // تضمین عدم تداخل با تسک پیامک
    waitForSmsIdle();

    Serial.print(F("[SIM800Client] → "));
    Serial.print(host);
    Serial.print(F(":"));
    Serial.println(port);

    _rxLen = 0;
    _rxPos = 0;
    _connected = false;
    _sim->tcp_reading_binary = false;
    _sim->tcp_binary_len = 0;

    Serial.println(F("[SIM800Client] Closing previous TCP socket"));
    SIM800_SendCommand(_sim, "AT+CIPCLOSE", "CLOSE OK", 3000);
    unsigned long t = millis();
    while (millis() - t < 3000)
    {
        SIM800_Process(_sim);
        esp_task_wdt_reset();
        delay(10);
    }

    delay(300);
    _sim->tcp_state = TCP_IDLE;
    _sim->cmd_state = CMD_IDLE;
    _sim->cmd_queue_size = 0;
    _sim->cmd_queue_head = 0;
    _sim->cmd_queue_tail = 0;
    _connected = false;

    if (!SIM800_GprsIsConnected(_sim))
    {
        Serial.println(F("[SIM800Client] GPRS پایینه، اول GPRS رو دوباره وصل می‌کنم..."));
        SIM800_Result_t gres = SIM800_GprsConnect(_sim, GPRS_APN, GPRS_USER, GPRS_PASS);

        if (gres == SIM800_OK)
        {
            unsigned long gt = millis();
            while (!SIM800_GprsIsConnected(_sim) && (millis() - gt < 60000))
            {
                SIM800_Process(_sim);
                esp_task_wdt_reset();
                delay(100);
            }
        }

        if (!SIM800_GprsIsConnected(_sim))
        {
            Serial.println(F("[SIM800Client] اتصال GPRS ناموفق بود، TCP connect لغو شد"));
            return 0;
        }
    }

    Serial.println(F("[SIM800Client] Setting CIPHEAD=1"));
    if (SIM800_SendCommand(_sim, "AT+CIPHEAD=1", "OK", 3000) == SIM800_OK)
    {
        unsigned long t2 = millis();
        while (_sim->cmd_state == CMD_WAIT_RESPONSE && millis() - t2 < 4000)
        {
            SIM800_Process(_sim);
            esp_task_wdt_reset();
            delay(10);
        }
    }

    delay(200);

    bool fastRejection = false;
    int result = attemptCipStart(host, port, &fastRejection);

    if (result == 1)
        return 1;

    if (fastRejection)
    {
        Serial.println(F("[SIM800Client] ⚠ CIPSTART فوری رد شد - چک وضعیت IP stack و ترمیم احتمالی PDP"));
        if (recoverDeadPdpContext())
        {
            Serial.println(F("[SIM800Client] یک تلاش دیگر برای CIPSTART بعد از ترمیم..."));
            delay(300);
            result = attemptCipStart(host, port, nullptr);
            if (result == 1)
                return 1;
        }
    }

    return 0;
}

// ─────────────────────────────────────────────
// read / available / peek
// ─────────────────────────────────────────────

int SIM800Client::available()
{
    SIM800_Process(_sim);
    return (int)(_rxLen - _rxPos);
}

int SIM800Client::read()
{
    if (_rxPos >= _rxLen)
        return -1;

    uint8_t b = _rxBuffer[_rxPos++];
    if (_rxPos >= _rxLen)
    {
        _rxPos = 0;
        _rxLen = 0;
    }
    return (int)b;
}

int SIM800Client::read(uint8_t *buf, size_t size)
{
    int avail = available();
    if (avail <= 0)
        return 0;

    size_t toRead = (size < (size_t)avail) ? size : (size_t)avail;
    memcpy(buf, _rxBuffer + _rxPos, toRead);
    _rxPos += toRead;

    if (_rxPos >= _rxLen)
    {
        _rxPos = 0;
        _rxLen = 0;
    }
    return (int)toRead;
}

int SIM800Client::peek()
{
    if (_rxPos >= _rxLen)
        return -1;
    return (int)_rxBuffer[_rxPos];
}

void SIM800Client::flush()
{
    unsigned long t = millis();
    while ((_sim->tcp_state == TCP_SENDING ||
            _sim->tcp_state == TCP_WAIT_SEND_PROMPT) &&
           millis() - t < 5000)
    {
        SIM800_Process(_sim);
        esp_task_wdt_reset();
        delay(10);
    }
}

// ─────────────────────────────────────────────
// stop / connected
// ─────────────────────────────────────────────

void SIM800Client::stop()
{
    Serial.println(F("[SIM800Client] Stop"));

    if (_connected || SIM800_TcpIsConnected(_sim))
    {
        SIM800_TcpClose(_sim);

        unsigned long t = millis();
        while (SIM800_TcpIsConnected(_sim) && millis() - t < 3000)
        {
            SIM800_Process(_sim);
            esp_task_wdt_reset();
            delay(10);
        }
    }

    _connected = false;
    _rxPos = 0;
    _rxLen = 0;
}

uint8_t SIM800Client::connected()
{
    SIM800_Process(_sim);

    bool tcpOk = (_sim->tcp_state == TCP_CONNECTED);

    if (!tcpOk && _connected)
    {
        _connected = false;
        Serial.println(F("[SIM800Client] Conn lost"));
    }

    return tcpOk ? 1 : 0;
}

SIM800Client::operator bool()
{
    return connected() == 1;
}

void SIM800Client::setTimeout(unsigned long timeout)
{
    _timeout = timeout;
}

void SIM800Client::loop()
{
    SIM800_Process(_sim);

    if (_connected && _sim->tcp_state != TCP_CONNECTED)
    {
        _connected = false;
        Serial.println(F("[SIM800Client] Dropped"));
    }
}