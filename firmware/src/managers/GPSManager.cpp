#include "GPSManager.h"

GPSManager::GPSManager()
{
    _serial = nullptr;

    _data.lat = DEFAULT_LAT;
    _data.lng = DEFAULT_LNG;
    _data.speed = 0;
    _data.altitude = 0;
    _data.course = 0;
    _data.satellites = 0;
    _data.hdop = 99.0f;
    _data.valid = false;
    _data.hasSignal = false;
    _data.stale = false;
    _data.lastUpdate = 0;

    _hasAnchor = false;
    _anchorLat = 0;
    _anchorLng = 0;
    _anchorMillis = 0;
    _lastSpeedKmph = 0;

    _hasPending = false;
    _pendingLat = 0;
    _pendingLng = 0;
    _pendingMillis = 0;

    _emaInitialized = false;
    _emaLat = 0;
    _emaLng = 0;
}

void GPSManager::begin(HardwareSerial *serial)
{
    _serial = serial;
    _serial->begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX, GPS_TX);

    GPS_LOGLN(F("[GPS] Initialized"));
}

// ── فاصله‌ی هاورساین بین دو مختصات (متر) ──
double GPSManager::haversineMeters(double lat1, double lng1, double lat2, double lng2)
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

// ── فیلتر کیفیت: حداقل ماهواره، حداکثر HDOP، حداکثر عمر فیکس ──
bool GPSManager::passesQualityFilter()
{
    if (_gps.satellites.value() < GPS_MIN_SATELLITES)
    {
        GPS_LOG(F("[GPS] ⚡ رد شد: سات‌ها="));
        GPS_LOGLN(_gps.satellites.value());
        return false;
    }

    if (_gps.hdop.isValid())
    {
        // hdop.value() مقیاسش ×100 است → تبدیل به float واقعی
        float hdopReal = _gps.hdop.value() / 100.0f;

        if (hdopReal > GPS_MAX_HDOP)
        {
            GPS_LOG(F("[GPS] ⚡ رد شد: HDOP="));
            GPS_LOGLN(hdopReal);
            return false;
        }
    }

    return true;
}

// ── فیلتر جهش با الگوی "تایید قبل از پذیرش" + فیلتر شتاب ──
bool GPSManager::passesJumpFilter(double lat, double lng, unsigned long nowMs)
{
    if (!_hasAnchor)
    {
        _hasAnchor = true;
        _anchorLat = lat;
        _anchorLng = lng;
        _anchorMillis = nowMs;
        _lastSpeedKmph = 0;
        _hasPending = false;
        return true;
    }

    unsigned long dt = nowMs - _anchorMillis;

    // فاصله‌ی زمانی خیلی کم - نمی‌شه سرعت رو قابل‌اعتماد حساب کرد، قبول کن
    if (dt < GPS_JUMP_MIN_DT_MS)
    {
        _anchorLat = lat;
        _anchorLng = lng;
        _anchorMillis = nowMs;
        _hasPending = false;
        return true;
    }

    double dist = haversineMeters(_anchorLat, _anchorLng, lat, lng);
    double impliedSpeedKmph = (dist / (double)dt) * 3600.0; // متر/میلی‌ثانیه → کیلومتر/ساعت

    // ── چک شتاب: تغییر سرعت بیش از حد → مشکوک ──
    double dv = impliedSpeedKmph - _lastSpeedKmph;
    double accelMps2 = (dv / 3.6) / ((double)dt / 1000.0); // km/h→m/s سپس /s²
    if (accelMps2 > GPS_JUMP_MAX_ACCEL_MPS2)
    {
        // شتاب غیرمعقول - مثل جهش رفتار کن
        goto suspect_jump;
    }

    if (impliedSpeedKmph <= GPS_JUMP_MAX_PLAUSIBLE_KMPH)
    {
        _anchorLat = lat;
        _anchorLng = lng;
        _anchorMillis = nowMs;
        _lastSpeedKmph = impliedSpeedKmph;
        _hasPending = false;
        return true;
    }

suspect_jump:
    // ── سرعت یا شتاب غیرمعقول - مشکوک به جهش کاذب ──
    if (_hasPending)
    {
        double distFromPending = haversineMeters(_pendingLat, _pendingLng, lat, lng);
        if (distFromPending <= GPS_JUMP_CONFIRM_RADIUS_M)
        {
            GPS_LOGLN(F("[GPS] ✓ جهش با فیکس بعدی تایید شد - پذیرفته شد"));
            _anchorLat = lat;
            _anchorLng = lng;
            _anchorMillis = nowMs;
            _lastSpeedKmph = impliedSpeedKmph;
            _hasPending = false;
            return true;
        }
    }

    // فیکس مشکوک جدید رو به‌عنوان pending نگه دار و رد کن
    _pendingLat = lat;
    _pendingLng = lng;
    _pendingMillis = nowMs;
    _hasPending = true;

    GPS_LOG(F("[GPS] ⚠ جهش مشکوک رد شد - سرعت ضمنی: "));
    GPS_LOG(impliedSpeedKmph, 0);
    GPS_LOG(F(" km/h | شتاب: "));
    GPS_LOG(accelMps2, 1);
    GPS_LOGLN(F(" m/s²"));

    return false;
}

// ── هموارسازی نمایی (EMA) روی مختصات ──
void GPSManager::applyEMA(double lat, double lng)
{
    if (!_emaInitialized)
    {
        _emaLat = lat;
        _emaLng = lng;
        _emaInitialized = true;
    }
    else
    {
        _emaLat = _emaLat + GPS_EMA_ALPHA * (lat - _emaLat);
        _emaLng = _emaLng + GPS_EMA_ALPHA * (lng - _emaLng);
    }
}

void GPSManager::loop()
{
    if (!_serial)
        return;

    // ── خواندن داده از GPS با محدودیت بایت در هر loop ──
    int bytesRead = 0;
    while (_serial->available() > 0 && bytesRead < GPS_MAX_SERIAL_BYTES_PER_LOOP)
    {
        _gps.encode(_serial->read());
        bytesRead++;
    }

    // بروزرسانی داده‌ها
    if (_gps.location.isUpdated())
    {
        bool isValidFix = _gps.location.isValid();
        double newLat = _gps.location.lat();
        double newLng = _gps.location.lng();
        bool looksLikeNullIsland = (fabs(newLat) < 0.5 && fabs(newLng) < 0.5);

        // اگه سیگنال قبلاً قطع بود، انکر جهش رو ریست کن
        bool signalWasLost = !_data.hasSignal;
        if (signalWasLost)
        {
            _hasAnchor = false;
            _hasPending = false;
            _emaInitialized = false;
        }

        if (isValidFix && !looksLikeNullIsland)
        {
            // ── فیلتر کیفیت ──
            if (!passesQualityFilter())
            {
                return;
            }

            unsigned long nowMs = millis();

            // ── فیلتر جهش ──
            if (passesJumpFilter(newLat, newLng, nowMs))
            {
                // ── هموارسازی EMA ──
                applyEMA(newLat, newLng);

                _data.lat = _emaLat;
                _data.lng = _emaLng;
                _data.speed = _gps.speed.kmph();
                _data.altitude = _gps.altitude.meters();
                _data.course = _gps.course.deg();
                _data.satellites = _gps.satellites.value();
                _data.hdop = _gps.hdop.isValid() ? _gps.hdop.value() : 99.0f;
                _data.valid = true;
                _data.stale = false;
                _data.lastUpdate = nowMs;
                _data.hasSignal = true;

                GPS_LOG(F("[GPS] ✓ "));
                GPS_LOG(_data.lat, 6);
                GPS_LOG(F(", "));
                GPS_LOG(_data.lng, 6);
                GPS_LOG(F(" | Spd: "));
                GPS_LOG(_data.speed);
                GPS_LOG(F(" | Sats: "));
                GPS_LOG(_data.satellites);
                GPS_LOG(F(" | HDOP: "));
                GPS_LOGLN(_data.hdop);
            }
            // اگه رد شد، _data دست‌نخورده می‌مونه
        }
        else
        {
            GPS_LOG(F("[GPS] ⚠ آپدیت نامعتبر رد شد (valid="));
            GPS_LOG(isValidFix);
            GPS_LOG(F(", nullIsland="));
            GPS_LOG(looksLikeNullIsland);
            GPS_LOG(F(", lat="));
            GPS_LOG(newLat, 4);
            GPS_LOG(F(", lng="));
            GPS_LOG(newLng, 4);
            GPS_LOGLN(F(")"));
        }
    }

    // ── چک کردن timeout ──
    if (_data.hasSignal && (millis() - _data.lastUpdate > GPS_TIMEOUT))
    {
        _data.hasSignal = false;
        _data.valid = false;
        GPS_LOGLN(F("[GPS] ✗ Signal lost (timeout)"));
    }

    // ── چک stale: فیکس قدیمی ولی هنوز داخل timeout ──
    if (_data.hasSignal && (millis() - _data.lastUpdate > GPS_MAX_FIX_AGE_MS))
    {
        _data.stale = true;
    }
}

GPSData GPSManager::getData()
{
    if (!_data.hasSignal)
    {
        GPSData lastKnown = _data;
        lastKnown.speed = 0;
        lastKnown.satellites = 0;
        return lastKnown;
    }

    return _data;
}

bool GPSManager::hasSignal()
{
    return _data.hasSignal;
}

bool GPSManager::isValid()
{
    return _data.valid;
}

bool GPSManager::getUnixTimestamp(uint32_t &outTs)
{
    if (!_gps.date.isValid() || !_gps.time.isValid())
        return false;

    int year = _gps.date.year();
    int month = _gps.date.month();
    int day = _gps.date.day();
    int hour = _gps.time.hour();
    int minute = _gps.time.minute();
    int second = _gps.time.second();

    if (year < 2020 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31)
        return false;

    static const uint16_t cumulativeDays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

    auto isLeap = [](int y)
    {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    };

    uint32_t days = 0;
    for (int y = 1970; y < year; y++)
        days += isLeap(y) ? 366 : 365;

    days += cumulativeDays[month - 1];
    if (month > 2 && isLeap(year))
        days += 1;
    days += (uint32_t)(day - 1);

    outTs = days * 86400UL + (uint32_t)hour * 3600UL + (uint32_t)minute * 60UL + (uint32_t)second;
    return true;
}

void GPSManager::printStatus()
{
    Serial.println(F("\n╔═══════════════════════════════════╗"));
    Serial.println(F("║         GPS STATUS                ║"));
    Serial.println(F("╠═══════════════════════════════════╣"));

    Serial.print(F("║ Signal:     "));
    Serial.println(_data.hasSignal ? "✓ OK      " : "✗ LOST   ");

    Serial.print(F("║ Valid:      "));
    Serial.println(_data.valid ? "✓ YES     " : "✗ NO      ");

    Serial.print(F("║ Stale:      "));
    Serial.println(_data.stale ? "⚠ YES     " : "✓ NO      ");

    Serial.print(F("║ Satellites: "));
    Serial.print(_data.satellites);
    Serial.println(F("         "));

    Serial.print(F("║ HDOP:       "));
    Serial.print(_data.hdop, 1);
    Serial.println(F("         "));

    Serial.print(F("║ Position:   "));
    Serial.print(_data.lat, 4);
    Serial.print(F(", "));
    Serial.println(_data.lng, 4);

    Serial.print(F("║ Speed:      "));
    Serial.print(_data.speed, 1);
    Serial.println(F(" km/h   "));

    Serial.println(F("╚═══════════════════════════════════╝\n"));
}