const fs = require('fs'), vm = require('vm'), assert = require('assert/strict');
const ui = fs.readFileSync(require('path').join(__dirname, '../../data/www/app.js'), 'utf8');
let sensors = [], configs = {};
const ctx = {
  configSensorsById: () => configs,
  activeSensorsList: () => sensors,
  finiteOrNull: v => v == null || !Number.isFinite(Number(v)) ? null : Number(v),
  sensorStatusInfo: s => ({key:s.ok ? 'ok' : 'lost'})
};
vm.createContext(ctx);
for (const name of ['gridJsyApparentPowerVA', 'unitForValueKey']) {
  const start = ui.indexOf('function ' + name + '(');
  vm.runInContext(ui.slice(start, ui.indexOf('\n}', start) + 2), ctx);
}
configs.meter = {channels:[{id:'clamp2', role:'grid'}, {id:'clamp1', role:'production'}]};
sensors = [{id:'meter', type:'JSY-MK-194T', ok:true, values:{CH1_APPARENT:999, CH2_APPARENT:234 * 1.17}}];
assert.ok(Math.abs(ctx.gridJsyApparentPowerVA() - 273.78) < 0.001);
configs.meter.channels[0].role = 'production';
configs.meter.channels[1].role = 'grid';
assert.equal(ctx.gridJsyApparentPowerVA(), 999, 'follow configured grid channel');
sensors[0].values.CH1_APPARENT = 0;
assert.equal(ctx.gridJsyApparentPowerVA(), 0, 'zero current is valid');
sensors[0].ok = false;
assert.equal(ctx.gridJsyApparentPowerVA(), null, 'do not display stale measurement');
sensors[0].ok = true;
sensors[0].values.CH1_APPARENT = null;
assert.equal(ctx.gridJsyApparentPowerVA(), null, 'missing value is not zero');
sensors[0].values.CH1_APPARENT = -10;
assert.equal(ctx.gridJsyApparentPowerVA(), null, 'apparent power cannot be negative');
sensors[0].values.CH1_APPARENT = 200;
configs.meter.channels[0].role = 'grid';
assert.equal(ctx.gridJsyApparentPowerVA(), null, 'ambiguous grid roles are unavailable');
configs.meter.channels[0].role = 'production';
sensors[0].disabled = true;
assert.equal(ctx.gridJsyApparentPowerVA(), null);
assert.equal(ctx.unitForValueKey('CH2_APPARENT'), 'VA');
assert.equal(ctx.unitForValueKey('CH2_POWER'), 'W');
console.log('PASS: apparent VA follows grid role, zero, absent, stale, disabled, ambiguous roles and units');
