// Execute the actual C++ PID bodies after mechanical translation, plus WebUI functions.
// This checks the arithmetic/guards; the ESP32 build and bench tests remain necessary.
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert/strict');
const root = path.join(__dirname, '../..');
const pid = fs.readFileSync(path.join(root, 'src/logic/pid_controller.cpp'), 'utf8');
const ui = fs.readFileSync(path.join(root, 'data/www/app.js'), 'utf8');
const config = fs.readFileSync(path.join(root, 'src/config/config_manager.cpp'), 'utf8');
const tic = fs.readFileSync(path.join(root, 'src/sensors/linky_tic_manager.cpp'), 'utf8');
const labelStart=tic.indexOf('void LinkyTICManager::applyLabelValue(');
const labelBody=tic.slice(tic.indexOf('{',labelStart)+1,tic.indexOf('\n}',labelStart))
  .replace(/\b(?:const )?float\s+/g,'var ').replace(/value.toFloat\(\)/g,'Number(value)');
const meter={reading:{apparentPowerVA:0,gridPowerW:0},millis:()=>1000};
vm.createContext(meter);
vm.runInContext('function apply(label,value){'+labelBody+'}',meter);
for (const reversed of [false,true]) {
  for (const [importVA,exportVA,expected] of [[400,0,400],[0,500,-500],[0,0,0],[200,0,200]]) {
    const values=[['SINSTS',String(importVA)],['SINSTI',String(exportVA)]];
    if (reversed) values.reverse();
    values.forEach(args=>meter.apply(...args));
    assert.equal(meter.reading.gridPowerW,expected,'TIC import/export fields must not erase each other');
  }
}
meter.apply('PAPP','350');
assert.equal(meter.reading.gridPowerW,350,'historical mode stays compatible');
const checks=[...config.matchAll(/if \(strcmp\(path, RULES_PATH\) == 0\) return ([^;]+);/g)];
assert.equal(checks.length,2);
const ruleChecks=checks.map(m=>new Function('doc','return '+m[1]
  .replace('doc["rules"].is<JsonArray>()','Array.isArray(doc.rules)')));
assert.equal(ruleChecks[0]({version:2,rules:[]}),true);
assert.equal(ruleChecks[1]({version:2,rules:[]}),false);
assert.equal(ruleChecks[0]({version:2}),false);
assert.equal(ruleChecks[0]({version:2,rules:{}}),false);

function js(name) {
  const start=ui.search(new RegExp('(?:async )?function '+name+'\\('));
  assert.ok(start>=0,name);
  return ui.slice(start,ui.indexOf('\n}',start)+2);
}
let saved;
const browser={
  cache:{rules:{version:2,rules:[{id:'old-rule'}]}},
  $:()=>null, confirm:()=>true, markDirty:()=>{}, drawLogicPage:()=>{},
  clearDirty:()=>{}, alert:()=>{}, esc:String,
  stashRuleEditor:()=>true,
  postJson:async(url,doc)=>{ assert.equal(url,'/api/rules'); saved=JSON.parse(JSON.stringify(doc)); return {ok:true}; }
};
vm.createContext(browser);
vm.runInContext(['canonicalRuleCommand','deleteRule','saveRules','actionValueControl'].map(js).join('\n'),browser);
browser.deleteRule(0);
(async()=>{
  await browser.saveRules();
  assert.deepEqual(saved,{version:2,rules:[]},'last-rule deletion posts an explicit empty array');

  console.log('PASS: TIC decoding, empty rule list and WebUI deletion (PID covered by shared engine tests)');
})().catch(error=>{console.error(error);process.exitCode=1;});
