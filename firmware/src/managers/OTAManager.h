#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

// ════════════════════════════════════════════════════════════════
// ═══ OTA Manager - Production-Grade OTA over GSM (Segmented) ═══
// ════════════════════════════════════════════════════════════════
//
// Architecture:
//   • MQTT: فقط برای اعلام آپدیت + گزارش وضعیت (سر مرز هر بخش)
//   • HTTP: دانلود فریمور تکه‌ای (5 بخش، هرکدوم Range جداگانه) روی SIM800Client خام
//   • NVS:  پرسیست offset برای بازیابی بعد از کرش/ریبوت
//   • SHA256: چک یکپارچگی قبل از سوییچ پارتیشن
//   • Dual OTA partitions (OTA_0 / OTA_1) با safety rollback
//
// State Machine:
//   IDLE → REQUESTED → PREPARING → CONNECTING_HTTP → DOWNLOADING
//                                        ↑                  ↓ (بخش کامل شد)
//                                        └── SEGMENT_COOLDOWN
//                                                          ↓ (همه بخش‌ها کامل)
//                                                       VERIFYING
//                                                          ↓
//                                                COMPLETING / FAILED
//                                                          ↓
//                                                     REBOOTING
//
// ════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <nvs.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include "config.h"

// ── Forward declarations ──
class SIM800Client;
class SIM800Manager;
class MQTTManager;

// ────────────────────────────────────────────────────────────────
// OTA State Machine States
// ────────────────────────────────────────────────────────────────
enum class OtaState : uint8_t
{
    IDLE = 0,         // فعالیتی نیست
    REQUESTED,        // دستور دریافت شد، منتظر شروع
    PREPARING,        // مقداردهی Update + SHA256 + قطع MQTT
    CONNECTING_HTTP,  // باز کردن اتصال HTTP با Range مربوط به بخش فعلی
    DOWNLOADING,      // خوندن استریم بخش فعلی و نوشتن روی فلش
    SEGMENT_COOLDOWN, // ⚠️ جدید - مکث کوتاه بین بخش‌ها، فرصت نفس کشیدن مودم
    VERIFYING,        // مقایسه SHA256 محاسبه‌شده با مقدار مورد انتظار
    COMPLETING,       // نهایی‌سازی Update و ست کردن boot partition
    REBOOTING,        // در آستانه‌ی ESP.restart()
    FAILED_STATE      // OTA شکست خورد، خطا پابلیش می‌شه، برمی‌گرده IDLE
};

// ────────────────────────────────────────────────────────────────
// OTA Metadata — در NVS پرسیست می‌شه برای بازیابی بعد از کرش
// ────────────────────────────────────────────────────────────────
struct OtaMetadata
{
    bool active;        // true = OTA در حال انجام (برای تشخیص بعد از ریبوت)
    uint32_t offset;    // آفست فعلی دانلود/نوشتن (بایت)
    uint32_t totalSize; // اندازه کل فریمور (بایت)
    char sha256[65];    // SHA256 هگز مورد انتظار (64 کاراکتر + null)
    char version[32];   // نسخه‌ی هدف
    char urlPath[128];  // مسیر URL فریمور روی سرور HTTP
};

// ────────────────────────────────────────────────────────────────
// OTAManager Class
// ────────────────────────────────────────────────────────────────
class OTAManager
{
public:
    OTAManager(SIM800Client *client, SIM800Manager *simMgr, MQTTManager *mqttMgr);
    ~OTAManager();

    /// مقداردهی NVS و بارگذاری متادیتای قبلی.
    void begin();

    /// state machine اصلی — از loop اصلی صدا زده می‌شه.
    void loop();

    /// پارس دستور OTA از JSON دریافتی MQTT.
    bool handleOtaCommand(JsonDocument &doc);

    /// ⚠️ جدید - چک فعال آخرین نسخه از سرور (device-initiated).
    /// اگه نسخه جدیدتری موجود باشه، خودکار متادیتا رو ست و OTA رو شروع می‌کنه
    /// و به کاربر پیامک اطلاع می‌ده. باید periodic از main.cpp صدا زده بشه.
    bool checkForUpdate();

    /// true وقتی OTA فعالانه در حال اجراست (loop اصلی باید عملیات عادی رو رد کنه).
    bool isInProgress() const;

    /// درصد پیشرفت 0–100.
    uint8_t getProgress() const;

    /// وضعیت فعلی.
    OtaState getState() const { return _state; }

    /// رشته‌ی قابل‌خوندن وضعیت فعلی.
    const char *getStateString() const;

    /// لغو اجباری — آپدیت نیمه‌کاره رو دور می‌ریزه.
    void abort();

    /// چک بعد از بوت: اگه NVS نشون بده OTA قطع شده، دوباره شروعش کن.
    void checkPendingOta();

    /// آخرین پیام خطا.
    const char *getLastError() const { return _lastError; }

private:
    // ── Dependencies ──
    SIM800Client *_client;
    SIM800Manager *_simMgr;
    MQTTManager *_mqttMgr;

    // ── State ──
    OtaState _state;
    OtaMetadata _meta;

    // ── NVS ──
    nvs_handle_t _nvs;

    // ── Raw HTTP over SIM800Client ──
    bool _httpConnected;
    int _httpRespCode;
    uint32_t _contentLength;
    bool _serverSupportsRange;

    // ── SHA256 (incremental) ── ⚠️ تبدیل به pointer برای جلوگیری از Stack Overflow
    mbedtls_sha256_context *_shaCtx; // ✅ اصلاح شده
    bool _shaInitialized;

    // ── ⚠️ جدید - دانلود تکه‌ای (Segmented) ──
    uint8_t _segmentCount;      // تعداد کل بخش‌ها (پیش‌فرض از OTA_SEGMENT_COUNT)
    uint32_t _segmentSize;      // اندازه هر بخش (بایت)
    uint8_t _currentSegment;    // ایندکس بخش فعلی (0-based)
    uint32_t _segmentEndOffset; // آفست پایان بخش فعلی (exclusive)
    uint8_t _segmentRetries;    // شمارنده‌ی retry برای بخش فعلی

    // ── ⚠️ جدید - پیامک پیشرفت (هر milestone فقط یک‌بار) ──
    int8_t _lastSmsProgressMilestone; // -1 = هنوز چیزی ارسال نشده

    // ── Retry tracking ──
    uint8_t _globalRetries;
    unsigned long _retryTimer;
    unsigned long _stateEnterTime;
    unsigned long _delayResumeMs; // ⚠️ Non-blocking delay timer (0 = inactive)

    // ── Buffer ──
    uint8_t *_chunkBuf;
    static const int CHUNK_BUF_SIZE = OTA_CHUNK_SIZE;

    // ── HTTP response line buffer ──
    static const int HTTP_LINE_BUF_SIZE = 256;
    char _httpLineBuf[256];

    // ── Error ──
    char _lastError[128];

    // ── Internal: NVS ──
    void loadMetadata();
    void saveMetadata();
    void saveOffset();
    void clearMetadata();

    // ── Internal: State machine steps ──
    void enterState(OtaState newState);
    bool doPrepare();
    bool doConnectHttp();
    bool doDownload();
    void doDisconnectHttp();
    bool doVerify();
    bool doComplete();

    // ── Internal: Segmented download ──
    void computeSegmentBounds();
    void advanceSegment();
    void reportSegmentProgress();
    void maybeSendProgressSms(uint8_t progress);

    // ── Internal: Raw HTTP client ──
    bool sendHttpGet();
    bool parseHttpStatusLine();
    bool parseHttpHeaders();
    int readHttpBodyChunk(uint8_t *buf, int maxLen);
    String buildUrl();

    // ── Internal: MQTT ──
    void publishStatus(const char *status, const char *error = nullptr);
    bool ensureMqttConnected();
    void ensureMqttDisconnected();

    // ── Internal: Utilities ──
    bool checkPrerequisites();
    void failOta(const char *reason);
    static String sha256ToHex(const uint8_t *hash, size_t len);
    void feedWatchdog();
    void sha256Init();
    void sha256Update(const uint8_t *data, size_t len);
    String sha256Final();
};

#endif // OTA_MANAGER_H