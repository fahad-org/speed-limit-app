// Applies the app's native settings to the generated android/ and ios/ projects.
// Run after `npx cap add <platform>`:  node scripts/patch-native.mjs android|ios
import { readFileSync, writeFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { join } from 'node:path';

const platform = process.argv[2];
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
    if (statSync(p).isDirectory()) {
      const hit = findFile(p, names);
      if (hit) return hit;
    } else if (names.includes(entry)) return p;
  }
  return null;
}

if (platform === 'android') {
  const manifest = 'android/app/src/main/AndroidManifest.xml';
  edit(manifest, s => s.includes('ACCESS_FINE_LOCATION') ? s : s.replace(
    /(\s*)<uses-permission android:name="android\.permission\.INTERNET" \/>/,
    '$1<uses-permission android:name="android.permission.INTERNET" />' +
    '$1<uses-permission android:name="android.permission.ACCESS_COARSE_LOCATION" />' +
    '$1<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />' +
    '$1<uses-permission android:name="android.permission.VIBRATE" />' +
    '$1<uses-feature android:name="android.hardware.location.gps" android:required="false" />'
  ), 'location + vibrate permissions');
  edit(manifest, s => s.includes('screenOrientation') ? s : s.replace(
    /android:name="\.MainActivity"/, 'android:name=".MainActivity"\n            android:screenOrientation="portrait"'
  ), 'portrait only', false);

  // Keep the screen on while the app is open
  const main = findFile('android/app/src/main/java', ['MainActivity.java', 'MainActivity.kt']);
  if (main && main.endsWith('.java')) {
    edit(main, s => s.includes('FLAG_KEEP_SCREEN_ON') ? s : s
      .replace(/(import com\.getcapacitor\.BridgeActivity;)/,
        '$1\nimport android.os.Bundle;\nimport android.view.WindowManager;')
      .replace(/extends BridgeActivity\s*\{\s*\}/,
        'extends BridgeActivity {\n    @Override\n    protected void onCreate(Bundle savedInstanceState) {\n' +
        '        super.onCreate(savedInstanceState);\n' +
        '        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);\n    }\n}'),
      'keep screen on', false);
  } else if (main) {
    edit(main, s => s.includes('FLAG_KEEP_SCREEN_ON') ? s : s
      .replace(/(import com\.getcapacitor\.BridgeActivity)/,
        '$1\nimport android.os.Bundle\nimport android.view.WindowManager')
      .replace(/class MainActivity\s*:\s*BridgeActivity\(\)\s*(\{\s*\})?/,
        'class MainActivity : BridgeActivity() {\n    override fun onCreate(savedInstanceState: Bundle?) {\n' +
        '        super.onCreate(savedInstanceState)\n' +
        '        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)\n    }\n}'),
      'keep screen on', false);
  }

  // Each CI build gets a higher versionCode so it installs over the previous one
  edit('android/app/build.gradle', s => s
    .replace(/versionCode \d+/, `versionCode ${build}`)
    .replace(/versionName "[^"]*"/, `versionName "1.0.${build}"`),
    `version 1.0.${build}`, false);
}

else if (platform === 'ios') {
  const plist = 'ios/App/App/Info.plist';
  const add = (s, key, xml) => s.includes(`<key>${key}</key>`) ? s
    : s.replace(/<\/dict>\s*<\/plist>\s*$/, `\t<key>${key}</key>\n\t${xml}\n</dict>\n</plist>\n`);
  edit(plist, s => {
    s = add(s, 'NSLocationWhenInUseUsageDescription',
      '<string>يستخدم التطبيق موقعك لمعرفة الشارع اللي أنت فيه وحد السرعة فيه، وحساب سرعتك.</string>');
    s = add(s, 'CFBundleDevelopmentRegion', '<string>ar</string>');
    s = s.replace(/(<key>UISupportedInterfaceOrientations<\/key>\s*<array>)[\s\S]*?(<\/array>)/,
      '$1\n\t\t<string>UIInterfaceOrientationPortrait</string>\n\t$2');
    return s;
  }, 'location permission text + portrait');

  const delegate = findFile('ios/App/App', ['AppDelegate.swift']);
  if (delegate) {
    edit(delegate, s => s.includes('isIdleTimerDisabled') ? s : s.replace(
      /(didFinishLaunchingWithOptions[^{]*\{)/,
      '$1\n        // Keep the screen on while the app is open\n        UIApplication.shared.isIdleTimerDisabled = true'
    ), 'keep screen on', false);
  }
}

else {
  console.error('usage: node scripts/patch-native.mjs android|ios');
  process.exit(1);
}

if (!existsSync('www/index.html')) console.warn('note: www/ is empty, run `npm run build` first');
