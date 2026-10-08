#include "OTAManager.h"
#include "SIM800Manager.h"
#include "MQTTManager.h"
#include "SIM800Client.h"
#include <esp_task_wdt.h>

// ════════════════════════════════════════════════════════════════
// ═══ NVS Keys ═══
// ════════════════════════════════════════════════════════════════
static const char *NVS_NAMESPACE = "ota_data";
static const char *NVS_KEY_META = "meta";
static const char *NVS_KEY_OFFSET = "offset";

// ════════════════════════════════════════════════════════════════
// ═══ Constructor / Destructor ═══
// ════════════════════════════════════════════════════════════════

OTAManager::OTAManager(SIM800Client *client, SIM800Manager *simMgr, MQTTManager *mqttMgr)
    : _client(client), _simMgr(simMgr), _mqttMgr(mqttMgr)
{
    _state = OtaState::IDLE;
    _httpConnected = false;
    _httpRespCode = 0;
    _contentLength = 0;
    _serverSupportsRange = false;
    _shaCtx = nullptr;
    _shaInitialized = false;

    _segmentCount = OTA_SEGMENT_COUNT;
    _segmentSize = 0;
    _currentSegment = 0;
    _segmentEndOffset = 0;
    _segmentRetries = 0;
    _lastSmsProgressMilestone = -1;

    _globalRetries = 0;
    _retryTimer = 0;
    _stateEnterTime = 0;
    _delayResumeMs = 0;
    _chunkBuf = nullptr;
    _lastError[0] = '\0';
    _httpLineBuf[0] = '\0';
    memset(&_meta, 0, sizeof(_meta));
    _nvs = 0;
}

OTAManager::~OTAManager()
{
    if (_chunkBuf)
    {
        free(_chunkBuf);
        _chunkBuf = nullptr;
    }
    if (_nvs)
    {
        nvs_close(_nvs);
        _nvs = 0;
    }
    if (_shaInitialized && _shaCtx)
    {
        mbedtls_sha256_free(_shaCtx);
        free(_shaCtx);
        _shaCtx = nullptr;
        _shaInitialized = false;
    }
}

// ════════════════════════════════════════════════════════════════
// ═══ begin() — Initialize NVS and load metadata ═══
// ════════════════════════════════════════════════════════════════

void OTAManager::begin()
{
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &_nvs);
    if (err != ESP_OK)
    {
        Serial.println(F("[OTA] ✗ Failed to open NVS namespace!"));
        return;
    }

    loadMetadata();

    _chunkBuf = (uint8_t *)malloc(CHUNK_BUF_SIZE);
    if (!_chunkBuf)
    {
        Serial.println(F("[OTA] ✗ Failed to allocate chunk buffer!"));
    }

    Serial.println(F("[OTA] Initialized"));
    if (_meta.active)
    {
        Serial.print(F("[OTA] ⚠ Pending OTA detected: offset="));
        Serial.print(_meta.offset);
        Serial.print(F("/"));
        Serial.print(_meta.totalSize);
        Serial.print(F(" version="));
        Serial.println(_meta.version);
    }
}

// ════════════════════════════════════════════════════════════════
// ═══ NVS Operations ═══
// ════════════════════════════════════════════════════════════════

void OTAManager::loadMetadata()
{
    size_t requiredSize = sizeof(OtaMetadata);
    esp_err_t err = nvs_get_blob(_nvs, NVS_KEY_META, &_meta, &requiredSize);
    if (err != ESP_OK || requiredSize != sizeof(OtaMetadata))
    {
        memset(&_meta, 0, sizeof(_meta));
    }
}

void OTAManager::saveMetadata()
{
    esp_err_t err = nvs_set_blob(_nvs, NVS_KEY_META, &_meta, sizeof(OtaMetadata));
    if (err != ESP_OK)
    {
        Serial.print(F("[OTA] ✗ NVS save meta failed: "));
        Serial.println(esp_err_to_name(err));
    }
    nvs_commit(_nvs);
}

void OTAManager::saveOffset()
{
    nvs_set_u32(_nvs, NVS_KEY_OFFSET, _meta.offset);
    nvs_commit(_nvs);
}

void OTAManager::clearMetadata()
{
    memset(&_meta, 0, sizeof(_meta));
    nvs_erase_key(_nvs, NVS_KEY_META);
    nvs_erase_key(_nvs, NVS_KEY_OFFSET);
    nvs_commit(_nvs);
}

// ════════════════════════════════════════════════════════════════
// ═══ checkPendingOta() — Called after boot to resume ═══
// ════════════════════════════════════════════════════════════════

void OTAManager::checkPendingOta()
{
    if (_meta.active && _meta.totalSize > 0)
    {
        Serial.println(F("[OTA] Resuming interrupted OTA..."));
        _state = OtaState::REQUESTED;
    }
}

// ════════════════════════════════════════════════════════════════
// ═══ handleOtaCommand() — Parse MQTT OTA command ═══
// ════════════════════════════════════════════════════════════════

bool OTAManager::handleOtaCommand(JsonDocument &doc)
{
    const char *cmd = doc["cmd"];
    if (!cmd)
        return false;

    if (strcmp(cmd, "update") != 0)
        return false;

    if (_state != OtaState::IDLE && _state != OtaState::FAILED_STATE)
    {
        Serial.println(F("[OTA] Already in progress, ignoring new command"));
        publishStatus("failed", "OTA already in progress");
        return false;
    }

    const char *version = doc["version"] | "";
    const char *url = doc["url"] | "";
    uint32_t size = doc["size"] | 0;
    const char *sha256 = doc["sha256"] | "";

    if (strlen(version) == 0 || strlen(url) == 0 || size == 0 || strlen(sha256) != 64)
    {
        Serial.println(F("[OTA] ✗ Invalid OTA command: missing fields"));
        publishStatus("failed", "Invalid OTA command metadata");
        return false;
    }

    if (strcmp(version, FIRMWARE_VERSION) == 0)
    {
        Serial.print(F("[OTA] Already running version "));
        Serial.println(version);
        publishStatus("failed", "Already running this version");
        return false;
    }

    memset(&_meta, 0, sizeof(_meta));
    _meta.active = true;
    _meta.offset = 0;
    _meta.totalSize = size;
    strncpy(_meta.sha256, sha256, sizeof(_meta.sha256) - 1);
    strncpy(_meta.version, version, sizeof(_meta.version) - 1);
    strncpy(_meta.urlPath, url, sizeof(_meta.urlPath) - 1);

    saveMetadata();
    saveOffset();

    Serial.print(F("[OTA] Update command received: v"));
    Serial.print(_meta.version);
    Serial.print(F(" size="));
    Serial.print(_meta.totalSize);
    Serial.print(F(" sha256="));
    Serial.println(_meta.sha256);

    _globalRetries = 0;
    _lastSmsProgressMilestone = -1;
    enterState(OtaState::REQUESTED);
    return true;
}

// ════════════════════════════════════════════════════════════════
// ═══ checkForUpdate() — ⚠️ جدید - چک فعال آخرین نسخه (device-initiated) ═══
// ════════════════════════════════════════════════════════════════

bool OTAManager::checkForUpdate()
{
    if (isInProgress())
    {
        Serial.println(F("[OTA] چک آپدیت رد شد - OTA در حال اجراست"));
        return false;
    }

    if (!_simMgr->isGprsConnected())
    {
        Serial.println(F("[OTA] چک آپدیت رد شد - GPRS وصل نیست"));
        return false;
    }

    Serial.println(F("[OTA] در حال چک کردن آخرین نسخه از سرور..."));

    ensureMqttDisconnected();
    // ⚠️ delay(500) حذف شد - checkForUpdate در حالت IDLE اجرا میشه
    // و500ms settle کافیه با millis()-based delay در CONNECTING_HTTP

    if (!_client->connect(OTA_FW_SERVER_IP, OTA_FW_SERVER_PORT))
    {
        Serial.println(F("[OTA] ✗ اتصال چک آپدیت ناموفق بود"));
        ensureMqttConnected();
        return false;
    }

    String path = String(OTA_CHECK_ENDPOINT_PATH) + "?device_id=" + DEVICE_ID + "&current_version=" + FIRMWARE_VERSION;

    String request = "GET " + path + " HTTP/1.1\r\n";
    request += "Host: ";
    request += OTA_FW_SERVER_IP;
    request += ":";
    request += OTA_FW_SERVER_PORT;
    request += "\r\n";
    request += "User-Agent: ESP32-OTA-Check/1.0\r\n";
    request += "Connection: close\r\n\r\n";

    size_t sent = _client->write((const uint8_t *)request.c_str(), request.length());
    if (sent != request.length())
    {
        Serial.println(F("[OTA] ✗ ارسال درخواست چک ناموفق بود"));
        _client->stop();
        ensureMqttConnected();
        return false;
    }

    unsigned long startWait = millis();
    while (_client->available() <= 0 && millis() - startWait < OTA_CHECK_TIMEOUT_MS)
    {
        feedWatchdog();
        _client->loop();
        delay(10);
    }

    if (_client->available() <= 0)
    {
        Serial.println(F("[OTA] ✗ Timeout پاسخ چک آپدیت"));
        _client->stop();
        ensureMqttConnected();
        return false;
    }

    if (!parseHttpStatusLine())
    {
        _client->stop();
        ensureMqttConnected();
        return false;
    }

    if (_httpRespCode == 404)
    {
        Serial.println(F("[OTA] چک آپدیت: سرور فریموری برای اعلام ندارد (404)"));
        _client->stop();
        ensureMqttConnected();
        return false;
    }

    if (_httpRespCode != 200)
    {
        Serial.print(F("[OTA] ✗ چک آپدیت HTTP خطا: "));
        Serial.println(_httpRespCode);
        _client->stop();
        ensureMqttConnected();
        return false;
    }

    if (!parseHttpHeaders())
    {
        _client->stop();
        ensureMqttConnected();
        return false;
    }

    if (_contentLength == 0 || _contentLength > 511)
    {
        Serial.println(F("[OTA] ✗ اندازه پاسخ چک آپدیت نامعتبر است"));
        _client->stop();
        delay(300);
        ensureMqttConnected();
        return false;
    }

    char body[512];
    uint32_t received = 0;
    unsigned long bodyStart = millis();
    while (received < _contentLength && millis() - bodyStart < OTA_CHECK_TIMEOUT_MS)
    {
        int n = readHttpBodyChunk((uint8_t *)(body + received), _contentLength - received);
        if (n > 0)
            received += n;
        else
            delay(10);
    }
    body[received] = '\0';

    _client->stop();

    if (received < _contentLength)
    {
        Serial.println(F("[OTA] ✗ بدنه‌ی پاسخ چک آپدیت ناقص دریافت شد"));
        ensureMqttConnected();
        return false;
    }

    StaticJsonDocument<384> doc;
    DeserializationError jsonErr = deserializeJson(doc, body, received);
    if (jsonErr)
    {
        Serial.print(F("[OTA] ✗ خطای پارس JSON چک آپدیت: "));
        Serial.println(jsonErr.c_str());
        ensureMqttConnected();
        return false;
    }

    const char *version = doc["version"] | "";
    const char *url = doc["url"] | "";
    uint32_t size = doc["size"] | 0;
    const char *sha256 = doc["sha256"] | "";

    if (strlen(version) == 0 || strlen(url) == 0 || size == 0 || strlen(sha256) != 64)
    {
        Serial.println(F("[OTA] چک آپدیت: پاسخ سرور ناقص است"));
        ensureMqttConnected();
        return false;
    }

    if (strcmp(version, FIRMWARE_VERSION) == 0)
    {
        Serial.println(F("[OTA] چک آپدیت: نسخه‌ی فعلی از قبل به‌روز است"));
        ensureMqttConnected();
        return false;
    }

    Serial.print(F("[OTA] ✓ نسخه‌ی جدید موجود است: "));
    Serial.println(version);

    if (strlen(SYSTEM_ALERT_PHONE) > 0)
    {
        char msg[180];
        snprintf(msg, sizeof(msg), "GPS Tracker: بروزرسانی جدید %s موجود است (%.0f KB) - شروع نصب خودکار...",
                 version, size / 1024.0);
        _simMgr->sendSMS(SYSTEM_ALERT_PHONE, msg);
    }

    memset(&_meta, 0, sizeof(_meta));
    _meta.active = true;
    _meta.offset = 0;
    _meta.totalSize = size;
    strncpy(_meta.sha256, sha256, sizeof(_meta.sha256) - 1);
    strncpy(_meta.version, version, sizeof(_meta.version) - 1);
    strncpy(_meta.urlPath, url, sizeof(_meta.urlPath) - 1);

    saveMetadata();
    saveOffset();

    _globalRetries = 0;
    _lastSmsProgressMilestone = -1;
    enterState(OtaState::REQUESTED);

    return true;
}

// ════════════════════════════════════════════════════════════════
// ═══ State Machine — loop() ═══
// ════════════════════════════════════════════════════════════════

void OTAManager::loop()
{
    feedWatchdog();

    switch (_state)
    {
    case OtaState::IDLE:
        break;

    case OtaState::REQUESTED:
    {
        if (millis() - _stateEnterTime < 2000)
            break;

        if (!checkPrerequisites())
        {
            if (millis() - _retryTimer > OTA_GLOBAL_RETRY_DELAY_MS)
            {
                _globalRetries++;
                if (_globalRetries > OTA_MAX_GLOBAL_RETRIES)
                {
                    failOta("Prerequisites not met after max retries");
                    return;
                }
                _retryTimer = millis();
                Serial.print(F("[OTA] Retrying prerequisites (attempt "));
                Serial.print(_globalRetries);
                Serial.println(F(")"));
            }
            break;
        }

        publishStatus("started");
        enterState(OtaState::PREPARING);
        break;
    }

    case OtaState::PREPARING:
    {
        if (doPrepare())
        {
            enterState(OtaState::CONNECTING_HTTP);
        }
        else
        {
            failOta("Prepare failed");
        }
        break;
    }

    case OtaState::CONNECTING_HTTP:
    {
        // ⚠️ Non-blocking settle delay (جایگزین delay() قبلی)
        if (_delayResumeMs > 0 && millis() < _delayResumeMs)
            break;

        if (doConnectHttp())
        {
            enterState(OtaState::DOWNLOADING);
        }
        else
        {
            _segmentRetries++;
            if (_segmentRetries > OTA_MAX_SEGMENT_RETRIES)
            {
                failOta("اتصال HTTP بعد از حداکثر تلاش برای این بخش ناموفق بود");
            }
            else
            {
                Serial.printf("[OTA] تلاش مجدد اتصال HTTP برای بخش %u/%u (%u/%u)\n",
                              _currentSegment + 1, _segmentCount, _segmentRetries, OTA_MAX_SEGMENT_RETRIES);
                unsigned long backoff = OTA_RETRY_BASE_DELAY_MS * (1UL << (_segmentRetries - 1));
                if (backoff > 30000)
                    backoff = 30000;
                delay(backoff);
            }
        }
        break;
    }

    case OtaState::DOWNLOADING:
    {
        if (doDownload())
        {
            doDisconnectHttp();

            if (_meta.offset >= _meta.totalSize)
            {
                enterState(OtaState::VERIFYING);
            }
            else
            {
                reportSegmentProgress();
                advanceSegment();
                enterState(OtaState::SEGMENT_COOLDOWN);
            }
        }
        break;
    }

    case OtaState::SEGMENT_COOLDOWN:
    {
        if (millis() - _stateEnterTime < OTA_SEGMENT_COOLDOWN_MS)
            break;
        enterState(OtaState::CONNECTING_HTTP);
        break;
    }

    case OtaState::VERIFYING:
    {
        if (doVerify())
        {
            enterState(OtaState::COMPLETING);
        }
        else
        {
            failOta("SHA256 verification failed");
        }
        break;
    }

    case OtaState::COMPLETING:
    {
        if (doComplete())
        {
            publishStatus("completed");

            if (_simMgr && strlen(SYSTEM_ALERT_PHONE) > 0)
            {
                char msg[160];
                snprintf(msg, sizeof(msg), "GPS Tracker: بروزرسانی به نسخه %s با موفقیت نصب شد - در حال ری‌استارت",
                         _meta.version);
                _simMgr->sendSMS(SYSTEM_ALERT_PHONE, msg);
            }

            enterState(OtaState::REBOOTING);
        }
        else
        {
            failOta("Complete (finalize) failed");
        }
        break;
    }

    case OtaState::REBOOTING:
    {
        Serial.println(F("[OTA] ✅ Update complete! Rebooting in 3 seconds..."));
        delay(3000);
        clearMetadata();
        ESP.restart();
        break;
    }

    case OtaState::FAILED_STATE:
    {
        doDisconnectHttp();
        ensureMqttConnected();

        if (_simMgr && strlen(SYSTEM_ALERT_PHONE) > 0)
        {
            char msg[220];
            snprintf(msg, sizeof(msg), "GPS Tracker: بروزرسانی %s ناموفق بود - %s", _meta.version, _lastError);
            _simMgr->sendSMS(SYSTEM_ALERT_PHONE, msg);
        }

        clearMetadata();
        enterState(OtaState::IDLE);
        break;
    }
    }
}

// ════════════════════════════════════════════════════════════════
// ═══ State Machine — enterState() ═══
// ════════════════════════════════════════════════════════════════

void OTAManager::enterState(OtaState newState)
{
    Serial.print(F("[OTA] State: "));
    Serial.print(getStateString());
    Serial.print(F(" → "));
    _state = newState;
    _stateEnterTime = millis();
    Serial.println(getStateString());
}

// ════════════════════════════════════════════════════════════════
// ═══ checkPrerequisites() ═══
// ════════════════════════════════════════════════════════════════

bool OTAManager::checkPrerequisites()
{
    if (!_simMgr->isGprsConnected())
    {
        Serial.println(F("[OTA] GPRS not connected"));
        return false;
    }

    int signal = _simMgr->getSignalQuality();
    if (signal < OTA_MIN_SIGNAL_QUALITY)
    {
        Serial.print(F("[OTA] Signal too low: "));
        Serial.print(signal);
        Serial.print(F(" (min: "));
        Serial.print(OTA_MIN_SIGNAL_QUALITY);
        Serial.println(F(")"));
        return false;
    }

    Serial.print(F("[OTA] Prerequisites OK (signal="));
    Serial.print(signal);
    Serial.println(F(")"));
    return true;
}

// ════════════════════════════════════════════════════════════════
// ═══ SHA256 Wrapper Methods — ✅ اصلاح شده برای استفاده از Heap ═══
// ════════════════════════════════════════════════════════════════

void OTAManager::sha256Init()
{
    if (_shaInitialized && _shaCtx)
    {
        mbedtls_sha256_free(_shaCtx);
        free(_shaCtx);
        _shaCtx = nullptr;
        _shaInitialized = false;
    }

    _shaCtx = (mbedtls_sha256_context *)malloc(sizeof(mbedtls_sha256_context));
    if (!_shaCtx)
    {
        Serial.println(F("[OTA] ✗ Failed to allocate SHA256 context!"));
        return;
    }

    mbedtls_sha256_init(_shaCtx);
    mbedtls_sha256_starts(_shaCtx, 0);
    _shaInitialized = true;
}

void OTAManager::sha256Update(const uint8_t *data, size_t len)
{
    if (_shaInitialized && _shaCtx)
    {
        mbedtls_sha256_update(_shaCtx, data, len);
    }
}

String OTAManager::sha256Final()
{
    uint8_t shaResult[32];
    memset(shaResult, 0, sizeof(shaResult));

    if (_shaInitialized && _shaCtx)
    {
        mbedtls_sha256_finish(_shaCtx, shaResult);
        mbedtls_sha256_free(_shaCtx);
        free(_shaCtx);
        _shaCtx = nullptr;
        _shaInitialized = false;
    }

    return sha256ToHex(shaResult, 32);
}

// ════════════════════════════════════════════════════════════════
// ═══ doPrepare() — Initialize Update + SHA256 + Segments ═══
// ════════════════════════════════════════════════════════════════

bool OTAManager::doPrepare()
{
    Serial.println(F("[OTA] Preparing..."));

    ensureMqttDisconnected();

    if (_meta.offset > 0)
    {
        Serial.println(F("[OTA] â  Cannot resume SHA256 state â restarting from 0"));
        _meta.offset = 0;
        saveOffset();
    }

    if (!Update.begin(_meta.totalSize, U_FLASH))
    {
        Serial.print(F("[OTA] â Update.begin failed: "));
        Serial.println(Update.errorString());
        return false;
    }

    sha256Init();
    computeSegmentBounds();
    _lastSmsProgressMilestone = -1;

    Serial.println(F("[OTA] Prepare OK"));

    // Non-blocking settle delay - give modem time after MQTT disconnect
    // State transition to CONNECTING_HTTP is handled by the caller (PREPARING case)
    _delayResumeMs = millis() + 500;
    return true;
}

// ════════════════════════════════════════════════════════════════
// ═══ Segmented Download — computeSegmentBounds() / advanceSegment() ═══
// ════════════════════════════════════════════════════════════════

void OTAManager::computeSegmentBounds()
{
    _segmentCount = OTA_SEGMENT_COUNT;
    if (_segmentCount == 0)
        _segmentCount = 1;

    _segmentSize = _meta.totalSize / _segmentCount;
    if (_segmentSize == 0)
        _segmentSize = _meta.totalSize;

    _currentSegment = (uint8_t)(_meta.offset / _segmentSize);
    if (_currentSegment >= _segmentCount)
        _currentSegment = _segmentCount - 1;

    _segmentEndOffset = (uint32_t)(_currentSegment + 1) * _segmentSize;
    if (_currentSegment == _segmentCount - 1 || _segmentEndOffset > _meta.totalSize)
    {
        _segmentEndOffset = _meta.totalSize;
    }

    _segmentRetries = 0;

    Serial.printf("[OTA] بخش %u/%u | محدوده: %lu - %lu\n",
                  _currentSegment + 1, _segmentCount,
                  (unsigned long)_meta.offset, (unsigned long)_segmentEndOffset);
}

void OTAManager::advanceSegment()
{
    _currentSegment++;
    _segmentEndOffset = (uint32_t)(_currentSegment + 1) * _segmentSize;
    if (_currentSegment >= _segmentCount - 1 || _segmentEndOffset > _meta.totalSize)
    {
        _segmentEndOffset = _meta.totalSize;
    }
    _segmentRetries = 0;

    Serial.printf("[OTA] رفتن به بخش %u/%u | محدوده: %lu - %lu\n",
                  _currentSegment + 1, _segmentCount,
                  (unsigned long)_meta.offset, (unsigned long)_segmentEndOffset);
}

void OTAManager::reportSegmentProgress()
{
    uint8_t progress = getProgress();

    Serial.printf("[OTA] ✓ بخش %u/%u کامل شد (%u%%)\n",
                  _currentSegment + 1, _segmentCount, progress);

    publishStatus("progress");
    maybeSendProgressSms(progress);
}

void OTAManager::maybeSendProgressSms(uint8_t progress)
{
    if (!_simMgr || strlen(SYSTEM_ALERT_PHONE) == 0)
        return;

    int8_t milestone = (int8_t)((progress / 10) * 10);

    if (milestone < 10 || milestone <= _lastSmsProgressMilestone)
        return;

    _lastSmsProgressMilestone = milestone;

    char msg[112];
    snprintf(msg, sizeof(msg), "GPS Tracker: پیشرفت بروزرسانی %s: %d%%", _meta.version, milestone);
    _simMgr->sendSMS(SYSTEM_ALERT_PHONE, msg);

    Serial.printf("[OTA] پیامک پیشرفت ارسال شد: %d%%\n", milestone);
}

// ════════════════════════════════════════════════════════════════
// ═══ Raw HTTP Client — buildUrl() ═══
// ════════════════════════════════════════════════════════════════

String OTAManager::buildUrl()
{
    String url = "http://";
    url += OTA_FW_SERVER_IP;
    url += ":";
    url += OTA_FW_SERVER_PORT;
    url += _meta.urlPath;
    return url;
}

// ════════════════════════════════════════════════════════════════
// ═══ Raw HTTP Client — sendHttpGet() — ⚠️ Range همیشه محدود به بخش فعلی ═══
// ════════════════════════════════════════════════════════════════

bool OTAManager::sendHttpGet()
{
    String request = "GET ";
    request += _meta.urlPath;
    request += " HTTP/1.1\r\n";
    request += "Host: ";
    request += OTA_FW_SERVER_IP;
    request += ":";
    request += OTA_FW_SERVER_PORT;
    request += "\r\n";
    request += "User-Agent: ESP32-OTA/1.0\r\n";
    request += "Connection: close\r\n";

    request += "Range: bytes=";
    request += _meta.offset;
    request += "-";
    request += (_segmentEndOffset - 1);
    request += "\r\n";

    request += "\r\n";

    Serial.print(F("[OTA] HTTP Request:\n"));
    Serial.println(request);

    size_t sent = _client->write((const uint8_t *)request.c_str(), request.length());
    if (sent != request.length())
    {
        Serial.print(F("[OTA] ✗ HTTP request send failed (sent="));
        Serial.print(sent);
        Serial.println(F(")"));
        return false;
    }

    unsigned long startWait = millis();
    while (_client->available() <= 0 && millis() - startWait < OTA_HTTP_TIMEOUT_MS)
    {
        feedWatchdog();
        _client->loop();
        delay(10);
    }

    if (_client->available() <= 0)
    {
        Serial.println(F("[OTA] ✗ HTTP response timeout"));
        return false;
    }

    return true;
}

// ════════════════════════════════════════════════════════════════
// ═══ Raw HTTP Client — parseHttpStatusLine() ═══
// ════════════════════════════════════════════════════════════════

bool OTAManager::parseHttpStatusLine()
{
    int idx = 0;
    unsigned long startTime = millis();

    while (millis() - startTime < OTA_HTTP_TIMEOUT_MS)
    {
        feedWatchdog();
        _client->loop();

        while (_client->available() > 0 && idx < HTTP_LINE_BUF_SIZE - 1)
        {
            char c = (char)_client->read();
            if (c == '\n')
            {
                _httpLineBuf[idx] = '\0';
                if (idx > 0 && _httpLineBuf[idx - 1] == '\r')
                    _httpLineBuf[idx - 1] = '\0';

                Serial.print(F("[OTA] Status: "));
                Serial.println(_httpLineBuf);

                if (strncmp(_httpLineBuf, "HTTP/", 5) != 0)
                {
                    Serial.println(F("[OTA] ✗ Invalid HTTP response"));
                    return false;
                }

                char *space = strchr(_httpLineBuf, ' ');
                if (!space)
                {
                    Serial.println(F("[OTA] ✗ Malformed status line"));
                    return false;
                }
                _httpRespCode = atoi(space + 1);

                Serial.print(F("[OTA] HTTP code: "));
                Serial.println(_httpRespCode);
                return true;
            }
            _httpLineBuf[idx++] = c;
        }
        delay(5);
    }

    Serial.println(F("[OTA] ✗ Timeout reading status line"));
    return false;
}

// ════════════════════════════════════════════════════════════════
// ═══ Raw HTTP Client — parseHttpHeaders() ═══
// ════════════════════════════════════════════════════════════════

bool OTAManager::parseHttpHeaders()
{
    _contentLength = 0;
    _serverSupportsRange = false;

    unsigned long startTime = millis();

    while (millis() - startTime < OTA_HTTP_TIMEOUT_MS)
    {
        feedWatchdog();
        _client->loop();

        int idx = 0;
        bool lineComplete = false;

        while (_client->available() > 0 && idx < HTTP_LINE_BUF_SIZE - 1)
        {
            char c = (char)_client->read();
            if (c == '\n')
            {
                _httpLineBuf[idx] = '\0';
                if (idx > 0 && _httpLineBuf[idx - 1] == '\r')
                    _httpLineBuf[idx - 1] = '\0';
                lineComplete = true;
                break;
            }
            _httpLineBuf[idx++] = c;
        }

        if (!lineComplete)
        {
            delay(5);
            continue;
        }

        // ✅ Debug: چاپ هر header
        Serial.print(F("[OTA] RAW Header: ["));
        Serial.print(_httpLineBuf);
        Serial.println(F("]"));

        if (strlen(_httpLineBuf) == 0)
        {
            Serial.println(F("[OTA] Headers parsed (end)"));
            return true;
        }

        String header = String(_httpLineBuf);
        header.toLowerCase();

        if (header.startsWith("content-length:"))
        {
            const char *val = _httpLineBuf + 15;
            while (*val == ' ')
                val++;
            _contentLength = (uint32_t)atol(val);
            Serial.print(F("[OTA] ✓ Content-Length: "));
            Serial.println(_contentLength);
        }
        else if (header.startsWith("content-range:"))
        {
            _serverSupportsRange = true;
            Serial.print(F("[OTA] Content-Range: "));
            Serial.println(_httpLineBuf + 15);
        }
        else if (header.startsWith("accept-ranges:"))
        {
            const char *val = _httpLineBuf + 14;
            while (*val == ' ')
                val++;
            if (strncmp(val, "bytes", 5) == 0)
            {
                _serverSupportsRange = true;
                Serial.println(F("[OTA] Server supports Range"));
            }
        }
    }

    Serial.println(F("[OTA] ✗ Timeout parsing headers"));
    return false;
}

// ════════════════════════════════════════════════════════════════
// ═══ Raw HTTP Client — readHttpBodyChunk() ═══
// ════════════════════════════════════════════════════════════════

int OTAManager::readHttpBodyChunk(uint8_t *buf, int maxLen)
{
    feedWatchdog();
    _client->loop();

    int avail = _client->available();
    if (avail <= 0)
        return 0;

    int toRead = (avail < maxLen) ? avail : maxLen;
    int bytesRead = _client->read(buf, toRead);

    return bytesRead;
}

// ════════════════════════════════════════════════════════════════
// ═══ doConnectHttp() — Open HTTP with Range محدود به بخش فعلی ═══
// ════════════════════════════════════════════════════════════════

bool OTAManager::doConnectHttp()
{
    Serial.print(F("[OTA] Connecting to "));
    Serial.print(OTA_FW_SERVER_IP);
    Serial.print(F(":"));
    Serial.println(OTA_FW_SERVER_PORT);

    // ⚠️ حذف شد - این خط قبلاً "AT+CIPHEAD=0" رو به‌جای AT command، روی
    // سوکت TCP فعلی به‌عنوان دیتا می‌فرستاد (چون _client یک Client interface
    // هست و println() روش write روی سوکت انجام می‌ده، نه ارسال به لایه‌ی AT).
    // نتیجه‌ش: state پارسر AT مودم خراب می‌شد و هدرهای HTTP بعدی هم درست
    // پارس نمی‌شدن (Content-Length همیشه صفر می‌اومد). حذف این خط ضرری هم
    // نداره چون SIM800Client::connect() به‌هرحال خودش CIPHEAD=1 رو دوباره
    // ست می‌کنه - این خط از اول هیچ‌وقت واقعاً موثر نبوده.

    if (!_client->connect(OTA_FW_SERVER_IP, OTA_FW_SERVER_PORT))
    {
        Serial.println(F("[OTA] ✗ TCP connect failed"));
        return false;
    }

    Serial.println(F("[OTA] TCP connected, sending HTTP GET..."));

    if (!sendHttpGet())
    {
        Serial.println(F("[OTA] ✗ Failed to send HTTP request"));
        _client->stop();
        _delayResumeMs = millis() + 300;
        return false;
    }

    if (!parseHttpStatusLine())
    {
        Serial.println(F("[OTA] ✗ Failed to parse status line"));
        _client->stop();
        _delayResumeMs = millis() + 300;
        return false;
    }

    if (_httpRespCode != 200 && _httpRespCode != 206)
    {
        Serial.print(F("[OTA] ✗ HTTP error: "));
        Serial.println(_httpRespCode);

        if (_httpRespCode == 416)
        {
            Serial.println(F("[OTA] Range not satisfiable — restarting from 0"));
            _meta.offset = 0;
            saveOffset();
            computeSegmentBounds();
        }

        _client->stop();
        _delayResumeMs = millis() + 300;
        return false;
    }

    if (!parseHttpHeaders())
    {
        Serial.println(F("[OTA] ✗ Failed to parse headers"));
        _client->stop();
        _delayResumeMs = millis() + 300;
        return false;
    }

    if (_contentLength == 0)
    {
        Serial.println(F("[OTA] ✗ Invalid Content-Length (0)"));
        _client->stop();
        _delayResumeMs = millis() + 300;
        return false;
    }

    _httpConnected = true;

    if (_httpRespCode == 200 && _meta.offset > 0)
    {
        Serial.println(F("[OTA] ⚠ سرور Range رو نادیده گرفت (200 به‌جای 206) - ری‌استارت کامل از صفر"));
        _meta.offset = 0;
        saveOffset();

        Update.abort();
        if (!Update.begin(_meta.totalSize, U_FLASH))
        {
            Serial.println(F("[OTA] ✗ Update.begin after 200 failed"));
            _client->stop();
            _httpConnected = false;
            delay(300);
            return false;
        }
        sha256Init();
        computeSegmentBounds();
    }
    else if (_httpRespCode == 200 && _meta.offset == 0)
    {
        Serial.println(F("[OTA] ⚠ سرور از Range پشتیبانی نمی‌کند - دانلود تکه‌ای غیرفعال، کل فایل یکجا می‌آید"));
    }

    if (_contentLength > 0)
    {
        uint32_t expectedLen = _segmentEndOffset - _meta.offset;
        if (_httpRespCode == 206 && _contentLength != expectedLen)
        {
            Serial.print(F("[OTA] ⚠ Content-Length mismatch: got="));
            Serial.print(_contentLength);
            Serial.print(F(" expected="));
            Serial.println(expectedLen);
        }
    }

    Serial.print(F("[OTA] HTTP connected, resp="));
    Serial.print(_httpRespCode);
    Serial.print(F(" contentLen="));
    Serial.println(_contentLength);
    return true;
}

void OTAManager::doDisconnectHttp()
{
    if (_httpConnected)
    {
        _client->stop();
        delay(300);
        _httpConnected = false;

        // ⚠️ حذف شد - همون باگ برعکس (AT+CIPHEAD=1 روی سوکت). بعد از stop()
        // هم خطرناکه (اگه lib هنوز state رو "connected" فرض کنه) و به‌هرحال
        // بی‌فایده‌ست چون connect() بعدی خودش CIPHEAD=1 رو دوباره ست می‌کنه.
    }
}

// ════════════════════════════════════════════════════════════════
// ═══ doDownload() — ✅ اصلاح شده با بافرینگ برای نوشتن بلاکی ═══
// ════════════════════════════════════════════════════════════════

bool OTAManager::doDownload()
{
    if (!_httpConnected)
    {
        Serial.println(F("[OTA] ✗ HTTP not connected in download"));
        enterState(OtaState::CONNECTING_HTTP);
        return false;
    }

    static uint8_t writeBuf[CHUNK_BUF_SIZE];
    static int accumulated = 0;

    int bytesRead = readHttpBodyChunk(_chunkBuf, CHUNK_BUF_SIZE);

    if (bytesRead > 0)
    {
        int spaceLeft = CHUNK_BUF_SIZE - accumulated;
        int toCopy = (bytesRead < spaceLeft) ? bytesRead : spaceLeft;

        memcpy(writeBuf + accumulated, _chunkBuf, toCopy);
        accumulated += toCopy;

        if (accumulated >= CHUNK_BUF_SIZE || (_meta.offset + accumulated) >= _segmentEndOffset)
        {
            size_t written = Update.write(writeBuf, accumulated);

            if (written != (size_t)accumulated)
            {
                Serial.print(F("[OTA] ✗ Update.write mismatch: wrote="));
                Serial.print(written);
                Serial.print(F(" expected="));
                Serial.println(accumulated);
                failOta("Flash write error");
                accumulated = 0;
                return false;
            }

            sha256Update(writeBuf, accumulated);
            _meta.offset += accumulated;

            if (_meta.offset % 8192 < CHUNK_BUF_SIZE)
            {
                saveOffset();
            }

            accumulated = 0;
            _segmentRetries = 0;

            if (_meta.offset % (64 * 1024) < CHUNK_BUF_SIZE)
            {
                Serial.print(F("[OTA] Progress: "));
                Serial.print(_meta.offset);
                Serial.print(F("/"));
                Serial.print(_meta.totalSize);
                Serial.print(F(" ("));
                Serial.print(getProgress());
                Serial.println(F("%)"));
            }
        }

        if (toCopy < bytesRead)
        {
            int remaining = bytesRead - toCopy;
            size_t written = Update.write(_chunkBuf + toCopy, remaining);

            if (written != (size_t)remaining)
            {
                failOta("Flash write error (overflow)");
                return false;
            }

            sha256Update(_chunkBuf + toCopy, remaining);
            _meta.offset += remaining;
        }
    }

    if (_meta.offset >= _segmentEndOffset)
    {
        if (accumulated > 0)
        {
            size_t written = Update.write(writeBuf, accumulated);
            if (written != (size_t)accumulated)
            {
                failOta("Flash write error (final chunk)");
                accumulated = 0;
                return false;
            }
            sha256Update(writeBuf, accumulated);
            _meta.offset += accumulated;
            accumulated = 0;
        }

        Serial.println(F("[OTA] بخش کامل شد"));
        return true;
    }

    if (!_client->connected() && bytesRead <= 0)
    {
        if (_meta.offset < _segmentEndOffset)
        {
            Serial.println(F("[OTA] ✗ اتصال حین دانلود قطع شد"));
            doDisconnectHttp();
            saveOffset();

            _segmentRetries++;
            if (_segmentRetries > OTA_MAX_SEGMENT_RETRIES)
            {
                failOta("اتصال در طول بخش قطع شد (بعد از حداکثر تلاش مجدد)");
                return false;
            }

            Serial.printf("[OTA] تلاش مجدد اتصال بخش (%u/%u)\n", _segmentRetries, OTA_MAX_SEGMENT_RETRIES);

            unsigned long backoff = OTA_RETRY_BASE_DELAY_MS * (1UL << (_segmentRetries - 1));
            if (backoff > 30000)
                backoff = 30000;
            delay(backoff);

            enterState(OtaState::CONNECTING_HTTP);
            return false;
        }
    }

    // ⚠️ delay(50) حذف شد - بازگشت فوری به loop غیربلاکینگ

    return false;
}

// ════════════════════════════════════════════════════════
// ═══ doVerify() — Compare SHA256 ═══
// ════════════════════════════════════════════════════════════════

String OTAManager::sha256ToHex(const uint8_t *hash, size_t len)
{
    String hex = "";
    for (size_t i = 0; i < len; i++)
    {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", hash[i]);
        hex += buf;
    }
    return hex;
}

bool OTAManager::doVerify()
{
    Serial.println(F("[OTA] Verifying SHA256..."));

    String computed = sha256Final();
    Serial.print(F("[OTA] Computed SHA256: "));
    Serial.println(computed);
    Serial.print(F("[OTA] Expected SHA256: "));
    Serial.println(_meta.sha256);

    String expectedLower = String(_meta.sha256);
    expectedLower.toLowerCase();
    String computedLower = computed;
    computedLower.toLowerCase();

    if (computedLower != expectedLower)
    {
        Serial.println(F("[OTA] ✗✗ SHA256 MISMATCH! Aborting update."));
        return false;
    }

    Serial.println(F("[OTA] ✓ SHA256 verified!"));
    return true;
}

// ════════════════════════════════════════════════════════════════
// ═══ doComplete() — Finalize and set boot partition ═══
// ════════════════════════════════════════════════════════════════

bool OTAManager::doComplete()
{
    Serial.println(F("[OTA] Finalizing update..."));

    if (!Update.end(true))
    {
        Serial.print(F("[OTA] ✗ Update.end failed: "));
        Serial.println(Update.errorString());
        return false;
    }

    if (!Update.isFinished())
    {
        Serial.println(F("[OTA] ✗ Update not finished"));
        return false;
    }

    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition)
    {
        Serial.println(F("[OTA] ✗ No next update partition"));
        return false;
    }

    esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK)
    {
        Serial.print(F("[OTA] ✗ set_boot_partition failed: "));
        Serial.println(esp_err_to_name(err));
        return false;
    }

    Serial.print(F("[OTA] ✓ Boot partition set to "));
    Serial.println((partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) ? "OTA_0" : "OTA_1");

    err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK)
    {
        Serial.print(F("[OTA] ⚠ mark_app_valid failed (non-fatal): "));
        Serial.println(esp_err_to_name(err));
    }

    return true;
}

// ════════════════════════════════════════════════════════════════
// ═══ MQTT Status Publishing ═══
// ════════════════════════════════════════════════════════════════

bool OTAManager::ensureMqttConnected()
{
    if (_mqttMgr && !_mqttMgr->isConnected())
    {
        Serial.println(F("[OTA] Reconnecting MQTT for status..."));
        _mqttMgr->connect();
        delay(2000);
        _mqttMgr->loop();
    }
    return _mqttMgr ? _mqttMgr->isConnected() : false;
}

void OTAManager::ensureMqttDisconnected()
{
    if (_mqttMgr && _mqttMgr->isConnected())
    {
        Serial.println(F("[OTA] Disconnecting MQTT (free TCP for HTTP)..."));
        _client->stop();
        delay(500);
    }
}

void OTAManager::publishStatus(const char *status, const char *error)
{
    if (!_mqttMgr)
        return;

    if (!_mqttMgr->isConnected())
    {
        if (_httpConnected)
        {
            doDisconnectHttp();
        }

        if (!ensureMqttConnected())
        {
            Serial.println(F("[OTA] ⚠ Cannot publish status: MQTT not connected"));
            return;
        }
    }

    StaticJsonDocument<256> doc;
    doc["status"] = status;
    doc["version"] = _meta.version;
    doc["device_id"] = DEVICE_ID;
    doc["current_version"] = FIRMWARE_VERSION;

    if (strcmp(status, "progress") == 0 || strcmp(status, "downloading") == 0)
    {
        doc["progress"] = getProgress();
        doc["offset"] = _meta.offset;
        doc["total"] = _meta.totalSize;
        doc["segment"] = _currentSegment + 1;
        doc["segments_total"] = _segmentCount;
    }

    if (error)
    {
        doc["error"] = error;
    }

    _mqttMgr->publish(TOPIC_OTA_STATUS, doc, false);
    _mqttMgr->loop();
    delay(100);
}

// ════════════════════════════════════════════════════════════════
// ═══ Utility Methods ═══
// ════════════════════════════════════════════════════════════════

bool OTAManager::isInProgress() const
{
    return _state != OtaState::IDLE && _state != OtaState::FAILED_STATE;
}

uint8_t OTAManager::getProgress() const
{
    if (_meta.totalSize == 0)
        return 0;
    uint32_t pct = (_meta.offset * 100) / _meta.totalSize;
    if (pct > 100)
        pct = 100;
    return (uint8_t)pct;
}

const char *OTAManager::getStateString() const
{
    switch (_state)
    {
    case OtaState::IDLE:
        return "IDLE";
    case OtaState::REQUESTED:
        return "REQUESTED";
    case OtaState::PREPARING:
        return "PREPARING";
    case OtaState::CONNECTING_HTTP:
        return "CONNECTING_HTTP";
    case OtaState::DOWNLOADING:
        return "DOWNLOADING";
    case OtaState::SEGMENT_COOLDOWN:
        return "SEGMENT_COOLDOWN";
    case OtaState::VERIFYING:
        return "VERIFYING";
    case OtaState::COMPLETING:
        return "COMPLETING";
    case OtaState::REBOOTING:
        return "REBOOTING";
    case OtaState::FAILED_STATE:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

void OTAManager::abort()
{
    Serial.println(F("[OTA] Aborting!"));
    doDisconnectHttp();

    if (_shaInitialized && _shaCtx)
    {
        mbedtls_sha256_free(_shaCtx);
        free(_shaCtx);
        _shaCtx = nullptr;
        _shaInitialized = false;
    }

    Update.abort();
    clearMetadata();

    enterState(OtaState::FAILED_STATE);
    strncpy(_lastError, "Aborted by user", sizeof(_lastError) - 1);
    publishStatus("failed", "Aborted by user");
}

void OTAManager::failOta(const char *reason)
{
    Serial.print(F("[OTA] ✗ FAILED: "));
    Serial.println(reason);

    strncpy(_lastError, reason, sizeof(_lastError) - 1);
    _lastError[sizeof(_lastError) - 1] = '\0';

    if (_shaInitialized && _shaCtx)
    {
        mbedtls_sha256_free(_shaCtx);
        free(_shaCtx);
        _shaCtx = nullptr;
        _shaInitialized = false;
    }

    Update.abort();
    publishStatus("failed", reason);
    enterState(OtaState::FAILED_STATE);
}

void OTAManager::feedWatchdog()
{
    esp_task_wdt_reset();
}