// Bundles the app (src/app.js + the Bluetooth plugin) and copies the page into www/ for Capacitor.
import { build } from 'esbuild';
import { cpSync, mkdirSync, rmSync } from 'node:fs';

rmSync('www', { recursive: true, force: true });
mkdirSync('www');
await build({ entryPoints: ['src/app.js'], bundle: true, minify: true, format: 'iife', target: ['chrome90'], outfile: 'www/app.js', logLevel: 'info' });
cpSync('src/index.html', 'www/index.html');
console.log('www/ ready');
