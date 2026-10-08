#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "managers/GPSManager.h"
#include "managers/SIM800Manager.h"
#include "managers/MQTTManager.h"
#include "managers/LocationBuffer.h"
#include "handlers/RelayHandler.h"
#include "managers/OTAManager.h"
#include "SIM800Client.h"

// ════════════════════════════════════════════════════════════════
// ═══ تنظیمات ثابت (به‌جای magic numbers) ═══
// ════════════════════════════════════════════════════════════════
struct LocalGeofence
{
  String id;
  double lat;
  double lng;
  uint32_t radius;
  bool alertOnExit;
  bool alertOnEnter;
  bool exitUseCall;
  bool enterUseCall;
  uint16_t speedLimit;
  unsigned long lastSpeedAlertTime;
  bool wasInside;
  bool hasState;
};

static const uint8_t MAX_LOCAL_GEOFENCES = 10;
LocalGeofence localGeofences[MAX_LOCAL_GEOFENCES];
uint8_t localGeofenceCount = 0;
String geofenceAlertPhone = "";

uint16_t globalSpeedLimit = 0;
unsigned long lastGlobalSpeedAlertTime = 0;
static const uint32_t SPEED_ALERT_COOLDOWN_MS = 5 * 60 * 1000UL;

unsigned long lastGeofenceCheck = 0;
static const uint32_t GEOFENCE_CHECK_INTERVAL_MS = 5000;

enum class CallAlertState : uint8_t
{
  IDLE,
  DIALING,
  RINGING,
  HANGING_UP,
  WAIT_RETRY
};
CallAlertState callState = CallAlertState::IDLE;
unsigned long callStateStart = 0;
String callTargetPhone = "";
uint8_t callAttemptsRemaining = 0;

static const uint32_t CALL_RING_DURATION_MS = 60000;
static const uint32_t CALL_RETRY_GAP_MS = 8000;
static const uint8_t CALL_TOTAL_ATTEMPTS = 3;
static const uint32_t WDT_TIMEOUT_SEC = 30;
static const uint32_t AUTH_SESSION_TIMEOUT_MS = 120000;
static const uint8_t MAX_INIT_RETRIES = 3;
static const uint32_t SIM800_READY_TIMEOUT_MS = 20000;
static const uint32_t INIT_RETRY_DELAY_MS = 2000;

static const uint32_t CONN_HEALTH_CHECK_MS = 30000;
static const uint32_t RECONNECT_BACKOFF_MIN_MS = 5000;
static const uint32_t RECONNECT_BACKOFF_MAX_MS = 120000;

static const uint8_t ESCALATE_SOFT_REINIT_AFTER = 2; // بعد از این‌قدر شکست ساده
static const uint8_t ESCALATE_HARD_RESET_AFTER = 4;  // بعد از این‌قدر شکست
static const uint8_t ESCALATE_FATAL_REBOOT_AFTER = 6;
static const uint32_t AUTH_PUBLISH_SETTLE_MS = 500;
static const uint32_t AUTH_SMS_TIMEOUT_MS = 120000; // 2 دقیقه

static const uint16_t ALERT_BLINK_INTERVAL_MS = 50;
static const uint8_t ALERT_BLINK_TOGGLES = 20;

static const uint32_t STATUS_PRINT_INTERVAL_MS = 30000;

// ⚠️ جدید - محدودیت تلاش اشتباه OTP (جلوی brute force)
static const uint8_t MAX_AUTH_ATTEMPTS = 5;

#define ENABLE_PERIODIC_LOGS 1

// ════════════════════════════════════════════════════════════════
// ═══ GLOBALS ═══
// ════════════════════════════════════════════════════════════════

HardwareSerial SerialSIM(0);
HardwareSerial SerialGPS(1);

GPSManager gps;
SIM800Manager sim800(&SerialSIM);
SIM800Client sim800Client(sim800.getSIM());
MQTTManager mqtt(&sim800Client);
RelayHandler relay;
LocationBuffer locBuffer;                      // ⚠️ جدید - بافر backlog برای جبران قطعی اینترنت
OTAManager ota(&sim800Client, &sim800, &mqtt); // ⚠️ OTA Manager

bool authSessionValid = false;
unsigned long authSessionStart = 0;
unsigned long lastGpsPublish = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastStatusPrint = 0;
uint32_t failedPublishCount = 0;
unsigned long lastOtaCheck = 0;

// ⚠️ جدید - تایمر افزودن دوره‌ای نقطه به بافر backlog (مستقل از پابلیش زنده)
unsigned long lastLocBufferAppend = 0;

// ⚠️ جدید - Sync state machine (هندشیک backlog با سرور)
enum class SyncState : uint8_t
{
  IDLE,
  WAITING_ACK
};
SyncState syncState = SyncState::IDLE;
unsigned long syncStateStart = 0;
unsigned long lastSyncAttempt = 0;
bool syncAckReceived = false;
uint32_t syncAckLastKnownSeq = 0;

enum class AuthState : uint8_t
{
  IDLE,
  PUBLISHING_PENDING,
  WAITING_SMS_RESULT
};
AuthState authState = AuthState::IDLE;
unsigned long authStateStart = 0;
unsigned long authPublishStart = 0; // ⚠️ جدید - جایگزین delay()
String authPendingPhone = "";
String authPendingCode = "";
bool authSmsAccepted = false;
uint8_t authFailedAttempts = 0; // ⚠️ جدید - شمارنده تلاش اشتباه

bool alertBlinkActive = false;
unsigned long alertBlinkLastToggle = 0;
uint8_t alertBlinkToggleCount = 0;

unsigned long lastHealthCheck = 0;
unsigned long lastGprsReconnectAttempt = 0;
uint32_t gprsReconnectBackoff = RECONNECT_BACKOFF_MIN_MS;
uint8_t gprsReconnectFailCount = 0;

// ⚠️ جدید - وضعیت شبکه برای هشدار قطعی طولانی
bool netConnectedState = false;
unsigned long netDownSince = 0;

// ⚠️ جدید - وضعیت بکسانی (دستگاه روشن شدن ماشین)
bool lastIgnitionState = false;
unsigned long lastIgnitionCheck = 0;

enum class ConnLedPattern : uint8_t
{
  OFF,
  SOLID,
  SLOW_BLINK,
  FAST_BLINK
};
ConnLedPattern currentLedPattern = ConnLedPattern::FAST_BLINK;
unsigned long ledPatternLastToggle = 0;
bool ledPatternState = false;
unsigned long lastLedStatusCheck = 0;

static const uint32_t LED_SLOW_BLINK_MS = 600;
static const uint32_t LED_FAST_BLINK_MS = 150;
static const uint32_t LED_STATUS_CHECK_INTERVAL_MS = 1000;

// ════════════════════════════════════════════════════════════════
// ═══ FORWARD DECLARATIONS ═══
// ════════════════════════════════════════════════════════════════
void updateConnectionStatusLed(unsigned long now);
void updateSystemAlerts(unsigned long now); // ⚠️ جدید - هشدار شبکه و روشن شدن ماشین

void onMqttMessage(const char *topic, JsonDocument &doc);
bool publishSafe(const char *topic, JsonDocument &doc);
void publishGPS();
void publishHeartbeat();
double distanceMeters(double lat1, double lng1, double lat2, double lng2);
void handleGeofenceConfig(JsonDocument &doc);
void checkLocalGeofences(unsigned long now);
void startCallAlert(const String &phone);
void updateCallAlertStateMachine(unsigned long now);
void startAuthRequest(const String &phone);
void updateAuthStateMachine(unsigned long now);
void handleAuthVerify(const String &phone, const String &enteredCode);
void resetAuthState();                          // ⚠️ جدید
void updateSyncStateMachine(unsigned long now); // ⚠️ جدید

void handleRelayCommand(const char *action);
bool isValidRelayAction(const char *action);

bool isAuthSessionValid();

void handleGeofenceAlert(const char *action, const char *geofenceName);
void startAlertBlink();
void updateAlertBlink(unsigned long now);

void initWatchdog();
inline void feedWatchdog();

bool initSim800WithRetry();
bool connectGprsWithRetry();
void manageConnections(unsigned long now);
void fatalRebootSequence(const char *reason);

void printBanner();
void printSystemStatus();

// ════════════════════════════════════════════════════════════════
// ═══ WATCHDOG ═══
// ════════════════════════════════════════════════════════════════

void initWatchdog()
{
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wdtConfig = {
      .timeout_ms = WDT_TIMEOUT_SEC * 1000UL,
      .idle_core_mask = (uint32_t)((1 << portNUM_PROCESSORS) - 1),
      .trigger_panic = true};

  esp_err_t err = esp_task_wdt_reconfigure(&wdtConfig);
  if (err == ESP_ERR_INVALID_STATE)
  {
    esp_task_wdt_init(&wdtConfig);
  }
#else
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
#endif
  esp_task_wdt_add(NULL);
  Serial.printf("[WDT] فعال شد - timeout: %lus\n", (unsigned long)WDT_TIMEOUT_SEC);
}

inline void feedWatchdog()
{
  esp_task_wdt_reset();
}

// ════════════════════════════════════════════════════════════════
// ═══ SETUP با Retry محدود ═══
// ════════════════════════════════════════════════════════════════
void updateConnectionStatusLed(unsigned long now)
{
  if (alertBlinkActive)
    return;

  if (now - lastLedStatusCheck >= LED_STATUS_CHECK_INTERVAL_MS)
  {
    lastLedStatusCheck = now;

    bool gprsOk = sim800.isGprsConnected();
    bool gpsOk = gps.hasSignal();
    bool mqttOk = mqtt.isConnected();

    ConnLedPattern newPattern;
    if (!gprsOk)
      newPattern = ConnLedPattern::FAST_BLINK;
    else if (!gpsOk)
      newPattern = ConnLedPattern::SLOW_BLINK;
    else if (!mqttOk)
      newPattern = ConnLedPattern::SOLID;
    else
      newPattern = ConnLedPattern::OFF;

    if (newPattern != currentLedPattern)
    {
      currentLedPattern = newPattern;
      ledPatternLastToggle = now;
      ledPatternState = false;
    }
  }

  switch (currentLedPattern)
  {
  case ConnLedPattern::OFF:
    digitalWrite(LED_STATUS, LOW);
    break;

  case ConnLedPattern::SOLID:
    digitalWrite(LED_STATUS, HIGH);
    break;

  case ConnLedPattern::SLOW_BLINK:
    if (now - ledPatternLastToggle >= LED_SLOW_BLINK_MS)
    {
      ledPatternLastToggle = now;
      ledPatternState = !ledPatternState;
      digitalWrite(LED_STATUS, ledPatternState ? HIGH : LOW);
    }
    break;

  case ConnLedPattern::FAST_BLINK:
    if (now - ledPatternLastToggle >= LED_FAST_BLINK_MS)
    {
      ledPatternLastToggle = now;
      ledPatternState = !ledPatternState;
      digitalWrite(LED_STATUS, ledPatternState ? HIGH : LOW);
    }
    break;
  }
}

bool initSim800WithRetry()
{
  for (uint8_t attempt = 1; attempt <= MAX_INIT_RETRIES; attempt++)
  {
    Serial.printf("[SIM800] تلاش %d/%d برای آماده شدن...\n", attempt, MAX_INIT_RETRIES);

    unsigned long start = millis();
    while (!sim800.isReady() && (millis() - start < SIM800_READY_TIMEOUT_MS))
    {
      sim800.loop();
      feedWatchdog();
      delay(100);
    }

    if (sim800.isReady())
    {
      Serial.println(F("[SIM800] ✓ Ready"));
      return true;
    }

    Serial.println(F("[SIM800] تلاش ناموفق بود، یه‌کم صبر و تلاش مجدد..."));
    delay(INIT_RETRY_DELAY_MS);
    feedWatchdog();
  }
  return false;
}

bool isAuthSessionValid()
{
  if (!authSessionValid)
    return false;

  if (millis() - authSessionStart >= AUTH_SESSION_TIMEOUT_MS)
  {
    authSessionValid = false;
    return false;
  }

  return true;
}

bool connectGprsWithRetry()
{
  for (uint8_t attempt = 1; attempt <= MAX_INIT_RETRIES; attempt++)
  {
    Serial.printf("[GPRS] تلاش %d/%d برای اتصال...\n", attempt, MAX_INIT_RETRIES);

    bool connected = sim800.connectGPRS(GPRS_APN, GPRS_USER, GPRS_PASS);
    feedWatchdog();

    if (connected)
    {
      Serial.println(F("[GPRS] ✓ Connected"));
      return true;
    }

    Serial.println(F("[GPRS] تلاش ناموفق بود، یه‌کم صبر و تلاش مجدد..."));
    delay(INIT_RETRY_DELAY_MS);
    feedWatchdog();
  }
  return false;
}

void fatalRebootSequence(const char *reason)
{
  Serial.print(F("[FATAL] "));
  Serial.println(reason);
  Serial.println(F("[FATAL] ریست خودکار دستگاه..."));
  for (int i = 0; i < 10; i++)
  {
    digitalWrite(LED_STATUS, HIGH);
    delay(100);
    digitalWrite(LED_STATUS, LOW);
    delay(100);
    feedWatchdog();
  }
  delay(200);
  ESP.restart();
}
void setup()
{
  Serial.begin(115200);
  delay(3000);
  printBanner();

  initWatchdog();
  randomSeed(esp_random());
  pinMode(LED_STATUS, OUTPUT);
  pinMode(IGNITION_PIN, INPUT); // ⚠️ جدید - خواندن وضعیت روشن بودن ماشین

  Serial.println(F("[1/5] Hardware init..."));
  relay.begin();

  Serial.println(F("[SIM800] begin() ..."));
  sim800.begin(&SerialSIM);

  feedWatchdog();
  delay(500);
  feedWatchdog();

  Serial.println(F("[2/5] SIM800 init..."));
  if (!initSim800WithRetry())
  {
    fatalRebootSequence("SIM800 بعد از چند تلاش آماده نشد");
  }

  Serial.println(F("[3/5] GPRS connecting..."));
  if (!connectGprsWithRetry())
  {
    // ⚠️ جدید - قبل از ری‌بوت، چک کن که مشکل واقعاً سیمکارت نیست. اگه
    // سیمکارت نباشه/قفل باشه، هیچ تلاش مجددی (حتی ری‌بوت کامل) درستش
    // نمی‌کنه - قبلاً همین حالت باعث حلقه‌ی بی‌پایان ری‌بوت در همون لحظه‌ی
    // بوت می‌شد. الان راه‌اندازی رو در حالت آفلاین ادامه می‌دیم (GPS و
    // بافر backlog کار می‌کنن)؛ manageConnections() در loop خودش بعداً
    // دوباره و به‌صورت دوره‌ای چک می‌کنه.
    if (!sim800.isSimCardPresent())
    {
      Serial.println(F("[WARN] ⚠⚠ سیمکارت شناسایی نشد - راه‌اندازی در حالت آفلاین ادامه می‌یابد"));
    }
    else
    {
      fatalRebootSequence("GPRS بعد از چند تلاش وصل نشد");
    }
  }

  Serial.println(F("[4/5] MQTT init..."));
  mqtt.begin();
  mqtt.setMessageCallback(onMqttMessage);

  if (!mqtt.connect())
  {
    Serial.println(F("[WARN] MQTT not connected (loop() تلاش مجدد می‌کند)"));
  }
  feedWatchdog();

  Serial.println(F("[5/5] GPS init..."));
  gps.begin(&SerialGPS);

  Serial.println(F("[LOCBUF] بافر backlog init..."));
  locBuffer.begin();

  Serial.println(F("[OTA] OTA Manager init..."));
  ota.begin();
  ota.checkPendingOta(); // اگر OTA قبلی نیمه‌کاره رخ داده، resume کن

  Serial.println(F("\n════════════════════════════════════"));
  Serial.println(F("✓✓✓ SYSTEM READY ✓✓✓"));
  Serial.println(F("════════════════════════════════════\n"));

  for (int i = 0; i < 3; i++)
  {
    digitalWrite(LED_STATUS, HIGH);
    delay(200);
    digitalWrite(LED_STATUS, LOW);
    delay(200);
  }

  printSystemStatus();
  lastHealthCheck = millis();

  // ⚠️ جدید - مقداردهی اولیه وضعیت شبکه و بکسانی برای هشدارها
  netConnectedState = sim800.isGprsConnected();
  if (!netConnectedState)
    netDownSince = millis();
  lastIgnitionState = digitalRead(IGNITION_PIN);
}
// ════════════════════════════════════════════════════════════════
// ═══ LOOP (کاملا غیربلاکینگ) ═══
// ════════════════════════════════════════════════════════════════

void loop()
{
  unsigned long now = millis();

  feedWatchdog();

  // ─────────────────────────────────────────────
  // 1. Process managers
  // ─────────────────────────────────────────────
  sim800.loop();
  mqtt.loop();
  gps.loop();
  ota.loop(); // ⚠️ OTA state machine

  // ─────────────────────────────────────────────
  // 1.5 اگر OTA در حال انجام است، عملیات عادی را رد کن
  // ─────────────────────────────────────────────
  if (ota.isInProgress())
  {
    updateConnectionStatusLed(now);
    delay(50);
    return;
  }

  // ─────────────────────────────────────────────
  // 2. State machineهای غیربلاکینگ
  // ─────────────────────────────────────────────
  updateAuthStateMachine(now);
  updateCallAlertStateMachine(now);
  updateAlertBlink(now);
  checkLocalGeofences(now);
  updateSyncStateMachine(now);
  updateConnectionStatusLed(now);

  // ─────────────────────────────────────────────
  // 3. سلامت GPRS + reconnect
  // ─────────────────────────────────────────────
  manageConnections(now);

  // ─────────────────────────────────────────────
  // 3.5 هشدارهای سیستمی (شبکه و روشن شدن ماشین)
  // ─────────────────────────────────────────────
  updateSystemAlerts(now);

  // ─────────────────────────────────────────────
  // 4. افزودن دوره‌ای نقطه به بافر backlog
  // ─────────────────────────────────────────────
  if (gps.hasSignal() && (now - lastLocBufferAppend >= LOC_BUFFER_APPEND_INTERVAL_MS))
  {
    uint32_t ts;
    if (gps.getUnixTimestamp(ts))
    {
      GPSData d = gps.getData();
      locBuffer.addPoint(d.lat, d.lng, d.speed, ts);
    }
    lastLocBufferAppend = now;
  }

  // ─────────────────────────────────────────────
  // 5. GPS publish (زنده)
  // ─────────────────────────────────────────────
  if (mqtt.isConnected() && (now - lastGpsPublish >= GPS_UPDATE_INTERVAL))
  {
    publishGPS();
    lastGpsPublish = now;
  }

  // ─────────────────────────────────────────────
  // 6. Heartbeat
  // ─────────────────────────────────────────────
  if (mqtt.isConnected() && (now - lastHeartbeat >= HEARTBEAT_INTERVAL))
  {
    publishHeartbeat();
    lastHeartbeat = now;
  }

  // ─────────────────────────────────────────────
  // 7. Status print دوره‌ای
  // ─────────────────────────────────────────────
#if ENABLE_PERIODIC_LOGS
  if (now - lastStatusPrint >= STATUS_PRINT_INTERVAL_MS)
  {
    printSystemStatus();
    lastStatusPrint = now;
  }
#endif

  // ─────────────────────────────────────────────
  // 8. ⚠️ جدید - چک دوره‌ای آخرین نسخه فریمور از سرور
  // ─────────────────────────────────────────────
  if (sim800.isGprsConnected() &&
      callState == CallAlertState::IDLE &&
      (now - lastOtaCheck >= OTA_CHECK_INTERVAL_MS))
  {
    lastOtaCheck = now;
    ota.checkForUpdate();
  }

  delay(50);
}

// ════════════════════════════════════════════════════════════════
// ═══ سلامت GPRS + reconnect با backoff ═══
// ════════════════════════════════════════════════════════════════

static inline uint32_t clampBackoff(uint32_t v, uint32_t maxV)
{
  return (v > maxV) ? maxV : v;
}
// ════════════════════════════════════════════════════════════════
// ═══ هشدارهای سیستمی (شبکه و روشن شدن ماشین) ⚠️ جدید ═══
// ════════════════════════════════════════════════════════════════
void updateSystemAlerts(unsigned long now)
{
  // ─── هشدار قطعی و وصل مجدد شبکه ───
  bool gprsOk = sim800.isGprsConnected();

  if (gprsOk && !netConnectedState)
  {
    // شبکه وصل شد
    netConnectedState = true;
    unsigned long offlineDuration = now - netDownSince;
    Serial.printf("[NET] شبکه وصل شد بعد از %lums قطعی\n", offlineDuration);

    if (offlineDuration >= OFFLINE_THRESHOLD_MS && strlen(SYSTEM_ALERT_PHONE) > 0)
    {
      char msg[140];
      unsigned long mins = offlineDuration / 60000;
      if (mins > 0)
        snprintf(msg, sizeof(msg), "GPS Tracker: شبکه بعد از %lu دقیقه قطعی مجدداً وصل شد", mins);
      else
        snprintf(msg, sizeof(msg), "GPS Tracker: شبکه بعد از %lu ثانیه قطعی مجدداً وصل شد", offlineDuration / 1000);

      sim800.sendSMS(SYSTEM_ALERT_PHONE, msg);
      Serial.println(F("[SMS] پیامک هشدار وصل شدن شبکه ارسال شد"));
    }
  }
  else if (!gprsOk && netConnectedState)
  {
    // شبکه قطع شد
    netConnectedState = false;
    netDownSince = now;
    Serial.println(F("[NET] قطعی شبکه ثبت شد"));
  }

  // ─── هشدار روشن شدن ماشین (0 -> 1) ───
  if (now - lastIgnitionCheck >= 1000)
  {
    lastIgnitionCheck = now;
    bool currentIgnition = digitalRead(IGNITION_PIN);

    if (currentIgnition && !lastIgnitionState)
    {
      Serial.println(F("[IGN] ماشین روشن شد (0 -> 1)"));
      if (strlen(SYSTEM_ALERT_PHONE) > 0)
      {
        sim800.sendSMS(SYSTEM_ALERT_PHONE, "GPS Tracker: ماشین روشن شد");
        Serial.println(F("[SMS] پیامک هشدار روشن شدن ماشین ارسال شد"));
      }
    }
    lastIgnitionState = currentIgnition;
  }
}

void manageConnections(unsigned long now)
{
  // ⚠️ جدید - تست دستی کاوان تایید کرد وقتی PDP context/IP stack گیر
  // می‌کند (CIPSTART با ERROR سریع رد می‌شود درحالی‌که CGATT/CREG سالم
  // نشان می‌دهند)، تنها ترمیم قطعی toggle فیزیکی پین RST است - نه هیچ
  // دستور AT، حتی CIPSHUT. SIM800Client بعد از امتحان یک ترمیم نرم
  // (CIPSHUT+reconnect) اگر باز هم شکست بخورد، این پرچم را ست می‌کند.
  // اینجا بدون صبر کردن به تایمر عادی health-check (۳۰s) یا شمارنده‌ی
  // fail count (که می‌تواند چند دقیقه طول بکشد)، مستقیم می‌رویم سراغ
  // hard reset - چون از قبل می‌دانیم راه‌حل نرم‌تر کافی نیست.
  if (sim800Client.needsHardReset())
  {
    if (callState != CallAlertState::IDLE)
    {
      Serial.println(F("[NET] hard reset فوری لازمه ولی یک هشدار تماس در حال انجامه - به تعویق افتاد"));
    }
    else
    {
      Serial.println(F("[NET] ⚠⚠ درخواست hard reset فوری از SIM800Client (PDP context با ترمیم نرم برنگشت)"));
      sim800Client.clearNeedsHardReset();

      sim800.pauseProcessing(true);
      bool ok = sim800.hardReset();
      if (ok)
        ok = sim800.connectGPRS(GPRS_APN, GPRS_USER, GPRS_PASS);
      sim800.pauseProcessing(false);
      feedWatchdog();

      if (ok)
      {
        Serial.println(F("[NET] ✓ hard reset فوری موفق بود - GPRS وصل شد"));
        gprsReconnectBackoff = RECONNECT_BACKOFF_MIN_MS;
        gprsReconnectFailCount = 0;
        lastHealthCheck = now;
        return;
      }
      else
      {
        Serial.println(F("[NET] ✗ حتی hard reset فوری هم ناموفق بود - سیکل عادی escalation ادامه می‌دهد"));
      }
    }
  }

  if (now - lastHealthCheck < CONN_HEALTH_CHECK_MS)
    return;
  lastHealthCheck = now;

  bool gprsOk = sim800.isGprsConnected();

  // ⚠️ فیکس قبلی - بدون این خط، وقتی flag داخلی _gprsReady غلط "ON" مونده
  // باشه، escalation ladder زیر هیچ‌وقت trigger نمی‌شه چون همینجا early
  // return می‌خوریم.
  if (gprsOk)
  {
    gprsOk = sim800.verifyGprsAlive();
  }

  if (gprsOk)
  {
    if (gprsReconnectFailCount > 0)
      Serial.println(F("[NET] GPRS دوباره سالم شد ✓"));
    gprsReconnectFailCount = 0;
    gprsReconnectBackoff = RECONNECT_BACKOFF_MIN_MS;
    return;
  }

  if (now - lastGprsReconnectAttempt < gprsReconnectBackoff)
    return;

  if (callState != CallAlertState::IDLE)
  {
    Serial.println(F("[NET] یک هشدار تماس در حال انجامه - تلاش GPRS این دور به تعویق افتاد"));
    return;
  }

  lastGprsReconnectAttempt = now;

  sim800.pauseProcessing(true);

  bool modemAlive = sim800.isModemResponding();
  bool ok = false;

  if (!modemAlive)
  {
    Serial.println(F("[NET] ⚠ مودم به AT ساده جواب نمی‌ده → مستقیم Hard Reset"));
    ok = sim800.hardReset();
    if (ok)
      ok = sim800.connectGPRS(GPRS_APN, GPRS_USER, GPRS_PASS);
  }
  else if (!sim800.isSimCardPresent())
  {
    Serial.println(F("[NET] ⚠⚠ سیمکارت شناسایی نشد - از ری‌کانکت/ری‌ست صرف‌نظر شد (منتظر برگشت سیمکارت)"));
    gprsReconnectFailCount = 0;
    gprsReconnectBackoff = RECONNECT_BACKOFF_MAX_MS;
    sim800.pauseProcessing(false);
    feedWatchdog();
    return;
  }
  else if (gprsReconnectFailCount < ESCALATE_SOFT_REINIT_AFTER)
  {
    Serial.printf("[NET] تلاش ساده‌ی reconnect (شکست‌های قبلی: %d)\n", gprsReconnectFailCount);
    ok = sim800.connectGPRS(GPRS_APN, GPRS_USER, GPRS_PASS);
  }
  else if (gprsReconnectFailCount < ESCALATE_HARD_RESET_AFTER)
  {
    Serial.printf("[NET] ⚠ %d شکست پشت‌سرهم → Soft Reinit\n", gprsReconnectFailCount);
    ok = sim800.softReinit();
    if (ok)
      ok = sim800.connectGPRS(GPRS_APN, GPRS_USER, GPRS_PASS);
  }
  else if (gprsReconnectFailCount < ESCALATE_FATAL_REBOOT_AFTER)
  {
    Serial.printf("[NET] ⚠⚠ %d شکست پشت‌سرهم → Hard Reset فیزیکی\n", gprsReconnectFailCount);
    ok = sim800.hardReset();
    if (ok)
      ok = sim800.connectGPRS(GPRS_APN, GPRS_USER, GPRS_PASS);
  }
  else
  {
    sim800.pauseProcessing(false);
    fatalRebootSequence("GPRS بعد از تمام سطوح تلاش (ساده/نرم/سخت) وصل نشد");
    return;
  }

  sim800.pauseProcessing(false);
  feedWatchdog();

  if (ok)
  {
    Serial.println(F("[NET] GPRS با موفقیت وصل شد ✓"));
    gprsReconnectBackoff = RECONNECT_BACKOFF_MIN_MS;
    gprsReconnectFailCount = 0;
  }
  else
  {
    gprsReconnectFailCount++;
    gprsReconnectBackoff = clampBackoff(gprsReconnectBackoff * 2, RECONNECT_BACKOFF_MAX_MS);
    Serial.printf("[NET] تلاش شماره %d ناموفق - backoff بعدی: %lus\n",
                  gprsReconnectFailCount, (unsigned long)(gprsReconnectBackoff / 1000));
  }
}

// ════════════════════════════════════════════════════════════════
// ═══ MQTT CALLBACK ═══
// ════════════════════════════════════════════════════════════════

void onMqttMessage(const char *topic, JsonDocument &doc)
{
  String t = String(topic);
  Serial.printf("[MQTT DEBUG] پیام جدید آمد روی تاپیک: %s\n", topic);

  if (t == TOPIC_AUTH_REQ)
  {
    String phone = doc["phone"] | "";
    if (phone.length() >= 5)
    {
      startAuthRequest(phone);
    }
  }
  else if (t == TOPIC_AUTH_VERIFY)
  {
    String phone = doc["phone"] | "";
    String code = doc["code"] | "";
    if (phone.length() >= 5 && code.length() > 0)
    {
      handleAuthVerify(phone, code);
    }
  }
  else if (t == TOPIC_GEOFENCE_CONFIG)
  {
    handleGeofenceConfig(doc);
  }
  else if (t == TOPIC_RELAY_CMD)
  {
    const char *action = doc["action"];
    if (action)
    {
      handleRelayCommand(action);
    }
  }
  else if (t == TOPIC_GEOFENCE_ALERT)
  {
    const char *action = doc["action"];
    const char *gfName = doc["geofence"]["name"];

    if (action && gfName)
    {
      handleGeofenceAlert(action, gfName);
    }
  }
  // ⚠️ جدید - جواب سرور به sync/hello
  else if (t == TOPIC_SYNC_ACK)
  {
    syncAckLastKnownSeq = doc["lastKnownSeq"] | 0;
    syncAckReceived = true;
    Serial.printf("[SYNC] ← ack دریافت شد، lastKnownSeq: %lu\n", (unsigned long)syncAckLastKnownSeq);
  }
  // ⚠️ OTA update command
  else if (t == TOPIC_OTA_CMD)
  {
    Serial.println(F("[OTA] ← دستور آپدیت دریافت شد"));
    if (!ota.handleOtaCommand(doc))
    {
      Serial.println(F("[OTA] ⚠ دستور OTA نامعتبر یا prerequisities برقرار نیست"));
    }
  }
}

// ════════════════════════════════════════════════════════════════
// ═══ PUBLISHERS ═══
// ════════════════════════════════════════════════════════════════

bool publishSafe(const char *topic, JsonDocument &doc)
{
  bool ok = mqtt.publish(topic, doc);
  if (!ok)
  {
    failedPublishCount++;
    Serial.printf("[MQTT] ⚠ publish ناموفق روی %s (مجموع ناموفق: %lu)\n",
                  topic, (unsigned long)failedPublishCount);
  }
  return ok;
}

void publishGPS()
{
  StaticJsonDocument<384> doc;
  GPSData data = gps.getData();

  // فرمت هماهنگ با FastAPI backend
  doc["lat"] = data.lat;
  doc["lon"] = data.lng;
  doc["alt"] = data.altitude;
  doc["speed"] = data.speed;
  doc["bearing"] = data.course;
  doc["accuracy"] = data.hdop;
  doc["battery"] = 100; // TODO: واقعی کردن از ADC
  doc["network_type"] = "gsm";
  doc["satellites"] = data.satellites;
  uint32_t ts;
  doc["timestamp"] = gps.getUnixTimestamp(ts) ? ts : (millis() / 1000);
  doc["session_id"] = DEVICE_ID;

  publishSafe(TOPIC_LOCATION, doc);
}

void publishHeartbeat()
{
  StaticJsonDocument<256> doc;

  doc["device"] = DEVICE_ID;
  doc["uptime"] = millis() / 1000;
  doc["gsm_signal"] = sim800.getSignalQuality();
  doc["gps_status"] = gps.hasSignal() ? "ok" : "offline";
  doc["mqtt_status"] = "connected";
  doc["ip"] = sim800.getIP();

  publishSafe(TOPIC_HEARTBEAT, doc);

#if ENABLE_PERIODIC_LOGS
  Serial.println(F("[💓] Heartbeat sent"));
#endif
}

// ════════════════════════════════════════════════════════════════
// ═══ SYNC STATE MACHINE (هندشیک backlog با سرور) ⚠️ جدید ═══
// ════════════════════════════════════════════════════════════════

void updateSyncStateMachine(unsigned long now)
{
  if (!mqtt.isConnected())
  {
    // آفلاینیم - فقط بافر رشد می‌کنه، هندشیک بی‌فایده‌ست
    syncState = SyncState::IDLE;
    return;
  }

  switch (syncState)
  {
  case SyncState::IDLE:
  {
    if (!locBuffer.hasBacklog())
      return; // چیزی برای sync نیست

    if (now - lastSyncAttempt < SYNC_RETRY_INTERVAL_MS)
      return;

    lastSyncAttempt = now; // چه موفق چه ناموفق - جلوی spam رو می‌گیره

    StaticJsonDocument<96> doc;
    doc["oldestSeq"] = locBuffer.oldestSeq();
    doc["newestSeq"] = locBuffer.newestSeq();
    doc["count"] = locBuffer.pendingCount();

    if (mqtt.publish(TOPIC_SYNC_HELLO, doc))
    {
      syncAckReceived = false;
      syncStateStart = now;
      syncState = SyncState::WAITING_ACK;
      Serial.printf("[SYNC] → hello (oldest:%lu newest:%lu count:%u)\n",
                    (unsigned long)locBuffer.oldestSeq(),
                    (unsigned long)locBuffer.newestSeq(),
                    locBuffer.pendingCount());
    }
    break;
  }

  case SyncState::WAITING_ACK:
  {
    if (syncAckReceived)
    {
      locBuffer.confirmUpTo(syncAckLastKnownSeq);

      BufferedPoint batch[LOC_BUFFER_BACKLOG_BATCH_SIZE];
      uint8_t n = locBuffer.getNextBatch(batch, LOC_BUFFER_BACKLOG_BATCH_SIZE);

      if (n > 0)
      {
        StaticJsonDocument<1536> doc;
        JsonArray arr = doc.createNestedArray("points");
        for (uint8_t i = 0; i < n; i++)
        {
          JsonObject o = arr.createNestedObject();
          o["seq"] = batch[i].seq;
          o["lat"] = batch[i].lat;
          o["lon"] = batch[i].lng;
          o["speed"] = batch[i].speed;
          o["ts"] = batch[i].ts;
        }
        mqtt.publish(TOPIC_LOCATION_BACKLOG, doc);
        Serial.printf("[SYNC] → %u نقطه از backlog ارسال شد\n", n);
      }

      syncState = SyncState::IDLE;
    }
    else if (now - syncStateStart >= SYNC_ACK_TIMEOUT_MS)
    {
      Serial.println(F("[SYNC] ⚠ Timeout در انتظار sync/ack - دور بعد دوباره تلاش می‌شه"));
      syncState = SyncState::IDLE;
    }
    break;
  }
  }
}

// ════════════════════════════════════════════════════════════════
// ═══ AUTH STATE MACHINE (نسخه‌ی تمیز و غیر بلاکینگ) ═══
// ════════════════════════════════════════════════════════════════

// ⚠️ جدید - ریست کامل وضعیت auth + پاک‌سازی کد از RAM
void resetAuthState()
{
  authState = AuthState::IDLE;
  authPendingPhone = "";
  authPendingCode = "";
  authFailedAttempts = 0;
  authSmsAccepted = false;
}

void startAuthRequest(const String &phone)
{
  if (authState != AuthState::IDLE)
  {
    Serial.println(F("[AUTH] یک درخواست auth در حال پردازش است - این یکی نادیده گرفته شد"));
    return;
  }

  // کد ۶ رقمی امن‌تر با RNG سخت‌افزاری
  uint32_t code = esp_random() % 900000UL + 100000UL; // 6 رقمی: 100000-999999

  authPendingPhone = phone;
  authPendingCode = String(code);
  authFailedAttempts = 0;
  authPublishStart = millis();
  authState = AuthState::PUBLISHING_PENDING;

  Serial.print(F("[AUTH] درخواست OTP برای: "));
  Serial.println(phone);

  StaticJsonDocument<96> doc;
  doc["phone"] = authPendingPhone;
  doc["sms_status"] = "sending";
  publishSafe(TOPIC_AUTH_CODE, doc);
}

double distanceMeters(double lat1, double lng1, double lat2, double lng2)
{
  const double R = 6371000.0;
  double phi1 = lat1 * PI / 180.0;
  double phi2 = lat2 * PI / 180.0;
  double dPhi = (lat2 - lat1) * PI / 180.0;
  double dLambda = (lng2 - lng1) * PI / 180.0;
  double a = sin(dPhi / 2) * sin(dPhi / 2) +
             cos(phi1) * cos(phi2) * sin(dLambda / 2) * sin(dLambda / 2);
  return R * 2 * atan2(sqrt(a), sqrt(1 - a));
}

void handleGeofenceConfig(JsonDocument &doc)
{
  geofenceAlertPhone = doc["alertPhone"] | "";
  globalSpeedLimit = doc["globalSpeedLimit"] | 0;

  JsonArray arr = doc["geofences"];
  localGeofenceCount = 0;

  for (JsonObject gf : arr)
  {
    if (localGeofenceCount >= MAX_LOCAL_GEOFENCES)
      break;

    LocalGeofence &lg = localGeofences[localGeofenceCount];
    const char *idStr = gf["id"] | "";
    lg.id = String(idStr);
    lg.lat = gf["lat"] | 0.0;
    lg.lng = gf["lng"] | 0.0;
    lg.radius = gf["radius"] | 500;
    lg.alertOnExit = gf["alertOnExit"] | false;
    lg.alertOnEnter = gf["alertOnEnter"] | false;

    const char *exitMethod = gf["exitMethod"] | "call";
    const char *enterMethod = gf["enterMethod"] | "sms";
    lg.exitUseCall = (strcmp(exitMethod, "call") == 0);
    lg.enterUseCall = (strcmp(enterMethod, "call") == 0);

    lg.speedLimit = gf["speedLimit"] | 0;
    lg.lastSpeedAlertTime = 0;
    lg.hasState = false;

    localGeofenceCount++;
  }

  Serial.printf("[GEOFENCE] %d حصار دریافت شد (alertPhone: %s, globalSpeedLimit: %u)\n",
                localGeofenceCount, geofenceAlertPhone.c_str(), globalSpeedLimit);
}

void checkLocalGeofences(unsigned long now)
{
  if (now - lastGeofenceCheck < GEOFENCE_CHECK_INTERVAL_MS)
    return;
  lastGeofenceCheck = now;

  if (!gps.hasSignal())
    return;

  GPSData data = gps.getData();

  if (globalSpeedLimit > 0 && data.speed > globalSpeedLimit)
  {
    if (geofenceAlertPhone.length() >= 5 &&
        (lastGlobalSpeedAlertTime == 0 || now - lastGlobalSpeedAlertTime >= SPEED_ALERT_COOLDOWN_MS))
    {
      char msg[100];
      snprintf(msg, sizeof(msg), "GPS Tracker: سرعت %.0f km/h - بیشتر از حد مجاز (%u)", data.speed, globalSpeedLimit);
      sim800.sendSMS(geofenceAlertPhone.c_str(), msg);
      lastGlobalSpeedAlertTime = now;
      Serial.printf("[SPEED] ⚠ سرعت سراسری رد شد: %.1f > %u\n", data.speed, globalSpeedLimit);
    }
  }

  if (localGeofenceCount == 0)
    return;

  for (uint8_t i = 0; i < localGeofenceCount; i++)
  {
    LocalGeofence &gf = localGeofences[i];
    double dist = distanceMeters(data.lat, data.lng, gf.lat, gf.lng);
    bool isInside = dist <= (double)gf.radius;

    if (!gf.hasState)
    {
      gf.wasInside = isInside;
      gf.hasState = true;
      continue;
    }

    if (gf.wasInside && !isInside && gf.alertOnExit)
    {
      Serial.printf("[GEOFENCE] ⛔ خروج از %s (%s)\n", gf.id.c_str(), gf.exitUseCall ? "تماس" : "پیامک");
      if (geofenceAlertPhone.length() >= 5)
      {
        if (gf.exitUseCall)
        {
          startCallAlert(geofenceAlertPhone);
        }
        else
        {
          String msg = "GPS Tracker: خروج از محدوده " + gf.id;
          sim800.sendSMS(geofenceAlertPhone.c_str(), msg.c_str());
        }
      }
    }
    else if (!gf.wasInside && isInside && gf.alertOnEnter)
    {
      Serial.printf("[GEOFENCE] ✓ ورود به %s (%s)\n", gf.id.c_str(), gf.enterUseCall ? "تماس" : "پیامک");
      if (geofenceAlertPhone.length() >= 5)
      {
        if (gf.enterUseCall)
        {
          startCallAlert(geofenceAlertPhone);
        }
        else
        {
          String msg = "GPS Tracker: ورود به محدوده " + gf.id;
          sim800.sendSMS(geofenceAlertPhone.c_str(), msg.c_str());
        }
      }
    }

    if (isInside && gf.speedLimit > 0 && data.speed > gf.speedLimit)
    {
      if (geofenceAlertPhone.length() >= 5 &&
          (gf.lastSpeedAlertTime == 0 || now - gf.lastSpeedAlertTime >= SPEED_ALERT_COOLDOWN_MS))
      {
        char msg[120];
        snprintf(msg, sizeof(msg), "GPS Tracker: سرعت %.0f km/h در محدوده %s - بیشتر از حد مجاز (%u)",
                 data.speed, gf.id.c_str(), gf.speedLimit);
        sim800.sendSMS(geofenceAlertPhone.c_str(), msg);
        gf.lastSpeedAlertTime = now;
        Serial.printf("[SPEED] ⚠ سرعت در %s رد شد: %.1f > %u\n", gf.id.c_str(), data.speed, gf.speedLimit);
      }
    }

    gf.wasInside = isInside;
  }
}

void startCallAlert(const String &phone)
{
  if (callState != CallAlertState::IDLE)
  {
    Serial.println(F("[CALL] یک هشدار تماس قبلاً در حال انجامه - این یکی رد شد"));
    return;
  }

  callTargetPhone = phone;
  callAttemptsRemaining = CALL_TOTAL_ATTEMPTS;
  callStateStart = millis();
  callState = CallAlertState::DIALING;

  sim800.pauseProcessing(true);
  Serial.printf("[CALL] شروع هشدار تماس به %s (%d تلاش)\n", phone.c_str(), CALL_TOTAL_ATTEMPTS);
}

void updateCallAlertStateMachine(unsigned long now)
{
  static char atResp[160];

  switch (callState)
  {
  case CallAlertState::IDLE:
    return;

  case CallAlertState::DIALING:
  {
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "ATD%s;", callTargetPhone.c_str());
    bool ok = sim800.sendRawAT(cmd, atResp, sizeof(atResp), 5000);

    Serial.printf("[CALL] دایال به %s -> %s\n", callTargetPhone.c_str(), ok ? "OK" : "بدون پاسخ فوری");

    callStateStart = now;
    callState = CallAlertState::RINGING;
    break;
  }

  case CallAlertState::RINGING:
    if (now - callStateStart >= CALL_RING_DURATION_MS)
    {
      sim800.sendRawAT("ATH", atResp, sizeof(atResp), 3000);
      Serial.println(F("[CALL] قطع شد (پایان زنگ)"));
      callStateStart = now;
      callState = CallAlertState::HANGING_UP;
    }
    break;

  case CallAlertState::HANGING_UP:
    if (now - callStateStart >= 3000)
    {
      Serial.println(F("[CALL] تماس قطع شد"));
      callAttemptsRemaining--;
      if (callAttemptsRemaining > 0)
      {
      callStateStart = now;
      callState = CallAlertState::WAIT_RETRY;
      Serial.printf("[CALL] remaining retries: %d\n", callAttemptsRemaining);
      }
      else
      {
        sim800.pauseProcessing(false);
        callState = CallAlertState::IDLE;
        Serial.println(F("[CALL] هشدار تماس پایان یافت"));
      }
    }
    break;

  case CallAlertState::WAIT_RETRY:
    if (now - callStateStart >= CALL_RETRY_GAP_MS)
    {
      callStateStart = now;
      callState = CallAlertState::DIALING;
      Serial.printf("[CALL] تلاش مجدد %d/%d\n",
                    callAttemptsRemaining, CALL_TOTAL_ATTEMPTS);
    }
    break;
  }
}

// ════════════════════════════════════════════════════════════════
// ═══ AUTH HANDLERS ═══
// ════════════════════════════════════════════════════════════════

void handleAuthVerify(const String &phone, const String &enteredCode)
{
  Serial.printf("[AUTH DEBUG] verify رسید - state فعلی: %d (phone:%s code:%s)\n",
                (int)authState, phone.c_str(), enteredCode.c_str());

  if (authState != AuthState::WAITING_SMS_RESULT)
  {
    Serial.println(F("[AUTH] هیچ درخواست فعالی برای verify وجود ندارد"));
    StaticJsonDocument<128> doc;
    doc["phone"] = phone;
    doc["status"] = "error";
    doc["message"] = "no_active_request";
    publishSafe(TOPIC_AUTH_RESULT, doc);
    return;
  }

  if (phone != authPendingPhone)
  {
    Serial.printf("[AUTH] شماره‌ها مطابقت ندارند: %s vs %s\n", phone.c_str(), authPendingPhone.c_str());
    StaticJsonDocument<128> doc;
    doc["phone"] = phone;
    doc["status"] = "error";
    doc["message"] = "wrong_phone";
    publishSafe(TOPIC_AUTH_RESULT, doc);
    return;
  }

  if (enteredCode == authPendingCode)
  {
    Serial.println(F("[AUTH] ✓ کد صحیح است - جلسه احراز فعال شد"));
    authSessionValid = true;
    authSessionStart = millis();

    StaticJsonDocument<256> doc;
    doc["phone"] = phone;
    doc["status"] = "approved";
    doc["session_valid_sec"] = AUTH_SESSION_TIMEOUT_MS / 1000;
    publishSafe(TOPIC_AUTH_RESULT, doc);

    // ⚠️ اصلاح بزرگ: حذف پیامک تاییدیه یونیکد فارسی برای جلوگیری از قفل شدن ۲۶ ثانیه‌ای خط UART
    // و قطع شدن سوکت اینترنت GPRS ردیاب. تاییدیه در وب‌سایت به کاربر نشان داده می‌شود.

    resetAuthState(); // موفقیت: پاک‌سازی متغیرها و NVS
  }
  else
  {
    authFailedAttempts++;
    Serial.printf("[AUTH] ✗ کد اشتباه است (تلاش %d/%d)\n", authFailedAttempts, MAX_AUTH_ATTEMPTS);

    StaticJsonDocument<128> doc;
    doc["phone"] = phone;
    doc["status"] = "rejected";
    doc["message"] = "wrong_code";
    doc["attempts_left"] = MAX_AUTH_ATTEMPTS - authFailedAttempts;
    publishSafe(TOPIC_AUTH_RESULT, doc);

    if (authFailedAttempts >= MAX_AUTH_ATTEMPTS)
    {
      Serial.println(F("[AUTH] ⚠ تعداد تلاش‌های اشتباه به حد مجاز رسید - جلسه بسته شد"));
      // ارسال پیامک هشدار انگلیسی بسیار سریع (ASCII) بدون بلاک کردن شبکه
      sim800.sendSMS(phone.c_str(), "GPS Tracker: Too many wrong OTP attempts.");
      resetAuthState();
    }
    else
    {
      // ارسال پیامک خطای انگلیسی بسیار سریع (ASCII) بدون بلاک کردن شبکه
      sim800.sendSMS(phone.c_str(), "GPS Tracker: Wrong code entered.");
    }
  }
}
void updateAuthStateMachine(unsigned long now)
{
  switch (authState)
  {
  case AuthState::IDLE:
    return;

  case AuthState::PUBLISHING_PENDING:
  {
    // ⚠️ به‌جای delay() - صبر کاملاً غیربلاکینگ
    if (now - authPublishStart < AUTH_PUBLISH_SETTLE_MS)
      return;

    authStateStart = now;
    authState = AuthState::WAITING_SMS_RESULT;

    bool ok = sim800.sendSMS(authPendingPhone.c_str(), authPendingCode.c_str());
    authSmsAccepted = ok;

    Serial.printf("[AUTH] SMS OTP به %s: %s\n", authPendingPhone.c_str(), ok ? "ارسال شد" : "ارسال ناموفق");

    StaticJsonDocument<128> doc;
    doc["phone"] = authPendingPhone;
    doc["sms_status"] = ok ? "sent" : "failed";
    publishSafe(TOPIC_AUTH_CODE, doc);
    break;
  }

  case AuthState::WAITING_SMS_RESULT:
  {
    if (now - authStateStart >= AUTH_SMS_TIMEOUT_MS)
    {
      Serial.println(F("[AUTH] ✗ Timeout در انتظار ورود کد OTP از سمت کاربر"));

      StaticJsonDocument<128> doc;
      doc["phone"] = authPendingPhone;
      doc["status"] = "timeout";
      doc["message"] = "sms_timeout";
      publishSafe(TOPIC_AUTH_RESULT, doc);

      sim800.sendSMS(authPendingPhone.c_str(), "GPS Tracker: زمان ورود کد تایید به پایان رسید. لطفاً دوباره تلاش کنید.");

      resetAuthState();
    }
    break;
  }
  }
}

// ════════════════════════════════════════════════════════════════
// ═══ RELAY ═══
// ════════════════════════════════════════════════════════════════

bool isValidRelayAction(const char *action)
{
  return (strcmp(action, "on") == 0) ||
         (strcmp(action, "off") == 0) ||
         (strcmp(action, "toggle") == 0) ||
         (strcmp(action, "pulse_1s") == 0) ||
         (strcmp(action, "pulse_5s") == 0) ||
         (strcmp(action, "pulse_30s") == 0);
}

void handleRelayCommand(const char *action)
{
  if (!isAuthSessionValid())
  {
    Serial.println(F("[RELAY] دستور رد شد - جلسه auth معتبر نیست"));
    return;
  }

  Serial.printf("[RELAY] اجرا: %s\n", action);
  relay.handleCommand(action);

  // ✅ ارسال وضعیت رله به اندروید
  StaticJsonDocument<128> statusDoc;
  statusDoc["status"] = "executed";
  statusDoc["action"] = action;
  statusDoc["engine"] = relay.getState();
  statusDoc["timestamp"] = millis();
  publishSafe(TOPIC_RELAY_STATUS, statusDoc);
  Serial.printf("[RELAY] ✓ Status published: engine=%s\n", relay.getState() ? "ON" : "OFF");
}

// ════════════════════════════════════════════════════════════════
// ═══ GEOFENCE ALERT ═══
// ════════════════════════════════════════════════════════════════

void handleGeofenceAlert(const char *action, const char *geofenceName)
{
  if (!isAuthSessionValid())
  {
    Serial.println(F("[GEOFENCE] رد شد - جلسه معتبر نیست"));
    return;
  }

  Serial.printf("[GEOFENCE] %s برای %s\n", action, geofenceName);
}

// ════════════════════════════════════════════════════════════════
// ═══ ALERT BLINK ═══
// ════════════════════════════════════════════════════════════════

void startAlertBlink()
{
  alertBlinkActive = true;
  alertBlinkToggleCount = 0;
  alertBlinkLastToggle = millis();
  digitalWrite(LED_STATUS, HIGH);
}

void updateAlertBlink(unsigned long now)
{
  if (!alertBlinkActive)
    return;

  if (now - alertBlinkLastToggle >= ALERT_BLINK_INTERVAL_MS)
  {
    alertBlinkLastToggle = now;
    alertBlinkToggleCount++;

    if (alertBlinkToggleCount >= ALERT_BLINK_TOGGLES)
    {
      alertBlinkActive = false;
      digitalWrite(LED_STATUS, LOW);
      return;
    }

    bool state = (alertBlinkToggleCount % 2 == 1);
    digitalWrite(LED_STATUS, state ? HIGH : LOW);
  }
}

// ════════════════════════════════════════════════════════════════
// ═══ PRINT ═══
// ════════════════════════════════════════════════════════════════

void printBanner()
{
  Serial.println(F("\n════════════════════════════════════"));
  Serial.println(F("  GPS Tracker v1.0"));
  Serial.println(F("════════════════════════════════════\n"));
}

void printSystemStatus()
{
  Serial.println(F("\n┌─────────────────────────────────┐"));
  Serial.println(F("│    SYSTEM STATUS                │"));
  Serial.println(F("├─────────────────────────────────┤"));
  Serial.printf("│ Uptime: %lu s\n", millis() / 1000);
  Serial.printf("│ GPRS: %s\n", sim800.isGprsConnected() ? "✓" : "✗");
  Serial.printf("│ MQTT: %s\n", mqtt.isConnected() ? "✓" : "✗");
  Serial.printf("│ GPS: %s\n", gps.hasSignal() ? "✓" : "✗");
  Serial.printf("│ Relay: %s\n", relay.getState() ? "ON" : "OFF");
  Serial.printf("│ Signal: %d/31\n", sim800.getSignalQuality());
  Serial.printf("│ Ignition: %s\n", digitalRead(IGNITION_PIN) ? "HIGH (1)" : "LOW (0)");
  Serial.printf("│ Buffer backlog: %u pending\n", locBuffer.pendingCount());
  Serial.printf("│ Failed pub: %lu\n", (unsigned long)failedPublishCount);
  Serial.printf("│ FW Version: %s\n", FIRMWARE_VERSION);
  Serial.printf("│ OTA State: %s\n", ota.getStateString());
  if (ota.isInProgress())
    Serial.printf("│ OTA Progress: %u%%\n", ota.getProgress());
  Serial.println(F("└─────────────────────────────────┘\n"));
}