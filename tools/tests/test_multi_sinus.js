const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

// Execute the portable C++ function bodies with the same integer arithmetic.
const source = fs.readFileSync(path.join(__dirname, "../../src/actuators/ssr_frame.h"), "utf8");
function body(name) {
  const signature = source.indexOf(name + "(");
  return source.slice(source.indexOf("{", signature) + 1, source.indexOf("\n}", signature));
}
const frameFor = vm.runInThisContext("(percent) => {" + body("ssrFrameFor")
  .replace(/\b(?:const )?uint(?:8|32)_t /g, "let ")
  .replace(/uint(?:8|32)_t\((\w+)\)/g, "$1")
  .replace(/return \{([01]), 1\}/g, "return {on: $1, total: 1}")
  .replace("((target + 50 * quantum) / (100 * quantum))", "Math.floor((target + 50 * quantum) / (100 * quantum))") + "}");
const halfWaveAdvance = vm.runInThisContext("(pulse, percent) => {" + body("ssrHalfWaveAdvance") + "}");
for (let percent = 0; percent <= 100; percent++) {
  const pulse = {credit: 0, phase: false, lastOnPhase: false};
  let onCount = 0;
  let lastOnPhase = false;
  for (let step = 0; step < 10000; step++) {
    if (!halfWaveAdvance(pulse, percent)) continue;
    onCount++;
    if (percent < 100) assert.notEqual(pulse.phase, lastOnPhase);
    lastOnPhase = pulse.phase;
  }
  assert.ok(Math.abs(onCount - percent * 100) <= 1);
}
const stoppedPulse = {credit: 99, phase: false, lastOnPhase: false};
assert.equal(halfWaveAdvance(stoppedPulse, 0), false);
assert.equal(stoppedPulse.credit, 0);
console.log("PASS: demi-sinus duty and parity for every whole percentage");

const advance = vm.runInThisContext("(frame, requested, cursor) => {" + body("ssrAdvance")
  .replace(/\bstep\b/g, "cursor.step").replace("const bool on", "const on") + "}");
function channel() {
  return {frame: {on: 0, total: 1}, cursor: {step: 0}};
}
function tick(ch, requested) {
  return Number(advance(ch.frame, requested, ch.cursor));
}
for (const percent of [0, 1, 5, 10, 25, 50, 75, 99, 100]) {
  const requested = frameFor(percent);
  const ch = channel();
  const ticks = requested.total * 100;
  let on = 0;
  for (let i = 0; i < ticks; i++) on += tick(ch, requested);
  assert.equal(on, requested.on * 100);
  assert.ok(Math.abs(on / ticks - percent / 100) < 0.004);
  console.log(percent + "%", requested);
}
// Literal transcription of the original F1ATB search, including float32.
function f1atbFrame(percent) {
  const target = Math.fround(percent / 100);
  for (let total = 20; total <= 100; total++) {
    for (let on = 0; on <= total; on++) {
      if (!(total % 2) && on % 2) continue;
      const error = Math.abs(Math.fround(Math.fround(on / total) - target));
      if (error < 0.004) return {on, total};
    }
  }
}
for (let percent = 0; percent <= 100; percent++) {
  const actual = frameFor(percent);
  const reference = f1atbFrame(percent);
  assert.equal(actual.on * reference.total, reference.on * actual.total);
  if (percent > 0 && percent < 100) assert.deepEqual(actual, reference);
}
const first = channel();
const second = channel();
const low = frameFor(10);
const high = frameFor(90);
assert.equal(tick(first, frameFor(0)), 0);
assert.equal(tick(first, frameFor(100)), 1);
assert.equal(tick(first, frameFor(0)), 0);
for (const [from, to] of [[low, high], [high, low]]) {
  const ch = channel();
  assert.equal(tick(ch, from), 1);
  let count = 1;
  for (let i = 1; i < from.total; i++) count += tick(ch, to);
  assert.equal(count, from.on, "finish the current frame before updating");
  count = 0;
  for (let i = 0; i < to.total; i++) count += tick(ch, to);
  assert.equal(count, to.on);
}
let firstOn = 0;
let secondOn = 0;
for (let i = 0; i < 200; i++) {
  firstOn += tick(first, low);
  secondOn += tick(second, high);
}
assert.equal(firstOn, 20);
assert.equal(secondOn, 180);
console.log("PASS: duty ratios, transitions and independent channels");

const managerSource = fs.readFileSync(path.join(__dirname, "../../src/actuators/actuator_manager.cpp"), "utf8");
const isrStart = managerSource.indexOf("void IRAM_ATTR ActuatorManager::onSsrTick");
const isrBody = managerSource.slice(managerSource.indexOf("{", isrStart) + 1, managerSource.indexOf("\n}", isrStart));
const runIsr = vm.runInThisContext("(manager, now, write) => {" + isrBody
  .replace(/^.*(?:static_cast|portENTER_CRITICAL_ISR|portEXIT_CRITICAL_ISR|esp_timer_get_time).*$/gm, "")
  .replace("const uint32_t elapsed = uint32_t(now - manager.ssrLastTickUs)", "const elapsed = now - manager.ssrLastTickUs")
  .replace("elapsed / 10000 - 1", "Math.floor(elapsed / 10000) - 1")
  .replace("auto &ssr :", "const ssr of")
  .replace("uint32_t(now / 1000)", "Math.floor(now / 1000)")
  .replace("const bool on =", "const on =")
  .replace("ssrHalfWaveAdvance(ssr.halfWave, ssr.halfWavePercent)", "manager.halfWaveAdvance(ssr.halfWave, ssr.halfWavePercent)")
  .replace("ssrAdvance(ssr.frame, ssr.requested, ssr.step)", "manager.advance(ssr.frame, ssr.requested, ssr)")
  .replaceAll("gpio_ll_set_level(&GPIO, ", "write(") + "}");
function manager() {
  return {
    advance, halfWaveAdvance, ssrLastTickUs: 0, ssrMaxIntervalUs: 0, ssrMissedSteps: 0, ssrTimingFault: false,
    timedSsr: [true, false].map((activeHigh, pin) => ({
      halfWave: {credit: 0, phase: false, lastOnPhase: false},
      pin, activeHigh, on: false, step: 0, expiresUs: pin ? 1000000 : 20000,
      frame: {on: 0, total: 1}, requested: frameFor(100)
    }))
  };
}
const writes = [];
const write = (pin, level) => writes.push([pin, level]);
const engine = manager();
runIsr(engine, 10000, write);
assert.deepEqual(writes.splice(0), [[0, true], [1, false]]);
runIsr(engine, 20000, write);
assert.deepEqual(writes.splice(0), [[0, false]], "lease expiry cuts only the expired channel");
runIsr(engine, 50000, write);
assert.deepEqual(writes.splice(0), [[1, true]], "missed ticks cut an active-low channel");
assert.equal(engine.ssrTimingFault, true);
assert.equal(engine.ssrMissedSteps, 2);
runIsr(engine, 60000, write);
assert.deepEqual(writes, [], "a later tick cannot restart after the timing fault");
console.log("PASS: lease expiry, output polarity and latched timing fault");

const halfWaveEngine = manager();
halfWaveEngine.timedSsr[1].pin = -1;
Object.assign(halfWaveEngine.timedSsr[0], {demiSinus: true, halfWavePercent: 50, expiresUs: 40000});
for (let now = 10000; now <= 50000; now += 10000) runIsr(halfWaveEngine, now, write);
assert.deepEqual(writes.splice(0), [[0, true], [0, false]], "half-wave expires while ON");
