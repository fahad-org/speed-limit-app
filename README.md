# حد السرعة

يعرض حد السرعة للشارع اللي أنت فيه (من OpenStreetMap) وينبهك إذا تجاوزته.

- **الويب / GitHub Pages**: الملفات في جذر الريبو (`index.html`، `sw.js`، `manifest.webmanifest`).
- **أندرويد وآيفون**: نفس الكود ملفوف بـ [Capacitor](https://capacitorjs.com). ما فيه مجلدات `android/` و`ios/` في الريبو، لأن GitHub Actions ينشئها ويبنيها عند كل رفع.
- **وضع التجربة**: أضف `?demo` للرابط عشان يشتغل بدون GPS (سواقة وهمية).

## تحميل تطبيق الأندرويد
بعد كل رفع لفرع `main` يبني GitHub التطبيق وينشره في Releases:

```
https://github.com/fahad-org/speed-limit/releases/latest/download/speed-limit.apk
```

افتح الرابط من الجوال ونزّل الملف، ولما يطلب منك اسمح بـ«تثبيت تطبيقات غير معروفة».

## الآيفون
الـ workflow حق `iOS` يتأكد إن المشروع يُبنى بدون أخطاء (نسخة للمحاكي). عشان تثبته على آيفون حقيقي تحتاج حساب Apple Developer، وبعدها نضيف التوقيع وTestFlight.

## الملفات
| الملف | وش فيه |
|---|---|
| `capacitor.config.json` | اسم التطبيق والمعرّف `com.fahad.speedlimit` |
| `scripts/build-web.mjs` | ينسخ ملفات الويب إلى `www/` |
| `scripts/patch-native.mjs` | صلاحيات الموقع، إبقاء الشاشة شغالة، الوضع الطولي، رقم النسخة |
| `scripts/make-assets.ps1` | يرسم الأيقونات وشاشة البداية في `assets/` |
| `scripts/serve.ps1` | سيرفر محلي للتجربة: `http://localhost:8080/?demo` |
| `.github/workflows/` | بناء الأندرويد والآيفون |
