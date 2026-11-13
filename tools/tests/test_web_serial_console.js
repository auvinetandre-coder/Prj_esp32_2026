const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = file => fs.readFileSync(path.join(__dirname, "../..", file), "utf8");

const reader = read("src/diagnostics/console_serial.cpp").match(/size_t ConsoleSerial::readLines\([^]*?\) \{([^]*?)\n\}/)[1]
  .replace(/port(?:ENTER|EXIT)_CRITICAL\(&mux\);/g, "")
  .replace("sequence - after", "(sequence - after) >>> 0")
  .replace("sequence - retained + copied", "(sequence - retained + copied) >>> 0")
  .replace(/\bconst (?:uint32_t|size_t) /g, "const ").replace("size_t i", "let i");
const readLines = new Function("state", "after", "out", "limit", "with(state) {" + reader + "}");
const ring = {capacity: 32, count: 32, sequence: 40, head: 8, next: 0, lost: 0, INT32_MAX: 0x7fffffff,
  lines: Array.from({length: 32}, (_, i) => i < 8 ? 33 + i : 1 + i)};
const batch = [];
assert.equal(readLines(ring, 0, batch, 4), 4);
assert.deepEqual(batch, [9, 10, 11, 12]);
assert.equal(ring.next, 12);
assert.equal(ring.lost, 8);
readLines(ring, 12, batch, 4);
assert.deepEqual(batch, [13, 14, 15, 16]);
assert.equal(ring.lost, 0);
readLines(ring, 41, batch, 4);
assert.equal(ring.lost, 0, "A future cursor cannot report billions of lost lines");
Object.assign(ring, {sequence:1, count:3, head:3, lines:["last before wrap", "zero", "one"]});
assert.equal(readLines(ring, 0xfffffffe, batch, 3), 3);
assert.deepEqual(batch.slice(0, 3), ring.lines);
assert.equal(ring.next, 1);

// Reproduce a report followed by a periodic summary before the browser reads.
const transportCapacity = Number(read("src/diagnostics/console_serial.h").match(/capacity = (\d+)/)[1]);
const transport = {capacity:transportCapacity,count:0,sequence:0,head:0,next:0,lost:0,lines:Array(transportCapacity),INT32_MAX:0x7fffffff};
const emit = text => {transport.lines[transport.head]=text;transport.head=(transport.head+1)%transport.capacity;transport.sequence++;transport.count=Math.min(transport.count+1,transport.capacity);};
for(let i=1;i<=6;i++)emit('WIFI #'+i);
emit('WIFI HISTORY END');for(let i=0;i<32;i++)emit('DBG '+i);emit('JSY');emit('JSY event');
let cursor=0, all=[];while(cursor<transport.sequence){const chunk=[];readLines(transport,cursor,chunk,8);assert.equal(transport.lost,0,'periodic report must not evict WiFi history before first poll');cursor=transport.next;all.push(...chunk);}
assert.deepEqual(all.filter(x=>x.startsWith('WIFI #')),Array.from({length:6},(_,i)=>'WIFI #'+(i+1)));

const queryBody = read("src/diagnostics/serial_debug.cpp").match(/bool serialDebugQuery\([^]*?\) \{([^]*?)\n\}/)[1]
  .replace(/\(unsigned long(?: long)?\)/g, "").replaceAll(".c_str()", "");
const query = new Function("state", "command", "out", "capacity", "strcmp", "snprintf", "ESP", "esp_timer_get_time", "ROUTEUR_FIRMWARE_VERSION", queryBody);
const runQuery = command => query({stationIp:"192.168.0.90", safetyLevel:"OK"}, command, {}, 160,
  (a,b) => a === b ? 0 : 1, () => {},
  {getFreeHeap:()=>60000, getMinFreeHeap:()=>55000, getMaxAllocHeap:()=>28000}, ()=>2200000, "20261110-01");
for (const command of ["help","status","heap","uptime","version"]) assert.equal(runQuery(command), true);
for (const command of ["debug on","reboot","setPower","status\nreboot","unknown"]) assert.equal(runQuery(command), false);
const http = read("src/web/serial_console_http.cpp");
assert.match(http,/Line batch\[8\]/);
assert.match(http,/doc\["more"\] = count == 8/);
const maxRows=Array.from({length:8},()=>({ms:0xffffffff,epochMs:9999999999999,text:'\\'.repeat(191),cut:true}));
assert.ok(Buffer.byteLength(JSON.stringify({boot:0xffffffff,next:0xffffffff,lost:0xffffffff,more:true,lines:maxRows}))+300<4096,'bounded HTTP packet holds maximum escaped rows plus header');
assert.match(http, /\/api\/serial-console", HTTP_GET,[^]*?if \(!authorizeConsole\(\)\) return;/);
assert.match(http, /\/api\/serial-console\/command", HTTP_POST,[^]*?if \(!authorizeConsole\(\)\) return;[^]*?X-Serial-Console/);
assert.match(http, /serialDebug\.executeCommand\(command\.c_str\(\), reply, sizeof\(reply\)\)/, "HTTP must call the same command engine as USB");
assert.match(http, /::send\([^;]+MSG_DONTWAIT/);
assert.doesNotMatch(http, /Serial\.write|ESP\.restart|saveConfig|LittleFS/);

class Element {
  handlers = {};
  children = new Map();
  value = "";
  textContent = "";
  isConnected = false;
  querySelector(key) {
    if (!this.children.has(key)) this.children.set(key, new Element());
    return this.children.get(key);
  }
  addEventListener(name, fn) { this.handlers[name] = fn; }
  setAttribute(name, value) { this[name] = value; }
  appendChild(node) { this.child = node; node.isConnected = true; }
}

(async () => {
  const document = new Element();
  document.createElement = () => new Element();
  const timers = new Map(), requests = [], responses = [], scheduledDelays=[];
  let id = 0;
  const context = {window:{}, document, page:"diagnostic", AbortController,
    setTimeout(fn, ms) { scheduledDelays.push(ms); timers.set(++id, fn); return id; },
    clearTimeout(key) { timers.delete(key); },
    async fetch(url, options) {
      requests.push({url, options});
      const reply = responses.shift();
      if (!reply) throw Error("Connexion interrompue");
      return {ok:true, json:async()=>reply, text:async()=>reply};
    }
  };
  vm.runInNewContext(read("data/www/serial_console.js"), context);
  async function tick() {
    const [key, fn] = timers.entries().next().value;
    timers.delete(key);
    await fn();
  }
  const slot = new Element();
  context.window.mountSerialConsole(slot);
  const panel = slot.child, screen = panel.querySelector("[data-screen]"), input = panel.querySelector("input");
  responses.push({boot:1, next:9, lost:2, more:true, lines:[
    {ms:52756, text:"<img onerror=alert(1)>"},
    {epochMs:1700000000123, text:"heure", cut:true}
  ]});
  await tick();
  assert.match(screen.textContent, /\[\+52\.756 s\] <img/);
  assert.match(screen.textContent, /\.123\] heure .*tronqué/);
  assert.match(screen.textContent, /2 lignes sorties/);
  assert.equal(scheduledDelays.at(-1),240,"drain backlog promptly");
  await tick();
  assert.match(panel.querySelector("[data-connection]").textContent, /Reconnexion/);
  assert.ok(scheduledDelays.at(-1)>=2000,"backoff must remain slow after a fast catch-up poll");
  responses.push({boot:2, next:1, lost:0, lines:[]});
  await tick();
  assert.equal(requests[2].url, "/api/serial-console?boot=1&after=9");
  assert.match(screen.textContent, /Redémarrage/);
  assert.equal(scheduledDelays.at(-1),1000,"idle polling returns to 1 second");
  panel.querySelector("[data-pause]").handlers.click();
  assert.equal(timers.size, 0);
  panel.querySelector("[data-clear]").handlers.click();
  assert.equal(screen.textContent, "");
  responses.push("Firmware 20261110-01");
  input.value = "version";
  await panel.querySelector("form").handlers.submit({preventDefault() {}});
  assert.equal(requests.at(-1).options.headers["X-Serial-Console"], "1");
  assert.match(screen.textContent, /> version\nFirmware/);
  input.value = "brouillon";
  input.handlers.keydown({key:"ArrowUp", preventDefault() {}});
  assert.equal(input.value, "version");
  input.handlers.keydown({key:"ArrowDown", preventDefault() {}});
  assert.equal(input.value, "brouillon");
  panel.querySelector("[data-pause]").handlers.click();
  assert.equal(timers.size, 1);
  document.hidden = true;
  document.handlers.visibilitychange();
  assert.equal(timers.size, 0);
  const nextSlot = new Element();
  context.window.mountSerialConsole(nextSlot);
  assert.equal(nextSlot.child, panel);
  assert.equal(input.value, "brouillon");
  console.log("OK console Web: curseurs, horodatage, reconnexion, pause, commandes, historique");
})().catch(error => { console.error(error); process.exitCode = 1; });
