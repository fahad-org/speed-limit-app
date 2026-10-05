// Phone app for the speed-limit device. Talks to it over Bluetooth with lines of JSON (protocol: esp32/speed_limit/ble.h).
// Add ?mock to the address to try the screens on a PC without the device.
import { BleClient } from '@capacitor-community/bluetooth-le';

const SVC = '6e400001-b5a3-f393-e0a9-e50e24dcca9e', RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e', TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';
const $ = id => document.getElementById(id);
const store = { get: k => { try { return localStorage.getItem(k); } catch (e) { return null; } }, set: (k, v) => { try { localStorage.setItem(k, v); } catch (e) {} } };
const CLASS_AR = ['طريق سريع', 'رئيسي سريع', 'رئيسي', 'ثانوي', 'ثالثي', 'غير مصنف', 'سكني', 'شارع داخلي', 'خدمة', 'وصلة سريع', 'وصلة', 'وصلة', 'وصلة', 'وصلة'];
const MSG_AR = { 1: 'ما فيه كرت الخريطة', 2: 'الخريطة ناقصة', 3: 'ما فيه GPS', 4: 'خارج الخريطة', 5: 'القطعة ما تستقبل من الـGPS' };
const ERR_AR = { 'no road': 'ما فيه شارع محدد الحين', auth: 'الرمز غلط', pin: 'الرمز غلط', storage: 'ما انحفظ في القطعة', range: 'رقم غير صحيح', 'no fix': 'ما فيه موقع GPS الحين' };

// ---------------- transports: how lines get to the device ----------------
class BleTransport {
  constructor() { this.id = null; this.buf = ''; }
  async connect(pick) {
    await BleClient.initialize({ androidNeverForLocation: true });
    let id = store.get('deviceId');
    if (pick || !id) {
      let d;
      try { d = await BleClient.requestDevice({ services: [SVC] }); }
      catch (e) {
        if (!/no device/i.test(String(e && e.message || e))) throw e;
        setStatus('ما لقيت القطعة بالبحث السريع. اختر SpeedLimit من القائمة…');
        d = await BleClient.requestDevice({});         // no filter: the list shows every Bluetooth device nearby
      }
      id = d.deviceId; store.set('deviceId', id);
    }
    await BleClient.connect(id, () => this.onClose && this.onClose(), { timeout: 10000 });
    this.id = id;
    await BleClient.startNotifications(id, SVC, TX, v => {
      this.buf += new TextDecoder().decode(v.buffer ? new Uint8Array(v.buffer, v.byteOffset, v.byteLength) : v);
      let i; while ((i = this.buf.indexOf('\n')) >= 0) { const line = this.buf.slice(0, i); this.buf = this.buf.slice(i + 1); if (line) this.onLine && this.onLine(line); }
    });
  }
  async write(text) {
    const bytes = new TextEncoder().encode(text);
    for (let i = 0; i < bytes.length; i += 20) await BleClient.writeWithoutResponse(this.id, SVC, RX, new DataView(bytes.buffer, bytes.byteOffset + i, Math.min(20, bytes.length - i)));
  }
  async disconnect() { try { if (this.id) await BleClient.disconnect(this.id); } catch (e) {} }
}

// Pretends to be the device (same lines), for trying the screens on a PC
class MockTransport {
  constructor() { this.st = { s: 83, l: 60, e: 1, o: 1, m: 0, r: 1, c: 4, d: 0, a: 11, w: 17, x: 0, u: 0, off: 5, tol: 2, et: 10, br: 100, v: 'mock', h: 50000 }; this.authed = false; this.prev = null; }
  async connect() { await new Promise(r => setTimeout(r, 400)); this.timer = setInterval(() => this.authed && this.state(), 1000); }
  state() { const s = this.st; s.o = s.l && s.s > s.l + (s.e ? s.et : s.tol) ? 1 : 0; this.onLine(JSON.stringify({ t: 's', ...s })); }
  async write(text) {
    for (const line of text.split('\n').filter(Boolean)) setTimeout(() => this.handle(JSON.parse(line)), 150);
  }
  handle(m) {
    const reply = (ok, x = {}) => this.onLine(JSON.stringify({ t: 'r', c: m.c, ok: ok ? 1 : 0, ...x }));
    if (m.c === 'auth') { this.authed = m.pin === '1234'; reply(this.authed, this.authed ? {} : { err: 'pin' }); if (this.authed) this.state(); return; }
    if (!this.authed) return reply(false, { err: 'auth' });
    const s = this.st;
    if (m.c === 'fix') { this.prev = { l: s.l, e: s.e, d: s.d }; s.l = m.limit || 60; s.e = m.limit ? 0 : 1; s.d = m.limit ? 1 : 0; s.u = 1; s.x += m.one ? 1 : 23; reply(true, { n: m.one ? 1 : 23 }); this.state(); }
    else if (m.c === 'undo') { if (this.prev) Object.assign(s, this.prev); s.u = 0; reply(true, { n: 23 }); this.state(); }
    else if (m.c === 'flag') reply(true);
    else if (m.c === 'set') { for (const k of ['off', 'tol', 'et', 'br']) if (k in m) s[k] = m[k]; reply(true); this.state(); }
    else reply(false, { err: 'unknown' });
  }
  async disconnect() { clearInterval(this.timer); this.onClose && this.onClose(); }
}

// ---------------- connection + commands ----------------
const mock = /[?&]mock\b/.test(location.search);
let tr = null, st = null, pending = {}, wantConnected = false, retryT = 0, lastState = 0;

function setStatus(t) { $('status').textContent = t || ''; }
function dot(c) { $('dot').className = 'dot' + (c ? ' ' + c : ''); }
function toast(t, ms = 2600) { const e = $('toast'); e.textContent = t; e.classList.remove('hide'); clearTimeout(toast.t); toast.t = setTimeout(() => e.classList.add('hide'), ms); }

function ask(obj, timeout = 6000) {
  return new Promise((resolve, reject) => {
    const t = setTimeout(() => { delete pending[obj.c]; reject(new Error('timeout')); }, timeout);
    pending[obj.c] = r => { clearTimeout(t); resolve(r); };
    tr.write(JSON.stringify(obj) + '\n').catch(e => { clearTimeout(t); delete pending[obj.c]; reject(e); });
  });
}
function onLine(line) {
  let m; try { m = JSON.parse(line); } catch (e) { return; }
  if (m.t === 's') { st = m; lastState = Date.now(); if (!$('app').classList.contains('hide')) draw(); }
  else if (m.t === 'r' && pending[m.c]) { const f = pending[m.c]; delete pending[m.c]; f(m); }
}

async function connect(pick) {
  if (tr) return;
  $('conBtn').disabled = true; setStatus('يتصل بالقطعة…'); dot('mid'); wantConnected = true;
  tr = mock ? new MockTransport() : new BleTransport();
  tr.onLine = onLine; tr.onClose = onClosed;
  try {
    await tr.connect(pick);
    const pin = $('pin').value.trim(); store.set('pin', pin);
    const r = await ask({ c: 'auth', pin });
    if (!r.ok) { wantConnected = false; await tr.disconnect(); tr = null; $('conBtn').disabled = false; dot(''); setStatus('الرمز غلط. جرّب الرمز الصحيح (الافتراضي 1234).'); return; }
    $('connect').classList.add('hide'); $('app').classList.remove('hide'); dot('on'); setStatus('');
  } catch (e) {
    const t = tr; tr = null; try { t && await t.disconnect(); } catch (x) {}
    $('conBtn').disabled = false; dot('');
    const m = String(e && e.message || e);
    setStatus(/cancel|dismiss/i.test(m) ? '' : 'ما قدرت أتصل بالقطعة. تأكد أنها شغّالة وقريبة، والبلوتوث وصلاحية «الأجهزة القريبة» شغّالين.\n(' + m.slice(0, 80) + ')');
    if (wantConnected && !pick && store.get('deviceId')) scheduleRetry();
  }
}
function onClosed() {
  tr = null; dot('');
  if (!wantConnected) return;
  $('app').classList.add('hide'); $('connect').classList.remove('hide'); $('conBtn').disabled = false;
  setStatus('انقطع الاتصال، يحاول يرجع…'); scheduleRetry();
}
function scheduleRetry() { clearTimeout(retryT); retryT = setTimeout(() => { if (wantConnected && !tr) connect(false); }, 3000); }
async function disconnect() { wantConnected = false; clearTimeout(retryT); const t = tr; if (t) { await t.disconnect(); } tr = null; st = null; dot(''); $('app').classList.add('hide'); $('connect').classList.remove('hide'); $('conBtn').disabled = false; setStatus(''); }

// ---------------- screens ----------------
function draw() {
  $('ver').textContent = 'v' + st.v;
  const s = $('sign'); s.className = 'sign';
  if (st.l) { s.textContent = st.l; if (st.e) s.classList.add('est'); if (st.o) s.classList.add('over'); }
  else { s.textContent = MSG_AR[st.m] || 'حد غير معروف'; s.classList.add('none'); }
  $('spd').textContent = st.s;
  const parts = [];
  if (st.r) {
    parts.push('<span class="chip">' + (CLASS_AR[st.c] || 'شارع') + '</span>');
    parts.push(st.d ? '<span class="chip ed">تعديلك</span>' : st.e ? '<span class="chip">تقدير حسب نوع الشارع</span>' : '<span class="chip">حد من الخريطة</span>');
  }
  parts.push('<br>أقمار ' + st.a + '/' + st.w);
  $('meta').innerHTML = parts.join('');
  $('fixBtn').disabled = !st.r; $('undoBtn').disabled = !st.u;
  if (!$('set').classList.contains('hide')) drawSet();
}

// fix sheet
let val = 60;
const VALUES = [20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 140];
$('grid').innerHTML = VALUES.map(v => '<button data-v="' + v + '">' + v + '</button>').join('');
function setVal(v) { val = Math.max(0, Math.min(200, v)); $('shVal').textContent = val; document.querySelectorAll('#grid button').forEach(b => b.classList.toggle('on', +b.dataset.v === val)); }
$('fixBtn').onclick = () => { $('shTitle').textContent = 'الحد الصحيح لهذا الشارع (' + (CLASS_AR[st.c] || '') + ')'; setVal(st.l || 60); $('sheet').classList.remove('hide'); };
$('grid').onclick = e => { if (e.target.dataset.v) setVal(+e.target.dataset.v); };
$('minus').onclick = () => setVal(val - 10); $('plus').onclick = () => setVal(val + 10);
$('closeBtn').onclick = () => $('sheet').classList.add('hide');
async function run(cmd, okText) {
  try { const r = await ask(cmd); toast(r.ok ? okText(r) : 'ما تم: ' + (ERR_AR[r.err] || r.err)); }
  catch (e) { toast('القطعة ما ردّت، جرّب مرة ثانية'); }
}
async function send(limit, one) {
  $('sheet').classList.add('hide');
  await run(one ? { c: 'fix', limit, one: 1 } : { c: 'fix', limit }, r => limit ? 'تم: ' + r.n + (r.n === 1 ? ' مقطع' : ' مقطع من الشارع') + ' صار ' + limit : 'رجع للتخمين');
}
$('applyAll').onclick = () => send(val, false);
$('applyOne').onclick = () => send(val, true);
$('clearBtn').onclick = () => send(0, false);
$('undoBtn').onclick = () => run({ c: 'undo' }, () => 'تم التراجع');
$('flagBtn').onclick = () => run({ c: 'flag' }, () => 'تم تعليم المكان للمراجعة');

// tabs + settings
function tab(t) { $('live').classList.toggle('hide', t !== 'live'); $('set').classList.toggle('hide', t !== 'set'); $('tLive').classList.toggle('on', t === 'live'); $('tSet').classList.toggle('on', t === 'set'); if (t === 'set' && st) drawSet(); }
$('tLive').onclick = () => tab('live'); $('tSet').onclick = () => tab('set');
let editing = 0;
function drawSet() {
  if (Date.now() - editing < 1500) return;
  $('v_off').textContent = st.off; $('v_tol').textContent = st.tol; $('v_et').textContent = st.et; $('bright').value = st.br;
  $('info').innerHTML = 'النسخة ' + st.v + ' · تعديلاتك المحفوظة: ' + st.x + '<br>الذاكرة الحرة في القطعة: ' + Math.round(st.h / 1024) + ' كيلوبايت';
}
async function save(p) { editing = Date.now(); try { const r = await ask({ c: 'set', ...p }); if (!r.ok) toast('ما انحفظ: ' + (ERR_AR[r.err] || r.err)); return r; } catch (e) { toast('القطعة ما ردّت'); } }
document.querySelectorAll('.step button').forEach(b => b.onclick = () => {
  const k = b.dataset.k, lim = { off: [0, 20], tol: [0, 20], et: [0, 40] }[k], v = Math.max(lim[0], Math.min(lim[1], st[k] + +b.dataset.d));
  st[k] = v; $('v_' + k).textContent = v; save({ [k]: v });
});
$('bright').oninput = () => { st.br = +$('bright').value; save({ br: st.br }); };
$('pinBtn').onclick = async () => {
  const p = $('newPin').value.trim(); if (!/^\d{4,8}$/.test(p)) { toast('الرمز 4 إلى 8 أرقام'); return; }
  const r = await save({ pin: p }); if (r && r.ok) { $('pin').value = p; store.set('pin', p); $('newPin').value = ''; toast('انحفظ الرمز الجديد'); }
};
$('disBtn').onclick = disconnect;
$('conBtn').onclick = () => connect(true);

// start: remember the PIN, and try the device we used last time
$('pin').value = store.get('pin') || '1234';
if (mock) { $('ver').textContent = 'تجربة'; }
else if (store.get('deviceId')) { wantConnected = true; setTimeout(() => connect(false), 300); }
// the device sends its state every second: if it goes quiet the link is dead
setInterval(() => { if (tr && lastState && Date.now() - lastState > 6000 && !$('app').classList.contains('hide')) { lastState = 0; try { tr.disconnect(); } catch (e) {} } }, 2000);
