// Copies the web app (kept at the repo root so GitHub Pages keeps working) into www/ for Capacitor.
import { cpSync, mkdirSync, rmSync } from 'node:fs';

const FILES = ['index.html', 'sw.js', 'manifest.webmanifest', 'roads.bin','icon-192.png', 'icon-512.png', 'icon-maskable-512.png'];

rmSync('www', { recursive: true, force: true });
mkdirSync('www');
for (const f of FILES) cpSync(f, `www/${f}`);
console.log(`www/: ${FILES.length} files`);
