#ifndef LOCATION_BUFFER_H
#define LOCATION_BUFFER_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// نقطه‌ی بافرشده - ترتیب فیلدها عمدی است (double ها اول) تا padding کمتر بشه
struct BufferedPoint
{
    double lat;
    double lng;
    uint32_t seq;
    uint32_t ts; // unix timestamp واقعی (UTC) از GPS
    float speed;
};

class LocationBuffer
{
private:
    BufferedPoint _ring[LOC_BUFFER_RAM_CAPACITY];
    uint8_t _ringStart; // ایندکس قدیمی‌ترین عنصر
    uint8_t _ringCount;

    Preferences _prefs;
    uint32_t _nextSeq;
    uint32_t _seqCeiling;

    bool _flashReady;
    uint16_t _flashCount;

    uint32_t allocSeq();
    void flushOldestToFlash(uint8_t count);
    uint16_t countFlashRecords();
    void compactFlashUpTo(uint32_t confirmedSeq);
    void purgeRamUpTo(uint32_t confirmedSeq);

public:
    LocationBuffer();

    void begin();

    // یک نقطه‌ی جدید به بافر backlog اضافه می‌کند (seq خودکار تخصیص داده می‌شود)
    void addPoint(double lat, double lng, float speed, uint32_t ts);

    bool hasBacklog();
    uint16_t pendingCount();
    uint32_t oldestSeq();
    uint32_t newestSeq();

    // بعدی‌ترین دسته از نقاط تاییدنشده رو (بدون حذف) برمی‌گردونه
    uint8_t getNextBatch(BufferedPoint *out, uint8_t maxCount);

    // نقاطی که seq شون <= confirmedSeq است رو از RAM و فلش حذف می‌کند
    void confirmUpTo(uint32_t confirmedSeq);
};

#endif