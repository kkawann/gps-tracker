#ifndef CONFIG_H
#define CONFIG_H

// Private overrides are intentionally excluded from Git.
#if __has_include("local_config.h")
#include "local_config.h"
#endif

// ════════════════════════════════════════════════════════════════
// ═══ شبکه و MQTT ═══
// ════════════════════════════════════════════════════════════════

#ifndef GPRS_APN
#define GPRS_APN "mcinet"
#endif
#ifndef GPRS_USER
#define GPRS_USER ""
#endif
#ifndef GPRS_PASS
#define GPRS_PASS ""
#endif

#ifndef MQTT_SERVER
#define MQTT_SERVER "gps.example.com"
#endif
#define MQTT_PORT 1883
#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID "esp32_device_1"
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASS
#define MQTT_PASS ""
#endif

#ifndef DEVICE_ID
#define DEVICE_ID "device_1"
#endif

// ════════════════════════════════════════════════════════════════
// ═══ شماره تماس برای هشدارهای سیستمی ═══
// ════════════════════════════════════════════════════════════════
#ifndef SYSTEM_ALERT_PHONE
#define SYSTEM_ALERT_PHONE ""
#endif

// ════════════════════════════════════════════════════════════════
// ═══ هشدار قطعی و وصل مجدد شبکه ═══
// ════════════════════════════════════════════════════════════════
#define OFFLINE_THRESHOLD_MS 300000 // 5 دقیقه - اگر این مدت آفلاین بود و وصل شد، پیامک بده

// ════════════════════════════════════════════════════════════════
// ═══ تاپیک‌ها ═══
// ════════════════════════════════════════════════════════════════

#define TOPIC_LOCATION "gps/" DEVICE_ID "/location"
#define TOPIC_RELAY_CMD "gps/" DEVICE_ID "/relay/command"
#define TOPIC_RELAY_STATUS "gps/" DEVICE_ID "/relay/status"
#define TOPIC_AUTH_REQ "gps/" DEVICE_ID "/auth/req"
#define TOPIC_AUTH_CODE "gps/" DEVICE_ID "/auth/code"
#define TOPIC_AUTH_VERIFY "gps/" DEVICE_ID "/auth/verify"
#define TOPIC_AUTH_RESULT "gps/" DEVICE_ID "/auth/result"
#define TOPIC_GEOFENCE_ALERT "gps/" DEVICE_ID "/geofence/alert"
#define TOPIC_HEARTBEAT "gps/" DEVICE_ID "/heartbeat"
#define TOPIC_GEOFENCE_CONFIG "gps/" DEVICE_ID "/geofence/config"

// ⚠️ جدید - هندشیک sync برای جبران داده‌های ازدست‌رفته حین قطعی اینترنت
#define TOPIC_SYNC_HELLO "gps/" DEVICE_ID "/sync/hello"
#define TOPIC_SYNC_ACK "gps/" DEVICE_ID "/sync/ack"
#define TOPIC_LOCATION_BACKLOG "gps/" DEVICE_ID "/location/backlog"

// ════════════════════════════════════════════════════════════════
// ═══ پین‌ها (ESP32-C3 Super Mini) ═══
// ════════════════════════════════════════════════════════════════

#define SIM800_RX 2  // GPIO20 (U0RXD)
#define SIM800_TX 3  // GPIO21 (U0TXD)
#define SIM800_RST 4 // GPIO10

#define GPS_RX 5 // GPIO4
#define GPS_TX 6 // GPIO5

#define RELAY_PIN 1    // GPIO1 (ESP32-C3 physical pin)
#define LED_STATUS 10  // GPIO10 (onboard LED)
#define IGNITION_PIN 0 // GPIO9
#define VOLTAGE_PIN 7  // GPIO0 (ADC)

// ════════════════════════════════════════════════════════════════
// ═══ تایمینگ ═══
// ════════════════════════════════════════════════════════════════

#define GPS_UPDATE_INTERVAL 3000   // 3 ثانیه
#define HEARTBEAT_INTERVAL 30000   // 30 ثانیه
#define GPS_TIMEOUT 10000          // 10 ثانیه
#define MQTT_RECONNECT_DELAY 10000 // 10 ثانیه (پایه‌ی backoff نمایی در MQTTManager)
#define SIM800_PROCESS_INTERVAL 50 // 50 میلی‌ثانیه

// ════════════════════════════════════════════════════════════════
// ═══ بافر آفلاین GPS (RAM + LittleFS) و Sync Handshake ═══
// ════════════════════════════════════════════════════════════════

#define LOC_BUFFER_RAM_CAPACITY 40          // ظرفیت بافر حلقه‌ای در RAM (تعداد نقطه)
#define LOC_BUFFER_APPEND_INTERVAL_MS 15000 // هر چند وقت یک نقطه به بافر backlog اضافه بشه (مستقل از پابلیش زنده)
#define LOC_BUFFER_FLASH_FILE "/locbuf.dat" // فایل append-only روی LittleFS برای نقاط قدیمی‌تر
#define LOC_BUFFER_FLASH_FLUSH_CHUNK 15     // وقتی RAM پر شد، این تعداد نقطه‌ی قدیمی‌تر یکجا به فلش منتقل می‌شه
#define LOC_BUFFER_BACKLOG_BATCH_SIZE 10    // هر پیام sync حداکثر چند نقطه بفرسته
#define SEQ_RESERVE_BLOCK 50                // هر بار چند شماره seq از قبل در NVS رزرو بشه (کاهش wear)

#define SYNC_RETRY_INTERVAL_MS 5000 // فاصله‌ی هر تلاش sync وقتی backlog داریم
#define SYNC_ACK_TIMEOUT_MS 8000    // اگه سرور تو این مدت جواب نده، دور بعد دوباره تلاش می‌کنیم

// ════════════════════════════════════════════════════════════════
// ═══ OTA (Over-The-Air Update) ═══
// ════════════════════════════════════════════════════════════════
// ── OTA - دانلود تکه‌ای (Segmented) ──
#define OTA_SEGMENT_COUNT 5          // تعداد بخش‌های دانلود (هر بخش ≈20% فایل)
#define OTA_SEGMENT_COOLDOWN_MS 3000 // مکث بین بخش‌ها قبل از اتصال بعدی
#define OTA_MAX_SEGMENT_RETRIES 3    // حداکثر تلاش مجدد به‌ازای هر بخش

// ── OTA - چک خودکار آخرین نسخه ──
#define OTA_CHECK_ENDPOINT_PATH "/api/firmware/latest"
#define OTA_CHECK_INTERVAL_MS (6UL * 60UL * 60UL * 1000UL) // هر 6 ساعت
#define OTA_CHECK_TIMEOUT_MS 10000
#define FIRMWARE_VERSION "1.0.0"

// سرور دانلود فایرویر (HTTP)
#ifndef OTA_FW_SERVER_IP
#define OTA_FW_SERVER_IP "firmware.example.com"
#endif
#define OTA_FW_SERVER_PORT 3000
#define OTA_FW_BASE_PATH "/firmware"

// تاپیک‌های MQTT برای OTA
#define TOPIC_OTA_CMD "gps/" DEVICE_ID "/ota/cmd"
#define TOPIC_OTA_STATUS "gps/" DEVICE_ID "/ota/status"

// اندازه chunk دانلود (بایت) - 2KB بهینه برای GSM
#define OTA_CHUNK_SIZE 2048

// حداکثر تعداد retry برای هر chunk
#define OTA_MAX_CHUNK_RETRIES 5

// timeout برای هر HTTP request (ms) - روی GPRS باید بالا باشه
#define OTA_HTTP_TIMEOUT_MS 30000

// فاصله بین retry‌های chunk (ms) - backoff پایه
#define OTA_RETRY_BASE_DELAY_MS 2000

// حداکثر تعداد retry کل فرایند OTA (مثلاً بعد از قطعی کامل شبکه)
#define OTA_MAX_GLOBAL_RETRIES 3

// فاصله بین retry‌های کل (ms)
#define OTA_GLOBAL_RETRY_DELAY_MS 10000

// حداقل سیگنال قابل‌قبول برای شروع OTA
#define OTA_MIN_SIGNAL_QUALITY 8

// فاصله پابلیش progress (ms) - برای جلوگیری از اسپم MQTT روی GPRS
#define OTA_PROGRESS_PUBLISH_INTERVAL_MS 5000

// ════════════════════════════════════════════════════════════════
// ═══ مختصات پیش‌فرض (میدان آزادی تهران) ═══
// ════════════════════════════════════════════════════════════════

#define DEFAULT_LAT 35.6997
#define DEFAULT_LNG 51.3380

// ════════════════════════════════════════════════════════════════
// ═══ دیباگ (اصلاح شده با variadic macros) ═══
// ════════════════════════════════════════════════════════════════

#define DEBUG_GPS 1
#define DEBUG_MQTT 1
#define DEBUG_SIM800 1

#if DEBUG_GPS
#define GPS_LOG(...) Serial.print(__VA_ARGS__)
#define GPS_LOGLN(...) Serial.println(__VA_ARGS__)
#else
#define GPS_LOG(...)
#define GPS_LOGLN(...)
#endif
// ── فیلتر جهش (Jump Filter) ──
#define GPS_JUMP_MAX_PLAUSIBLE_KMPH 250.0f // سقف سرعت معقول (با حاشیه) - بالاتر از این یعنی جهش کاذب
#define GPS_JUMP_MIN_DT_MS 1000            // اگه فاصله‌ی زمانی بین دو فیکس از این کمتر بود، چک سرعت غیرقابل‌اعتماده
#define GPS_JUMP_CONFIRM_RADIUS_M 60.0f    // اگه فیکس بعدی داخل این شعاع از فیکس مشکوک بود، جهش تایید می‌شه
#define GPS_JUMP_MAX_ACCEL_MPS2 10.0f      // حداکثر شتاب معقول (m/s²) - بالاتر از این یعنی جهش کاذب

// ── فیلتر کیفیت فیکس ──
#define GPS_MIN_SATELLITES 4    // حداقل تعداد ماهواره برای پذیرش فیکس
#define GPS_MAX_HDOP 5.0f       // حداکثر HDOP قابل‌قبول (بالاتر = دقت کمتر)
#define GPS_MAX_FIX_AGE_MS 5000 // حداکثر عمر فیکس (ms) - بالاتر از این فیکس stale محسوب می‌شه

// ── فیلتر EMA (هموارسازی نمایی) ──
#define GPS_EMA_ALPHA 0.4f // ضریب هموارسازی (0-1) - کمتر = هموارتر ولی کندتر

// ── باودریت GPS ──
#define GPS_BAUDRATE 9600 // باودریت پیش‌فرض GPS

// ── محدودیت خواندن سریال در هر loop ──
#define GPS_MAX_SERIAL_BYTES_PER_LOOP 256 // حداکثر بایت خوانده‌شده در هر فراخوانی loop

// ── اندازه پنجره‌ی لغزنده برای pending ──
#define GPS_PENDING_WINDOW_SIZE 3 // تعداد فیکس‌های مشکوک نگه‌داری‌شده

#if DEBUG_MQTT
#define MQTT_LOG(...) Serial.print(__VA_ARGS__)
#define MQTT_LOGLN(...) Serial.println(__VA_ARGS__)
#else
#define MQTT_LOG(...)
#define MQTT_LOGLN(...)
#endif

#endif // CONFIG_H