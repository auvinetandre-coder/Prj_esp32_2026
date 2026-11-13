// Execute production command handling and RX budget, adapting C++ char/pointer operations.
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../../src/diagnostics/serial_debug.cpp'), 'utf8');
let command = source.match(/bool SerialDebug::executeCommand\([^]*?\) \{([^]*?)\n\}/)[1]
 .replace('const size_t size = request ? strnlen(request, 48) : 48;', 'const size = request != null ? Math.min(request.length,48) : 48;')
 .replace('char normalized[48];', 'let normalized;')
 .replace('memcpy(normalized, request, size + 1);', 'normalized = Array.from(request);')
 .replace('size_t i = 0', 'let i = 0')
 .replace('const unsigned char c = normalized[i];', 'const c = normalized[i].charCodeAt(0);')
 .replace('normalized[i] = tolower(c);', 'normalized[i] = String.fromCharCode(c).toLowerCase();')
 .replace('char *text = normalized;', 'let text = normalized.join("");')
 .replace("while (*text == ' ' || *text == '\\t') ++text;", "while (text[0] === ' ' || text[0] === '\\t') text=text.slice(1);")
 .replace('size_t length = strlen(text);', 'let length = text.length;')
 .replace('text[--length] = 0;', 'text=text.slice(0,--length);')
 .replace('wifi->copyDiagnostics', 'wifi.copyDiagnostics').replace('wifi->copyLastConnected', 'wifi.copyLastConnected').replace('wifi->diagnosticBoot()', 'wifi.diagnosticBoot()').replace('wifi->diagnosticStorageStatus()', 'wifi.diagnosticStorageStatus()').replace('wifi->diagnosticPending()', 'wifi.diagnosticPending()').replace('wifi->lastConnectedBoot()', 'wifi.lastConnectedBoot()')
 .replace('strstr(text, "history")', 'text.includes("history")').replaceAll('(unsigned long)', '');
const runCommand = new Function('ctx','request','reply','capacity','strcmp','millis','snprintf', `with(ctx) { ${command} }`);
let rx = source.slice(source.indexOf('  for (uint8_t budget'), source.indexOf('  const uint32_t now = millis();', source.indexOf('void SerialDebug::loop()')))
  .replace('uint8_t budget', 'let budget')
  .replace('const char c = static_cast<char>(Serial.read());', 'const c = Serial.read();')
  .replace('sizeof(input)', 'input.length')
  .replace(/'(?:\\.|[^'\\])'/g, literal => String(new Function(`return ${literal}.charCodeAt(0)`)()));
const runRx = new Function('ctx', 'Serial', `with(ctx) { ${rx} }`);
let clock = 1000;
const ctx = {
  state: {}, serialDebugQuery() { return false; },
  SUMMARY: 0, EVENTS: 1, PEERS: 2, WIFI_STATE:3, WIFI_HISTORY:4, reportKind: 0, snapshotCount: 0,
  wifi:{copyDiagnostics(){return 3},copyLastConnected(){return true},diagnosticStorageStatus(){return "SAUVEGARDE_VERIFIEE"},diagnosticPending(){return false},diagnosticBoot(){return 1},lastConnectedBoot(){return 1}},wifiLastConnected:{},wifiLastConnectedValid:false,wifiStorageStatus:"",wifiSavePending:false,wifiBootId:0,wifiLastConnectedBoot:0,wifiEventCount:0,wifiSnapshot:{},wifiEvents:[],wifiTotal:0,
  snapshotEvents() { this.snapshotCount = 3; },
  input: Array(48).fill(0), inputLength: 0, droppingInput: false,
  enabled: false, reportLine: -1, lastReportMs: 0, messages: [],
  queue(...args) { this.messages.push(args); },
  command() { const reply={}; this.accepted = runCommand(this,String.fromCharCode(...this.input.slice(0,this.inputLength)),reply,160,(a,b)=>a===b?0:1,()=>clock,(out,cap,...args)=>{out[0]=1;out.value=args;}); if(reply[0]) this.queue(...reply.value); }
};
let pending = [];
const Serial = { available: () => pending.length, read: () => pending.shift() };
const feed = (text, once = false) => {
  pending.push(...Buffer.from(text));
  do { runRx(ctx, Serial); } while (!once && pending.length);
};
feed('debug on');
assert.equal(ctx.enabled, false, 'Partial command must not execute');
feed('\r\n');
assert.equal(ctx.enabled, true);
assert.equal(ctx.reportLine, 0);
assert.equal(ctx.messages.length, 1, 'CRLF must acknowledge only once');
feed('  DeBuG OfF \r');
assert.equal(ctx.enabled, false);
assert.equal(ctx.reportLine, -1, 'OFF cancels an ongoing report');
feed('debug status\n');
assert.equal(ctx.reportLine, 0);
assert.equal(ctx.enabled, false, 'One-shot must not enable periodic reports');
feed('debug off\n');
feed('x'.repeat(48) + 'debug on\n');
assert.equal(ctx.enabled, false, 'Oversized line suffix must never execute');
feed('debug on\n');
assert.equal(ctx.enabled, true, 'Parser must recover after an oversized line');
feed('debug off\n');
feed('debug oX\bn\n');
assert.equal(ctx.enabled, true, 'Backspace supports correcting a command');
feed('debug off\n');
feed('debug \x01on\n');
assert.equal(ctx.enabled, false, 'Binary input discards the entire command');
feed('debug now\n');assert.equal(ctx.reportKind,ctx.SUMMARY);assert.equal(ctx.enabled,false);
feed('wifi\n');assert.equal(ctx.reportKind,ctx.WIFI_STATE);
feed('wifi history\n');assert.equal(ctx.reportKind,ctx.WIFI_HISTORY);assert.equal(ctx.wifiEventCount,3);
feed('debug wifi history\n');assert.equal(ctx.reportKind,ctx.WIFI_HISTORY);
feed('debug help\n');
assert.equal(ctx.enabled, false);
feed('debug events\n');
assert.equal(ctx.reportKind, ctx.EVENTS);
assert.equal(ctx.snapshotCount, 3);
assert.equal(ctx.enabled, false, 'History is one-shot and does not enable periodic output');
feed('debug peers\n');
assert.equal(ctx.reportKind, ctx.PEERS);
assert.equal(ctx.enabled, false);
feed('debug status\n');
assert.equal(ctx.reportKind, ctx.SUMMARY);
const messagesBefore = ctx.messages.length;
feed('unknown\n');
assert.equal(ctx.messages.length, messagesBefore + 1);
assert.equal(ctx.enabled, false);
feed('x'.repeat(1000), true);
assert.equal(pending.length, 936, 'At most 64 input bytes per loop');
pending = [];
feed('\ndebug on\ndebug off\n');
assert.equal(ctx.enabled, false, 'Last complete command wins');
console.log('OK serial debug: partial lines, CR/LF/CRLF, case, OFF cancellation, one-shot, overflow, recovery, binary input, backspace, RX flood');

let tx = source.slice(source.indexOf('  const uint32_t now = millis();', source.indexOf('void SerialDebug::loop()')), source.indexOf('\nvoid SerialDebug::report'))
  .replace(/\n\}\s*$/, '')
  .replace('const uint32_t now', 'const now')
  .replace('uint32_t(now - lastReportMs)', '((now - lastReportMs) >>> 0)')
  .replace('5000UL', '5000')
  .replace('const int available', 'const available')
  .replace('const uint16_t bytes', 'const bytes')
  .replace('reinterpret_cast<const uint8_t *>(output + outputOffset)', 'output.slice(outputOffset)');
const runTx = new Function('ctx', 'Serial', 'millis', `with(ctx) { ${tx} }`);
let room = 0, reports = 0, writes = [];
const txState = {
  SUMMARY: 0, reportKind: 0, observe() {},
  output: 'a complete diagnostic line\r\n', outputOffset: 0, outputLength: 28,
  reportLine: -1, enabled: false, lastReportMs: 0,
  report() { reports++; this.reportLine = -1; this.output = 'report\r\n'; this.outputLength = 8; this.outputOffset = 0; }
};
txState.outputLength = txState.output.length;
const uart = { availableForWrite: () => room, write(data, bytes) { writes.push(data.slice(0, bytes)); return bytes; } };
const tick = time => runTx(txState, uart, () => time);
tick(1000);
assert.equal(writes.length, 0, 'Full UART must not be written to');
room = txState.outputLength - 1;
tick(1001);
assert.equal(writes.length, 0, 'Wait for a complete line, without blocking');
room = 512;
tick(1002);
assert.deepEqual(writes, ['a complete diagnostic line\r\n']);
tick(10000);
assert.equal(reports, 0, 'OFF suppresses periodic reports');
txState.enabled = true;
txState.lastReportMs = 10000;
tick(14999);
assert.equal(reports, 0);
tick(15000);
assert.equal(reports, 1, 'First report at five seconds');
tick(15001);
assert.equal(reports, 1, 'No duplicate report');
txState.lastReportMs = 0xfffffff0;
tick((0xfffffff0 + 5000) >>> 0);
assert.equal(reports, 2, 'Periodic scheduling survives millis wrap');
console.log('OK serial debug TX: UART backpressure, whole lines, OFF, five-second period and rollover');
