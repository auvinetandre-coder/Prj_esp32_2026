// Verify the actual simulation-transition block without accessing hardware.
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const assert = require("assert/strict");
const source = fs.readFileSync(path.join(__dirname, "../src/actuators/actuator_manager.cpp"), "utf8");
const start = source.indexOf("  if (!previousSimulationMode && simulationMode)");
assert.ok(start >= 0);
const end = source.indexOf("  scheduleTriacFire();", start);
const block = source.slice(start, end).replace(/(\d+)UL/g, "$1");
let cuts = 0;
const context = {
  previousSimulationMode: false, simulationMode: true,
  realModeHoldUntilMs: 0, now: 10000, safetyLocked: false,
  allOff: () => {cuts++;}, state: {addLog: () => {}}
};
vm.createContext(context);
const run = () => vm.runInContext("(function () {" + block + "})()", context);
run();
assert.equal(cuts, 1, "entering simulation must physically release outputs");
assert.equal(context.previousSimulationMode, true);
run();
assert.equal(cuts, 1, "steady simulation must not repeatedly reset simulated commands");
context.simulationMode = false;
run();
assert.equal(cuts, 3, "leaving simulation cuts outputs and begins OFF hold");
assert.equal(context.realModeHoldUntilMs, 12000);
context.now = 11000;
run();
assert.equal(cuts, 4, "real outputs remain OFF during hold");
context.now = 12000;
run();
assert.equal(context.realModeHoldUntilMs, 0);
assert.equal(cuts, 4, "hold ends without another transition");
console.log("PASS: physical OFF on simulation entry, steady simulation, exit and 2-second OFF hold");
