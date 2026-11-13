// Exercise the actual C++ restart predicate at its boundaries, without hardware.
const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../../src/safety/connectivity_timeout.h'), 'utf8');
const body = source.match(/inline bool connectivityWatchdogExpired\([^]*?\)\s*\{([^]*?)\}/)[1];
const expired = new Function('wifiExpected', 'wifiConnected', 'radioExpected', 'wifiDownMs', 'radioSilentMs', body.replace(/(\d+)UL\b/g, '$1'));
for (const wifiExpected of [false, true]) {
  for (const wifiUp of [false, true]) {
    for (const peers of [false, true]) {
      assert.equal(expired(wifiExpected, wifiUp, peers, 300000, 300000), wifiExpected && !wifiUp && peers);
    }
  }
}
assert.equal(expired(true, false, true, 299999, 600000), false, 'WiFi absent for less than five minutes');
assert.equal(expired(true, false, true, 600000, 299999), false, 'A peer spoke less than five minutes ago');
assert.equal(expired(true, false, true, 300000, 300000), true, 'Exact five-minute boundary');
assert.equal(expired(true, false, true, 0, 600000), false, 'WiFi returned and was lost again');
assert.equal(expired(true, false, true, 600000, 0), false, 'Any configured peer resets radio silence');
const rolloverStart = 0xffff0000;
const elapsed = now => (now - rolloverStart) >>> 0;
assert.equal(expired(true, false, true, elapsed((rolloverStart + 299999) >>> 0), 600000), false);
assert.equal(expired(true, false, true, elapsed((rolloverStart + 300000) >>> 0), 600000), true);
console.log('OK connectivity watchdog: AND, disabled/unconfigured links, exact boundary, restoration and millis rollover');
