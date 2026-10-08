#ifndef GPS_MANAGER_H
#define GPS_MANAGER_H

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include "config.h"

struct GPSData
{
    double lat;
    double lng;
    float speed;
    float altitude;
    float course;
    int satellites;
    float hdop;
    bool valid;
    bool hasSignal;
    bool stale; // true اگه داده قدیمی باشه (فیکس معتبر ولی قدیمی)
    unsigned long lastUpdate;
};

class GPSManager
{
private:
    TinyGPSPlus _gps;
    HardwareSerial *_serial;
    GPSData _data;

    // ── فیلتر جهش (Jump Filter) با الگوی تایید-قبل-از-پذیرش ──
    bool _hasAnchor;
    double _anchorLat;
    double _anchorLng;
    unsigned long _anchorMillis;
    double _lastSpeedKmph; // سرعت ضمنی آخرین فیکس تاییدشده (برای فیلتر شتاب)

    bool _hasPending;
    double _pendingLat;
    double _pendingLng;
    unsigned long _pendingMillis;

    // ── فیلتر EMA (هموارسازی نمایی) ──
    bool _emaInitialized;
    double _emaLat;
    double _emaLng;

    static double haversineMeters(double lat1, double lng1, double lat2, double lng2);
    bool passesJumpFilter(double lat, double lng, unsigned long nowMs);
    bool passesQualityFilter();
    void applyEMA(double lat, double lng);

public:
    GPSManager();

    void begin(HardwareSerial *serial);
    void loop();

    GPSData getData();
    bool hasSignal();
    bool isValid();

    bool getUnixTimestamp(uint32_t &outTs);

    void printStatus();
};

#endif