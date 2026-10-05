// Applies the app's native settings to the generated android/ project (run after `npx cap add android`).
import { readFileSync, writeFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';

const build = parseInt(process.env.BUILD_NUMBER || '1', 10);

function edit(file, fn, what, required = true) {
  const before = readFileSync(file, 'utf8');
  const after = fn(before);
  if (after === before) {
    if (required) throw new Error(`patch failed: ${what} (${file})`);
    console.warn(`skipped: ${what}`);
    return;
  }
  writeFileSync(file, after);
  console.log(`patched: ${what}`);
}
function findFile(dir, names) {
  for (const entry of readdirSync(dir)) {
    const p = join(dir, entry);
    if (statSync(p).isDirectory()) { const hit = findFile(p, names); if (hit) return hit; }
    else if (names.includes(entry)) return p;
  }
  return null;
}

const manifest = 'android/app/src/main/AndroidManifest.xml';
// Bluetooth: Android 12+ asks for SCAN/CONNECT (we never use them to find the user's location); older versions need these plus location
edit(manifest, s => s.includes('BLUETOOTH_CONNECT') ? s : s.replace(
  /(\s*)<uses-permission android:name="android\.permission\.INTERNET" \/>/,
  '$1<uses-permission android:name="android.permission.INTERNET" />' +
  '$1<uses-permission android:name="android.permission.BLUETOOTH" android:maxSdkVersion="30" />' +
  '$1<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" android:maxSdkVersion="30" />' +
  '$1<uses-permission android:name="android.permission.BLUETOOTH_SCAN" android:usesPermissionFlags="neverForLocation" />' +
  '$1<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />' +
  '$1<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" android:maxSdkVersion="30" />'
), 'bluetooth permissions');
edit(manifest, s => s.includes('screenOrientation') ? s : s.replace(
  /android:name="\.MainActivity"/, 'android:name=".MainActivity"\n            android:screenOrientation="portrait"'
), 'portrait only', false);

// Keep the screen on while the app is open (it is used in the car)
const main = findFile('android/app/src/main/java', ['MainActivity.java', 'MainActivity.kt']);
if (main && main.endsWith('.java')) {
  edit(main, s => s.includes('FLAG_KEEP_SCREEN_ON') ? s : s
    .replace(/(import com\.getcapacitor\.BridgeActivity;)/, '$1\nimport android.os.Bundle;\nimport android.view.WindowManager;')
    .replace(/extends BridgeActivity\s*\{\s*\}/,
      'extends BridgeActivity {\n    @Override\n    protected void onCreate(Bundle savedInstanceState) {\n' +
      '        super.onCreate(savedInstanceState);\n        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);\n    }\n}'),
    'keep screen on', false);
} else if (main) {
  edit(main, s => s.includes('FLAG_KEEP_SCREEN_ON') ? s : s
    .replace(/(import com\.getcapacitor\.BridgeActivity)/, '$1\nimport android.os.Bundle\nimport android.view.WindowManager')
    .replace(/class MainActivity\s*:\s*BridgeActivity\(\)\s*(\{\s*\})?/,
      'class MainActivity : BridgeActivity() {\n    override fun onCreate(savedInstanceState: Bundle?) {\n' +
      '        super.onCreate(savedInstanceState)\n        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)\n    }\n}'),
    'keep screen on', false);
}

// Each CI build gets a higher versionCode so it installs over the previous one
edit('android/app/build.gradle', s => s
  .replace(/versionCode \d+/, `versionCode ${build}`)
  .replace(/versionName "[^"]*"/, `versionName "1.0.${build}"`), `version 1.0.${build}`, false);
