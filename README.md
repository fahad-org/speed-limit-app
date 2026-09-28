# حد السرعة

يعرض حد السرعة للشارع اللي أنت فيه (من OpenStreetMap) وينبهك إذا تجاوزته. يشتغل **بدون إنترنت**: الشوارع محفوظة داخل التطبيق في ملف `roads.bin`.

## الخرائط (roads.bin)
- حالياً الملف يغطي **المدينة المنورة**. عشان تبنيه من جديد أو لمنطقة ثانية:
  ```
  powershell -ExecutionPolicy Bypass -File scripts/build-roads.ps1
  powershell -ExecutionPolicy Bypass -File scripts/build-roads.ps1 -South 24.55 -West 46.5 -North 24.95 -East 46.95
  ```
  أول مرة ينزّل خريطة دول الخليج من Geofabrik (حوالي 250 ميجا) ويحفظها، والمرات الجاية يستخدمها مباشرة. أضف `-Update` عشان ينزّل أحدث نسخة.
- إذا الشارع ما له حد مسجّل في OpenStreetMap (وهذا حال أغلب الشوارع في المدينة)، التطبيق يعرض حد **تقديري** حسب نوع الشارع (إطار متقطع ومكتوب «تقديري»)، والتنبيه يكون بالشاشة الحمراء فقط بدون صوت. الأرقام في `DEFAULT_LIMITS` داخل `index.html`.
- شكل الملف موصوف في أول `scripts/build-roads.ps1`، وهو بسيط عشان يقدر يقراه جهاز GPS صغير من كرت ذاكرة.

- **الويب / GitHub Pages**: الملفات في جذر الريبو (`index.html`، `sw.js`، `manifest.webmanifest`).
- **أندرويد وآيفون**: نفس الكود ملفوف بـ [Capacitor](https://capacitorjs.com). ما فيه مجلدات `android/` و`ios/` في الريبو، لأن GitHub Actions ينشئها ويبنيها عند كل رفع.
- **وضع التجربة**: أضف `?demo` للرابط عشان يشتغل بدون GPS (سواقة وهمية).

## تحميل تطبيق الأندرويد
بعد كل رفع لفرع `main` يبني GitHub التطبيق وينشره في Releases:

```
https://github.com/fahad-org/speed-limit-app/releases/latest/download/speed-limit.apk
```

افتح الرابط من الجوال ونزّل الملف، ولما يطلب منك اسمح بـ«تثبيت تطبيقات غير معروفة».

## الآيفون
الـ workflow حق `iOS` يتأكد إن المشروع يُبنى بدون أخطاء (نسخة للمحاكي). عشان تثبته على آيفون حقيقي تحتاج حساب Apple Developer، وبعدها نضيف التوقيع وTestFlight.

## الملفات
| الملف | وش فيه |
|---|---|
| `capacitor.config.json` | اسم التطبيق والمعرّف `com.fahad.speedlimit` |
| `scripts/build-web.mjs` | ينسخ ملفات الويب إلى `www/` |
| `scripts/build-roads.ps1` | يبني `roads.bin` من خرائط OpenStreetMap |
| `scripts/patch-native.mjs` | صلاحيات الموقع، إبقاء الشاشة شغالة، الوضع الطولي، رقم النسخة |
| `scripts/make-assets.ps1` | يرسم الأيقونات وشاشة البداية في `assets/` |
| `scripts/serve.ps1` | سيرفر محلي للتجربة: `http://localhost:8080/?demo` |
| `.github/workflows/` | بناء الأندرويد والآيفون |
