#include "LocationBuffer.h"
#include <LittleFS.h>

LocationBuffer::LocationBuffer()
{
    _ringStart = 0;
    _ringCount = 0;
    _nextSeq = 0;
    _seqCeiling = 0;
    _flashReady = false;
    _flashCount = 0;
}

void LocationBuffer::begin()
{
    _prefs.begin("locbuf", false);
    _seqCeiling = _prefs.getUInt("seqCeiling", 0);
    _nextSeq = _seqCeiling;

    if (!LittleFS.begin(true)) // true = فرمت خودکار اگه mount نشد (اولین بار)
    {
        Serial.println(F("[LOCBUF] ⚠ LittleFS mount ناموفق بود - بافر فلش غیرفعال است"));
        _flashReady = false;
        return;
    }

    _flashReady = true;
    _flashCount = countFlashRecords();

    Serial.printf("[LOCBUF] آماده | nextSeq: %lu | نقاط باقیمانده از قبل در فلش: %u\n",
                  (unsigned long)_nextSeq, _flashCount);
}

// ⚠️ رزرو بلوکی seq: به‌جای نوشتن NVS به‌ازای هر نقطه (wear زیاد)، هر بار یک
// بلوک از قبل رزرو می‌شه. بدترین حالت بعد از ریبوت ناگهانی، فقط چند شماره
// seq پرش می‌کنه (بی‌ضرر) نه اینکه seq تکراری تولید بشه (که دیدوپ سرور رو خراب می‌کرد).
uint32_t LocationBuffer::allocSeq()
{
    if (_nextSeq >= _seqCeiling)
    {
        _seqCeiling += SEQ_RESERVE_BLOCK;
        _prefs.putUInt("seqCeiling", _seqCeiling);
    }
    return _nextSeq++;
}

void LocationBuffer::addPoint(double lat, double lng, float speed, uint32_t ts)
{
    BufferedPoint p;
    p.seq = allocSeq();
    p.lat = lat;
    p.lng = lng;
    p.speed = speed;
    p.ts = ts;

    if (_ringCount >= LOC_BUFFER_RAM_CAPACITY)
    {
        flushOldestToFlash(LOC_BUFFER_FLASH_FLUSH_CHUNK);
    }

    uint8_t writeIdx = (_ringStart + _ringCount) % LOC_BUFFER_RAM_CAPACITY;
    _ring[writeIdx] = p;
    _ringCount++;

    Serial.printf("[LOCBUF] + seq=%lu (رم:%u فلش:%u)\n",
                  (unsigned long)p.seq, _ringCount, _flashCount);
}

void LocationBuffer::flushOldestToFlash(uint8_t count)
{
    if (!_flashReady)
    {
        // فلش در دسترس نیست - مجبوریم قدیمی‌ترین نقاط رو دور بریزیم
        // (بهتر از overwrite بی‌قاعده‌ی RAM یا رشد بی‌نهایت)
        Serial.println(F("[LOCBUF] ⚠ فلش در دسترس نیست - قدیمی‌ترین نقاط دور ریخته شدند"));
        uint8_t drop = min(count, _ringCount);
        _ringStart = (_ringStart + drop) % LOC_BUFFER_RAM_CAPACITY;
        _ringCount -= drop;
        return;
    }

    File f = LittleFS.open(LOC_BUFFER_FLASH_FILE, "a");
    if (!f)
    {
        Serial.println(F("[LOCBUF] ⚠ باز کردن فایل بافر برای append ناموفق بود"));
        return;
    }

    uint8_t moved = min(count, _ringCount);
    for (uint8_t i = 0; i < moved; i++)
    {
        f.write((uint8_t *)&_ring[_ringStart], sizeof(BufferedPoint));
        _ringStart = (_ringStart + 1) % LOC_BUFFER_RAM_CAPACITY;
        _ringCount--;
    }
    f.close();
    _flashCount += moved;

    Serial.printf("[LOCBUF] %u نقطه به فلش منتقل شد (مجموع فلش: %u)\n", moved, _flashCount);
}

uint16_t LocationBuffer::countFlashRecords()
{
    if (!LittleFS.exists(LOC_BUFFER_FLASH_FILE))
        return 0;

    File f = LittleFS.open(LOC_BUFFER_FLASH_FILE, "r");
    if (!f)
        return 0;

    uint16_t n = (uint16_t)(f.size() / sizeof(BufferedPoint));
    f.close();
    return n;
}

bool LocationBuffer::hasBacklog()
{
    return (_flashCount + _ringCount) > 0;
}

uint16_t LocationBuffer::pendingCount()
{
    return _flashCount + _ringCount;
}

uint32_t LocationBuffer::oldestSeq()
{
    if (_flashReady && _flashCount > 0)
    {
        File f = LittleFS.open(LOC_BUFFER_FLASH_FILE, "r");
        if (f)
        {
            BufferedPoint p;
            f.read((uint8_t *)&p, sizeof(BufferedPoint));
            f.close();
            return p.seq;
        }
    }
    if (_ringCount > 0)
        return _ring[_ringStart].seq;
    return 0;
}

uint32_t LocationBuffer::newestSeq()
{
    if (_ringCount > 0)
    {
        uint8_t lastIdx = (_ringStart + _ringCount - 1) % LOC_BUFFER_RAM_CAPACITY;
        return _ring[lastIdx].seq;
    }
    if (_flashReady && _flashCount > 0)
    {
        File f = LittleFS.open(LOC_BUFFER_FLASH_FILE, "r");
        if (f)
        {
            f.seek((_flashCount - 1) * sizeof(BufferedPoint));
            BufferedPoint p;
            f.read((uint8_t *)&p, sizeof(BufferedPoint));
            f.close();
            return p.seq;
        }
    }
    return 0;
}

uint8_t LocationBuffer::getNextBatch(BufferedPoint *out, uint8_t maxCount)
{
    uint8_t filled = 0;

    // اول قدیمی‌ترین نقاط که توی فلش‌اند
    if (_flashReady && _flashCount > 0 && filled < maxCount)
    {
        File f = LittleFS.open(LOC_BUFFER_FLASH_FILE, "r");
        if (f)
        {
            while (filled < maxCount && (uint32_t)f.available() >= sizeof(BufferedPoint))
            {
                f.read((uint8_t *)&out[filled], sizeof(BufferedPoint));
                filled++;
            }
            f.close();
        }
    }

    // بعد نقاط توی RAM که جدیدتر از فلش‌اند
    for (uint8_t i = 0; i < _ringCount && filled < maxCount; i++)
    {
        uint8_t idx = (_ringStart + i) % LOC_BUFFER_RAM_CAPACITY;
        out[filled++] = _ring[idx];
    }

    return filled;
}

void LocationBuffer::purgeRamUpTo(uint32_t confirmedSeq)
{
    while (_ringCount > 0 && _ring[_ringStart].seq <= confirmedSeq)
    {
        _ringStart = (_ringStart + 1) % LOC_BUFFER_RAM_CAPACITY;
        _ringCount--;
    }
}

void LocationBuffer::compactFlashUpTo(uint32_t confirmedSeq)
{
    if (!_flashReady || _flashCount == 0)
        return;

    File f = LittleFS.open(LOC_BUFFER_FLASH_FILE, "r");
    if (!f)
        return;

    // ⚠️ ابتدا یک فایل موقت جدید می‌سازیم با نقاطی که seq > confirmedSeq دارند
    // (یعنی نقاطی که سرور هنوز تأیید نکرده)
    const char *tmpPath = "/locbuf.tmp";

    // اگه فایل موقت از قبل وجود داشته (مثلاً از کرش قبلی)، پاکش می‌کنیم
    if (LittleFS.exists(tmpPath))
        LittleFS.remove(tmpPath);

    File tmp = LittleFS.open(tmpPath, "w");
    if (!tmp)
    {
        f.close();
        Serial.println(F("[LOCBUF] ⚠ باز کردن فایل موقت برای compact ناموفق بود"));
        return;
    }

    BufferedPoint p;
    uint16_t kept = 0;
    while ((uint32_t)f.available() >= sizeof(BufferedPoint))
    {
        f.read((uint8_t *)&p, sizeof(BufferedPoint));
        if (p.seq > confirmedSeq)
        {
            tmp.write((uint8_t *)&p, sizeof(BufferedPoint));
            kept++;
        }
    }
    f.close();
    tmp.close();

    // ⚠️ حالا rename می‌کنیم: فایل tmp جای فایل اصلی رو می‌گیره.
    // اگه crash قبل از این خط بیفته، فایل اصلی سر جاش هست (داده از دست نمی‌ره).
    // اگه crash بعد از rename بیفته، فایل tmp شده فایل اصلی و داده‌ها سالم هستند.
    if (!LittleFS.rename(tmpPath, LOC_BUFFER_FLASH_FILE))
    {
        // rename ناموفق - شاید فایل مقصد وجود داره (روی LittleFS بعضی ports اینجوریه)
        LittleFS.remove(LOC_BUFFER_FLASH_FILE);
        LittleFS.rename(tmpPath, LOC_BUFFER_FLASH_FILE);
    }
    _flashCount = kept;

    Serial.printf("[LOCBUF] compact شد: %u نقطه باقی ماند (تأییدشده تا seq=%lu)\n", kept, (unsigned long)confirmedSeq);
}

void LocationBuffer::confirmUpTo(uint32_t confirmedSeq)
{
    if (confirmedSeq == 0)
        return;

    compactFlashUpTo(confirmedSeq);
    purgeRamUpTo(confirmedSeq);
}