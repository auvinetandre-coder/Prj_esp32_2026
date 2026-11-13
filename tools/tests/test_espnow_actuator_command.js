// Execute production acceptance, lease and arbitration blocks without radio hardware.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert/strict');
const read = p => fs.readFileSync(path.join(__dirname, '../..', p), 'utf8');
const radio = read('src/communication/espnow_manager.cpp');
const actuator = read('src/actuators/actuator_manager.cpp');
const automate = read('src/logic/automate_manager.cpp');
function body(source, signature) {
  const start = source.indexOf(signature); assert.ok(start >= 0, signature);
  return source.slice(source.indexOf('{', start) + 1, source.indexOf('\n}', start));
}
const translate = s => s.replace(/\bActuator \*channel = channelForExisting\(id\);/, 'var channel = channels[id];')
  .replace(/channel->/g, 'channel.')
  .replaceAll('uint32_t(timeoutNow - channel.lastCommandMs)', '((timeoutNow - channel.lastCommandMs) >>> 0)')
  .replaceAll('uint32_t(millis() - channel.lastCommandMs)', '((millis() - channel.lastCommandMs) >>> 0)')
  .replace(/String\(channel.id\)/g, 'String(channel.id)')
  .replace(/\.length\(\)/g, '.length').replace(/(\d+(?:\.\d+)?)f\b/g, '$1');
let now = 100000, known = true;
const channel = {id: 'ssr', lastCommandMs: now, espNowLeaseMs: 1000, commandPercent: 60, sharedControl:{reset(){}}};
const calls = [], events = [];
const context = {channels: {ssr: channel}, channel, timeoutNow: now,
  state: {safetyTripped: false, safetyActive: false, activePriority: 'IDLE', logEvent: (...args) => events.push(args), addLog() {}},
  millis: () => now, isPeerKnown: () => known,
  publishPower: (id, value) => calls.push(['publish', id, value]),
  actuators: {allOff: () => calls.push(['allOff']), setMode: (...a) => calls.push(['mode', ...a]), command: (...a) => calls.push(['command', ...a])}};
vm.createContext(context);
vm.runInContext('function accept(message,mac){' + body(radio, 'bool EspNowManager::acceptActuatorCommand(') + '}\n' +
  'function held(id){' + translate(body(actuator, 'bool ActuatorManager::hasEspNowCommand(')) + '}\n' +
  'function apply(command){' + translate(body(automate, 'void AutomateManager::applyFinalCommand(')) + '}', context);
context.actuators.hasEspNowCommand = context.held;
// Fresh commands must work regardless of which ESP booted first, including wrap.
for (const timestamp of [0, 99999, 5000000, 0xfffffff0]) assert.equal(context.accept({timestamp, ttlMs: 1000}, 'peer'), true);
known = false; assert.equal(context.accept({}, 'peer'), false); known = true;
context.state.safetyTripped = true; assert.equal(context.accept({}, 'peer'), false); context.state.safetyTripped = false;
const command = {enabled: true, actuatorId: 'ssr', command: 'setPower', commandPercent: 0};
for (const priority of ['IDLE', 'AUTO_ROUTING', 'RULE_ENGINE']) {
  context.state.activePriority = priority; context.apply(command);
  assert.equal(calls.length, 0, 'local automation must preserve remote lease');
}
context.state.activePriority = 'MANUAL_FORCE'; context.apply(command); assert.equal(calls.at(-1)[0], 'command');
context.state.activePriority = 'IDLE';
context.apply({...command, command: 'allOff'}); assert.equal(calls.at(-1)[0], 'allOff');
context.state.safetyActive = true; context.apply(command); assert.equal(calls.at(-1)[0], 'allOff'); context.state.safetyActive = false;
context.apply({...command, actuatorId: 'other'}); assert.equal(calls.at(-1)[1], 'other');
const expiryStart = actuator.indexOf('    if (channel->espNowLeaseMs &&');
const expiryEnd = actuator.indexOf('    if (channel->commandPercent > 0', expiryStart);
assert.ok(expiryStart >= 0 && expiryEnd > expiryStart);
function expire() { context.timeoutNow = now; vm.runInContext(translate(actuator.slice(expiryStart, expiryEnd)), context); }
now += 999; expire(); assert.equal(channel.commandPercent, 60); assert.equal(context.held('ssr'), true);
now++; expire(); assert.equal(channel.commandPercent, 0); assert.equal(channel.espNowLeaseMs, 0); assert.equal(events.length, 1);
context.apply(command); assert.equal(calls.at(-1)[0], 'command', 'automation resumes after expiry');
Object.assign(channel, {lastCommandMs: 0xfffffff0, espNowLeaseMs: 1000, commandPercent: 75});
now = 20; expire(); assert.equal(context.held('ssr'), true);
now = 984; expire(); assert.equal(channel.commandPercent, 0, 'watchdog survives millis wrap');
Object.assign(channel, {lastCommandMs: 0, espNowLeaseMs: 1000, commandPercent: 30});
now = 1000; expire(); assert.equal(channel.commandPercent, 0, 'reception at millis zero still expires');
assert.match(radio, /msg\.mode, msg\.ttlMs, peer, msg\.reservedCounter\)/);
assert.match(actuator, /ttlMs && ttlMs < watchdogMs \? ttlMs : watchdogMs/);
const leaseStart = actuator.indexOf('  channel->espNowLeaseMs = ttlMs &&');
const leaseEnd = actuator.indexOf('  self->limitTimedSsrLease(channel);', leaseStart);
assert.ok(leaseStart >= 0 && leaseEnd > leaseStart);
for (const [ttlMs, watchdogMs, expected] of [[1000, 5000, 1000], [9000, 5000, 5000], [0, 5000, 5000], [0, 0, 1000]]) {
  Object.assign(context, {ttlMs, watchdogMs});
  vm.runInContext(translate(actuator.slice(leaseStart, leaseEnd)).replace(/(\d+)UL/g, '$1'), context);
  assert.equal(channel.espNowLeaseMs, expected);
}
assert.match(actuator, /self->state\.manualCommandPending && self->state\.manualCommandActuatorId == actuatorId/);
assert.match(actuator, /self->state\.safetyTripped/);
assert.match(body(actuator, 'void ActuatorManager::allOff('), /espNowLeaseMs = 0/);
console.log('PASS: unsynchronised boots, authorised peer, Safety, per-target arbitration, manual/OFF, local TTL, loss and clock wrap');
