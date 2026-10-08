# GPS Tracker v2.0 — Aurora Design System

بازطراحی کامل UI/UX از صفر با تم **Aurora**: فضای کیهانی تیره + لهجه‌های نئونی + سطوح شیشه‌ای (Glassmorphism)

## سیستم دیزاین

| عنصر | مشخصات |
|---|---|
| پس‌زمینه | `#050A14` (فضای عمیق) + هاله‌های رادیال آبی/بنفش/سبز |
| لهجه اصلی | Cyan `#22D3EE` · Blue `#3B82F6` · Green `#34D399` |
| وضعیت‌ها | موفق `#34D399` · هشدار `#FBBF24` · خطر `#FF4D67` |
| فونت | Vazirmatn (Regular/Medium/SemiBold/Bold) |
| کارت‌ها | شیشه‌ای با گوشه 20-30dp و حاشیه 15% سفید |

## Custom Views انیمیشنی (ui/widgets/)

1. **SpeedGauge** — گیج سرعت 270 درجه با:
   - گرادیان رنگ بر اساس سرعت (Cyan→Green→Amber→Red)
   - هاله گلو سه‌لایه GPU-friendly
   - انیمیشن count-up هموار (500ms, DecelerateInterpolator)
   - تیک‌های فعال/غیرفعال + تیک آستانه قرمز در 80٪

2. **PulseRadar** — رادار پالسی روی موقعیت خودرو روی نقشه (3 حلقه منتشرشونده با 60fps)

3. **GlowDot** — نقطه وضعیت با تنفس (breathing) نرم برای MQTT/Engine

4. **SparklineView** — نمودار زنده سرعت با گرادیان fill زیر منحنی

## انیمیشن‌های صفحه‌ها

- **Auth**: لوگو pop + چرخش → عنوان fade-up → کارت slide-up | تعویض مراحل با slide+fade
- **Dashboard**: پنل ابزار slide-up با Overshoot + دکمه پاور scale-pop | پنل OTP با scale+fade
- **History**: 4 کارت آمار با cascade entrance (120ms فاصله) | مسیر نئونی با glow زیرین
- **Geofence**: پنل ایجاد با slide-up از پایین | دایره‌های حصار نئونی
- **ناوبری**: Bottom Nav با slide هنگام تغییر مقصد + ترنزیشن بین فرگمنت‌ها

## بیلد

اندروید استودیو → Open → پوشه `android` → Sync → Run
minSdk 21 / target 34 / Kotlin 1.8
