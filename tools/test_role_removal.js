const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const root = path.join(__dirname, '..');
const read = name => fs.readFileSync(path.join(root, name), 'utf8');
const config = read('src/config/config_manager.cpp');
// Execute the production migration body with a minimal JsonObject adapter.
let body = config.match(/static bool removeRetiredSettings\([^]*?\) \{([^]*?)\n\}/)[1]
  .replace(/const char \*(deviceKeys|systemKeys)\[\] = \{([^]*?)\};/g, 'const $1 = [$2];')
  .replace('bool changed = false;', 'let changed = false;')
  .replace(/strcmp\(path, (DEVICE_PATH|SYSTEM_PATH)\) == 0/g, 'path === $1')
  .replace(/for \(const char \*key : (deviceKeys|systemKeys)\)/g, 'for (const key of $1)');
const migrate = new Function('path', 'root', 'DEVICE_PATH', 'SYSTEM_PATH', body);
function clean(file, doc) {
  return migrate('/config/' + file + '.json', {
    containsKey: key => Object.hasOwn(doc, key), remove: key => delete doc[key]
  }, '/config/device.json', '/config/system.json');
}
for (const role of ['MASTER', 'BACKUP', 'STANDALONE', 'NODE_SENSOR', 'NODE_ACTUATOR', 'NODE_MIXED']) {
  const doc = {role, redundancyRole: role, functionalRole: 'MIXED', deviceName: 'Cuisine', nodeId: 42, custom: {role: 'preserve'}};
  assert.equal(clean('device', doc), true);
  assert.deepEqual(doc, {deviceName: 'Cuisine', nodeId: 42, custom: {role: 'preserve'}});
  assert.equal(clean('device', doc), false, 'migration must be idempotent');
}
const system = {heartbeatIntervalMs: 300, takeoverTimeoutMs: 1000, redundancy: {}, role: 'BACKUP', functionalRole: 'MIXED', redundancyRole: 'BACKUP', wifi: {ssid: 'maison', password: 'keep'}, router: {kp: 0.02}, mqtt: {enabled: true}};
assert.equal(clean('system', system), true);
assert.deepEqual(system, {wifi: {ssid: 'maison', password: 'keep'}, router: {kp: 0.02}, mqtt: {enabled: true}});
for (const file of ['sensors', 'actuators', 'rules']) {
  const doc = {role: 'temperature_1', items: [{role: 'grid'}]};
  assert.equal(clean(file, doc), false);
  assert.deepEqual(doc, {role: 'temperature_1', items: [{role: 'grid'}]});
}
for (const file of ['device', 'system']) {
  const example = JSON.parse(read('config_examples/' + file + '.example.json'));
  assert.equal(clean(file, example), false, 'examples must already be clean');
}
const ui = read('data/www/app.js');
assert.doesNotMatch(ui, /MASTER|BACKUP|NODE_SENSOR|NODE_ACTUATOR|NODE_MIXED|redundancyRole|functionalRole|devRole|Redondance/);
assert.doesNotMatch(read('data/www/index.html'), />Secours</);
for (const file of ['src/runtime/runtime_state.cpp', 'src/web/web_ui.cpp', 'src/communication/espnow_manager.cpp', 'src/communication/mqtt_manager.cpp']) {
  assert.doesNotMatch(read(file), /(?:out|doc|deviceOut|node)\["(?:role|redundancyRole|functionalRole|isActiveMaster|masterAlive|epoch)"\]/);
}
assert.match(config, /written == expected && file.size\(\) == expected/);
assert.match(config, /LittleFS.rename\(temporary.c_str\(\), path\)/);
assert.doesNotMatch(read('RouteurSolaireESP32.ino'), /Redundancy|redundancy/);
assert.match(read('src/communication/espnow_manager.cpp'), /if \(!isPeerKnown\(mac\)\) return false/);
console.log('PASS: all legacy roles migrated, unrelated settings preserved, idempotent cleanup, API/UI removal, peer authorisation retained');
// Smoke-render the shipped pages with legacy data to catch dangling role references.
const vm = require('vm');
const elements = new Map();
const element = id => {
  if (!elements.has(id)) elements.set(id, {innerHTML:'', textContent:'', value:'', style:{}, classList:{add(){}, remove(){}, toggle(){}}, querySelectorAll(){return []}, getAttribute(){return null}, addEventListener(){}});
  return elements.get(id);
};
const context = {
  console, Date, Math, JSON, URLSearchParams,
  localStorage:{getItem:()=>null, setItem(){}, removeItem(){}},
  document:{getElementById:element, querySelectorAll:()=>[], querySelector:()=>null, addEventListener(){}, documentElement:{clientWidth:1280}, body:{classList:{add(){}, remove(){}}}},
  setTimeout:()=>1, clearTimeout(){}, setInterval:()=>1, clearInterval(){},
  requestAnimationFrame:()=>1, cancelAnimationFrame(){}, alert(){}, confirm:()=>false,
  location:{hostname:'test.local'}, navigator:{}, devicePixelRatio:1, innerWidth:1280, innerHeight:900, addEventListener(){}, matchMedia:()=>({matches:false})
};
context.window=context;
vm.createContext(context);
vm.runInContext(ui.replace(/render\(\);\s*setInterval\(pollActuatorRuntime, 1000\);\s*scheduleDashboardRefresh\(\);\s*refresh\(\);\s*$/, ''), context);
const legacyDevice={deviceName:'Test',role:'BACKUP',redundancyRole:'BACKUP',functionalRole:'MIXED'};
const responses={
  '/api/device':legacyDevice, '/api/system':{wifi:{ssid:'maison'},router:{}},
  '/api/sensors':{sensors:[],ds18b20:[]}, '/api/actuators':{actuators:[]},
  '/api/system-info':{device:legacyDevice, role:'BACKUP'},
  '/api/espnow':{roleFlags:15,redundancyRole:'MASTER',functionalRole:'MIXED',discoveredNodes:[{nodeId:2,nodeName:'Test',mac:'AA:BB:CC:DD:EE:FF',redundancyRole:'BACKUP',functionalRole:'SENSOR'}],peers:[]}
};
context.api=async url=>{assert.ok(responses[url],url);return responses[url];};
context.cache.sensors=responses['/api/sensors'];
context.cache.actuators=responses['/api/actuators'];
(async()=>{
  for(const sample of [{safetyLevel:'INIT'},{safetyLevel:'OK',gridPowerW:-500},{safetyLevel:'CRITICAL',safetyTripped:true}]) {
    Object.assign(context.state,sample,{role:'BACKUP'});
    assert.doesNotMatch(context.dashboard(),/MASTER|BACKUP|MIXED|Role fonctionnel|Role redondance/);
  }
  for(const name of ['settingsPage','espNowPage','systemPage']) {
    await context[name]();
    const html=element('app').innerHTML;
    assert.ok(html.length>500,name);
    assert.doesNotMatch(html,/MASTER|BACKUP|MIXED|Role fonctionnel|Role redondance|devRole|Redondance/,name);
  }
  console.log('PASS: dashboard INIT/OK/CRITICAL, settings, ESP-NOW and system render without roles, including legacy API payloads');
})().catch(error=>{console.error(error);process.exitCode=1;});
