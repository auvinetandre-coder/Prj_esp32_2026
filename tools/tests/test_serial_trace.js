// Exercise the production RAM ring and module timing capture (C++ adapter).
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const s = fs.readFileSync(path.join(__dirname, '../../src/diagnostics/serial_debug.cpp'), 'utf8');
const method = name => s.match(new RegExp('void SerialDebug::' + name + '\\([^]*?\\) \\{([^]*?)\\n\\}'))[1];
let trace = method('trace').replace('{millis(), kind, value, detail, consoleEpochMs()}', '{ms: millis(), kind, value, detail, epochMs: consoleEpochMs()}');
let snapshot = method('snapshotEvents').replace('uint8_t i', 'let i')
  .replace('traceSnapshot[i] = traces[(traceHead + traceCapacity - traceCount + i) % traceCapacity];',
    'traceSnapshot[i] = {...traces[(traceHead + traceCapacity - traceCount + i) % traceCapacity]};');
let moduleBody = method('observeModule').replace('const uint32_t now', 'const now')
  .replace('uint32_t(now - lastSlowTraceMs[module])', '((now - lastSlowTraceMs[module]) >>> 0)')
  .replace('1000UL', '1000');
const doTrace = new Function('ctx','millis','kind','value','detail',`with(ctx){${trace}}`);
const doSnapshot = new Function('ctx',`with(ctx){${snapshot}}`);
const doModule = new Function('ctx','millis','module','elapsedMs',`with(ctx){${moduleBody}}`);
let now = 100;
const ctx = {
  traceCapacity: 32, traces: Array(32).fill(null), traceSnapshot: Array(32).fill(null),
  traceHead: 0, traceCount: 0, snapshotCount: 0, PERF_COUNT: 12,
  recentModuleMax: Array(12).fill(0), lastSlowTraceMs: Array(12).fill(0),
  state: {slowTaskThresholdMs: 50},
  consoleEpochMs() { return 1700000000000 + now; },
  trace(kind,value,detail) { doTrace(this,()=>now,kind,value,detail); }
};
for(let i=0;i<40;i++) {now=100+i;ctx.trace(0,i,0);}
doSnapshot(ctx);
assert.equal(ctx.traceCount,32);
assert.equal(ctx.snapshotCount,32);
assert.deepEqual(ctx.traceSnapshot.map(t=>t.value),Array.from({length:32},(_,i)=>8+i),'Ring retains latest 32 entries in time order');
for(let i=40;i<75;i++) ctx.trace(1,i,0);
assert.equal(ctx.traceSnapshot[0].value,8,'A history report remains stable while recording continues');
assert.equal(ctx.traceSnapshot[31].value,39);
now=1000;
const oldCount=ctx.traceHead;
doModule(ctx,()=>now,6,49);
assert.equal(ctx.recentModuleMax[6],49);
assert.equal(ctx.traceHead,oldCount,'Below-threshold calls are not events');
doModule(ctx,()=>now,6,50);
assert.equal(ctx.traceHead,(oldCount+1)%32,'Threshold included');
const capturedHead=ctx.traceHead;
now=1500;
doModule(ctx,()=>now,6,500);
assert.equal(ctx.traceHead,capturedHead,'Repeated slow module limited to one event per second');
assert.equal(ctx.recentModuleMax[6],500,'Rate limit must not hide worst timing');
now=2000;
doModule(ctx,()=>now,6,60);
assert.equal(ctx.traceHead,(capturedHead+1)%32);
const beforeInvalid=ctx.traceHead;
doModule(ctx,()=>now,12,100);
assert.equal(ctx.traceHead,beforeInvalid,'Invalid module ignored');
ctx.lastSlowTraceMs[2]=0xfffffff0;
now=(0xfffffff0+1000)>>>0;
doModule(ctx,()=>now,2,100);
assert.equal(ctx.lastSlowTraceMs[2],now,'Rate limiting survives millis wrap');
console.log('OK RAM trace: bounded ring, chronological snapshot, overwrite isolation, slow threshold/rate limit, recent maxima and rollover');
