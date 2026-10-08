#include "SIM800Manager.h"
#include <esp_task_wdt.h>

static const uint32_t SOFT_REINIT_TIMEOUT_MS = 15000;
static const uint32_t HARD_RESET_INIT_TIMEOUT_MS = 20000;

SIM800Manager *SIM800Manager::_instance = nullptr;

SIM800Manager::SIM800Manager(HardwareSerial *serial)
{
    _serial = serial;
    _gprsReady = false;
    _initialized = false;
    _processingPaused = false;
    _lastProcessTime = 0;
    _lastStatusCheck = 0;
    _instance = this;
}

void SIM800Manager::initCallback(bool success)
{
    if (_instance)
    {
        _instance->_initialized = success;
        Serial.print(F("[SIM800] Init: "));
        Serial.println(success ? "SUCCESS" : "FAILED");
    }
}

void SIM800Manager::gprsCallback(bool connected)
{
    if (_instance)
    {
        _instance->_gprsReady = connected;
        Serial.print(F("[SIM800] GPRS: "));
        Serial.println(connected ? "CONNECTED" : "DISCONNECTED");
    }
}

void SIM800Manager::begin(HardwareSerial *serial)
{
    _serial = serial;
    _serial->begin(9600, SERIAL_8N1, SIM800_RX, SIM800_TX);

    Serial.println(F("[SIM800] Initializing..."));

    pinMode(SIM800_RST, OUTPUT);
    digitalWrite(SIM800_RST, LOW);
    delay(300);
    digitalWrite(SIM800_RST, HIGH);
    delay(3000);

    SIM800_Init(&_sim, _serial);
    SIM800_SetInitCallback(&_sim, initCallback);
    SIM800_SetGprsCallback(&_sim, gprsCallback);

    Serial.println(F("[SIM800] Waiting for module..."));
}

void SIM800Manager::loop()
{
    if (_processingPaused)
        return;

    unsigned long now = millis();

    if (now - _lastProcessTime >= SIM800_PROCESS_INTERVAL)
    {
        SIM800_Process(&_sim);
        _lastProcessTime = now;
    }

    if (now - _lastStatusCheck >= 10000)
    {
        if (_initialized && !SIM800_IsNetworkRegistered(&_sim))
        {
            Serial.println(F("[SIM800] ⚠ Network lost!"));
        }
        _lastStatusCheck = now;
    }
}

bool SIM800Manager::sendRawAT(const char *cmd, char *response, size_t responseSize, unsigned long timeoutMs)
{
    if (!_serial || !response || responseSize == 0)
        return false;

    while (_serial->available())
        _serial->read();

    _serial->print(cmd);
    _serial->print("\r\n");

    size_t idx = 0;
    response[0] = '\0';
    unsigned long start = millis();
    bool gotOk = false;

    while (millis() - start < timeoutMs)
    {
        esp_task_wdt_reset();

        while (_serial->available() && idx < responseSize - 1)
        {
            response[idx++] = (char)_serial->read();
        }
        response[idx] = '\0';

        if (strstr(response, "OK") || strstr(response, "ERROR") ||
            strstr(response, "NO CARRIER") || strstr(response, "BUSY") ||
            strstr(response, "NO ANSWER"))
        {
            gotOk = (strstr(response, "OK") != nullptr);
            break;
        }
        delay(20);
    }

    return gotOk;
}

bool SIM800Manager::isReady()
{
    return _initialized && SIM800_IsReady(&_sim);
}

bool SIM800Manager::isGprsConnected()
{
    return _gprsReady && SIM800_GprsIsConnected(&_sim);
}

int SIM800Manager::getSignalQuality()
{
    return SIM800_GetSignalStrength(&_sim);
}

const char *SIM800Manager::getIP()
{
    return SIM800_GprsGetIP(&_sim);
}

bool SIM800Manager::connectGPRS(const char *apn, const char *user, const char *pass)
{
    Serial.println(F("[SIM800] Connecting to GPRS..."));

    SIM800_Result_t result = SIM800_GprsConnect(&_sim, apn, user, pass);

    if (result != SIM800_OK)
    {
        Serial.print(F("[SIM800] GPRS connect failed: "));
        Serial.println(result);
        return false;
    }

    unsigned long start = millis();
    while (!_gprsReady && (millis() - start < 60000))
    {
        SIM800_Process(&_sim);
        esp_task_wdt_reset();
        delay(100);
    }

    if (_gprsReady)
    {
        Serial.print(F("[SIM800] ✓ GPRS Connected | IP: "));
        Serial.println(getIP());
        return true;
    }

    Serial.println(F("[SIM800] ✗ GPRS timeout"));
    return false;
}

// ════════════════════════════════════════════════════════════════
// ═══ SMS - مسیر عادی (خودکار تشخیص فارسی/یونیکد) ⚠️ اصلاح‌شده ═══
// ════════════════════════════════════════════════════════════════

bool SIM800Manager::containsNonAscii(const char *text)
{
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
    {
        if (*p >= 0x80)
            return true;
    }
    return false;
}

bool SIM800Manager::sendSMS(const char *number, const char *text)
{
    // ⚠️ اگه متن شامل کاراکتر غیر-ASCII باشه (فارسی/عربی/هر یونیکد دیگه)،
    // مسیر GSM 7-bit معمولی کاراکترهای عجیب‌غریب تولید می‌کنه؛ خودکار می‌ریم
    // مسیر UCS2. متن‌های خالص انگلیسی/عددی (مثل کد OTP) مثل قبل سریع می‌رن.
    if (containsNonAscii(text))
        return sendUnicodeSMS(number, text);

    return SIM800_SendSMS(&_sim, number, text) == SIM800_OK;
}

// ════════════════════════════════════════════════════════════════
// ═══ SMS یونیکد (UCS2) - برای فارسی/عربی/هر متن غیر-ASCII ⚠️ جدید ═══
// ════════════════════════════════════════════════════════════════

// تبدیل UTF-8 (چیزی که source فایل‌های فارسی این پروژه باهاش انکود شدن) به
// رشته‌ی هگز UCS2 (۴ رقم هگز به‌ازای هر کاراکتر، بزرگ‌حروف) که AT+CMGS در
// حالت CSCS="UCS2" انتظارش رو داره. فارسی همیشه داخل BMP هست (U+0600-U+06FF)
// پس نیازی به surrogate pair نیست.
bool SIM800Manager::utf8ToUcs2Hex(const char *utf8, char *outHex, size_t outSize)
{
    if (!utf8 || !outHex || outSize < 5)
        return false;

    size_t outIdx = 0;
    const unsigned char *p = (const unsigned char *)utf8;

    while (*p)
    {
        uint32_t cp = 0;
        uint8_t extraBytes = 0;

        if ((*p & 0x80) == 0x00)
        {
            cp = *p;
            extraBytes = 0;
        }
        else if ((*p & 0xE0) == 0xC0)
        {
            cp = *p & 0x1F;
            extraBytes = 1;
        }
        else if ((*p & 0xF0) == 0xE0)
        {
            cp = *p & 0x0F;
            extraBytes = 2;
        }
        else if ((*p & 0xF8) == 0xF0)
        {
            // کاراکتر خارج از BMP (پلن‌های بالاتر - ایموجی و امثالش) - UCS2
            // پایه پشتیبانی نمی‌کنه، رد می‌شه (فارسی رو تحت تاثیر قرار نمی‌ده)
            p += 4;
            continue;
        }
        else
        {
            p++; // بایت نامعتبر UTF-8 - رد شو
            continue;
        }

        p++;
        bool validSeq = true;
        for (uint8_t i = 0; i < extraBytes; i++)
        {
            if ((*p & 0xC0) != 0x80)
            {
                validSeq = false;
                break;
            }
            cp = (cp << 6) | (*p & 0x3F);
            p++;
        }
        if (!validSeq)
            continue;

        if (cp > 0xFFFF)
            continue;

        if (outIdx + 4 >= outSize - 1)
            break; // جا کافی نیست - به‌جای overflow، همینجا کامل (بدون بریدن کاراکتر) قطع می‌کنیم

        snprintf(outHex + outIdx, 5, "%04X", (unsigned int)cp);
        outIdx += 4;
    }

    outHex[outIdx] = '\0';
    return outIdx > 0;
}

bool SIM800Manager::waitForChar(char target, unsigned long timeoutMs)
{
    unsigned long start = millis();
    while (millis() - start < timeoutMs)
    {
        esp_task_wdt_reset();
        while (_serial->available())
        {
            if ((char)_serial->read() == target)
                return true;
        }
        delay(10);
    }
    return false;
}

bool SIM800Manager::sendUnicodeSMS(const char *number, const char *utf8Text)
{
    if (!_serial || !number || !utf8Text)
        return false;

    char hexNumber[96];
    char hexText[512]; // ~۱۲۷ کاراکتر یونیکد - بیشتر از ظرفیت تک‌پیامکی معمولاً بریده می‌شه، برای هشدارهای کوتاه کافیه

    if (!utf8ToUcs2Hex(number, hexNumber, sizeof(hexNumber)))
    {
        Serial.println(F("[SIM800] ⚠ تبدیل شماره به UCS2 ناموفق بود"));
        return false;
    }
    if (!utf8ToUcs2Hex(utf8Text, hexText, sizeof(hexText)))
    {
        Serial.println(F("[SIM800] ⚠ تبدیل متن پیامک به UCS2 ناموفق بود"));
        return false;
    }

    Serial.println(F("[SIM800] ⚠ ارسال پیامک یونیکد (UCS2) شروع شد..."));
    pauseProcessing(true); // تا پایان کل سکانس، SIM800_Process عادی متوقفه (مثل فلوی تماس)

    char resp[64];
    bool setupOk = true;

    setupOk &= sendRawAT("AT+CMGF=1", resp, sizeof(resp), 3000);
    setupOk &= sendRawAT("AT+CSCS=\"UCS2\"", resp, sizeof(resp), 3000);
    setupOk &= sendRawAT("AT+CSMP=17,167,0,8", resp, sizeof(resp), 3000);

    if (!setupOk)
    {
        Serial.println(F("[SIM800] ⚠ مقداردهی اولیه‌ی حالت UCS2 ناموفق بود"));
        sendRawAT("AT+CSCS=\"GSM\"", resp, sizeof(resp), 2000); // برگردوندن charset حتی موقع خطا
        pauseProcessing(false);
        return false;
    }

    while (_serial->available())
        _serial->read();

    char cmgsCmd[112];
    snprintf(cmgsCmd, sizeof(cmgsCmd), "AT+CMGS=\"%s\"\r", hexNumber);
    _serial->print(cmgsCmd);

    if (!waitForChar('>', 5000))
    {
        Serial.println(F("[SIM800] ⚠ پرامپت '>' برای ارسال متن پیامک دریافت نشد"));
        _serial->write((char)0x1B); // ESC برای لغو دستور CMGS نیمه‌کاره
        delay(300);
        sendRawAT("AT+CSCS=\"GSM\"", resp, sizeof(resp), 2000);
        pauseProcessing(false);
        return false;
    }

    delay(100);
    _serial->print(hexText);
    _serial->write((char)0x1A); // Ctrl+Z = ارسال نهایی پیامک

    char finalResp[96];
    size_t idx = 0;
    finalResp[0] = '\0';
    bool sent = false;
    unsigned long start = millis();

    while (millis() - start < 15000)
    {
        esp_task_wdt_reset();
        while (_serial->available() && idx < sizeof(finalResp) - 1)
        {
            finalResp[idx++] = (char)_serial->read();
        }
        finalResp[idx] = '\0';

        if (strstr(finalResp, "+CMGS:") || strstr(finalResp, "OK"))
        {
            sent = true;
            break;
        }
        if (strstr(finalResp, "ERROR"))
        {
            sent = false;
            break;
        }
        delay(20);
    }

    // ⚠️ خیلی مهم: برگردوندن charset به GSM - وگرنه بقیه‌ی توابع کتابخونه‌ی
    // SIM800_Arduino (که رشته‌ها رو GSM/IRA فرض می‌کنن) بعد از این خراب می‌شن.
    // اگه charset پیش‌فرض کتابخونه‌ت چیز دیگه‌ایه (نه "GSM")، فقط همین یک خط رو عوض کن.
    sendRawAT("AT+CSCS=\"GSM\"", resp, sizeof(resp), 2000);

    pauseProcessing(false);

    Serial.println(sent ? F("[SIM800] ✓ پیامک یونیکد ارسال شد")
                        : F("[SIM800] ✗ ارسال پیامک یونیکد ناموفق بود"));
    return sent;
}

bool SIM800Manager::isSmsBusy()
{
    return _sim.sms_state != SMS_IDLE;
}

// ════════════════════════════════════════════════════════════════
// ═══ نردبان Escalation ═══
// ════════════════════════════════════════════════════════════════

bool SIM800Manager::isModemResponding()
{
    char resp[48];
    bool ok = sendRawAT("AT", resp, sizeof(resp), 2000);
    Serial.print(F("[SIM800] چک پاسخگویی مودم (AT ساده): "));
    Serial.println(ok ? F("✓ زنده است") : F("✗ جواب نمی‌ده"));
    return ok;
}
bool SIM800Manager::isSimCardPresent()
{
    char resp[80];
    // بدون سیمکارت، ماژول معمولاً "+CME ERROR: 10" برمی‌گردونه (بدون OK).
    // با سیمکارت آماده، "+CPIN: READY" و بعدش OK می‌آد.
    bool ok = sendRawAT("AT+CPIN?", resp, sizeof(resp), 5000);
    bool present = ok && (strstr(resp, "READY") != nullptr);

    Serial.print(F("[SIM800] چک سیمکارت (AT+CPIN?): "));
    Serial.println(present ? F("✓ موجود و آماده") : F("✗ سیمکارت نیست یا قفل است (PIN)"));

    return present;
}
// ⚠️ جدید - تشخیص قطعی خاموش GPRS. چک ارزون اول: CREG (از URC، بدون AT اضافه).
// اگه شبکه هست ولی شک داریم، با AT+CGATT? واقعاً attach بودن رو تایید می‌کنیم.
// اگه هر کدوم رد بشه، gprs_state رو دستی به GPRS_IDLE برمی‌گردونیم تا چرخه‌ی
// بعدی manageConnections() واقعاً از صفر یک context جدید بسازه (نه اینکه فکر کنه
// همه‌چی رواله و هیچ‌کاری نکنه).
bool SIM800Manager::verifyGprsAlive()
{
    if (!_gprsReady || _sim.gprs_state != GPRS_CONNECTED)
        return false;

    if (!SIM800_IsNetworkRegistered(&_sim))
    {
        Serial.println(F("[SIM800] ⚠ CREG نشون میده شبکه از دست رفته - GPRS واقعاً قطعه"));
        _gprsReady = false;
        _sim.gprs_state = GPRS_IDLE;
        return false;
    }

    char resp[64];
    sendRawAT("AT+CGATT?", resp, sizeof(resp), 5000);

    bool attached = (strstr(resp, "+CGATT: 1") != nullptr);

    if (!attached)
    {
        Serial.print(F("[SIM800] ⚠ CGATT نشون داد GPRS detach شده ("));
        Serial.print(resp);
        Serial.println(F(") - وادار به reconnect کامل می‌شه"));

        _gprsReady = false;
        _sim.gprs_state = GPRS_IDLE;
    }

    return attached;
}
bool SIM800Manager::softReinit()
{
    Serial.println(F("[SIM800] ⚠ Soft Reinit (ForceReinit) در حال اجرا..."));

    SIM800_ForceReinit(&_sim);

    _gprsReady = false;
    _initialized = false;

    unsigned long start = millis();
    while (!SIM800_IsReady(&_sim) && (millis() - start < SOFT_REINIT_TIMEOUT_MS))
    {
        SIM800_Process(&_sim);
        esp_task_wdt_reset();
        delay(100);
    }

    _initialized = SIM800_IsReady(&_sim);
    Serial.println(_initialized ? F("[SIM800] ✓ Soft Reinit موفق بود")
                                : F("[SIM800] ✗ Soft Reinit ناموفق بود"));
    return _initialized;
}

bool SIM800Manager::hardReset()
{
    Serial.println(F("[SIM800] ⚠⚠ Hard Reset (toggle فیزیکی پین RST) در حال اجرا..."));

    _gprsReady = false;
    _initialized = false;

    digitalWrite(SIM800_RST, LOW);
    delay(300);
    esp_task_wdt_reset();
    digitalWrite(SIM800_RST, HIGH);
    delay(3000);
    esp_task_wdt_reset();

    SIM800_Init(&_sim, _serial);
    SIM800_SetInitCallback(&_sim, initCallback);
    SIM800_SetGprsCallback(&_sim, gprsCallback);

    unsigned long start = millis();
    while (!SIM800_IsReady(&_sim) && (millis() - start < HARD_RESET_INIT_TIMEOUT_MS))
    {
        SIM800_Process(&_sim);
        esp_task_wdt_reset();
        delay(100);
    }

    _initialized = SIM800_IsReady(&_sim);
    Serial.println(_initialized ? F("[SIM800] ✓ Hard Reset موفق بود - مودم دوباره Ready شد")
                                : F("[SIM800] ✗ Hard Reset ناموفق بود"));
    return _initialized;
}

void SIM800Manager::printStatus()
{
    Serial.println(F("\n╔═══════════════════════════════════╗"));
    Serial.println(F("║       SIM800 STATUS               ║"));
    Serial.println(F("╠═══════════════════════════════════╣"));

    Serial.print(F("║ Ready:      "));
    Serial.println(isReady() ? "✓ YES     " : "✗ NO      ");

    Serial.print(F("║ Network:    "));
    Serial.println(SIM800_IsNetworkRegistered(&_sim) ? "✓ REG     " : "✗ UNREG   ");

    Serial.print(F("║ Signal:     "));
    Serial.print(getSignalQuality());
    Serial.println(F("/31       "));

    Serial.print(F("║ GPRS:       "));
    Serial.println(isGprsConnected() ? "✓ ON      " : "✗ OFF     ");

    if (isGprsConnected())
    {
        Serial.print(F("║ IP:         "));
        Serial.println(getIP());
    }

    Serial.println(F("╚═══════════════════════════════════╝\n"));
}