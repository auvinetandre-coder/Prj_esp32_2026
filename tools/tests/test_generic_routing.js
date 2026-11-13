const fs = require('fs');
const path = require('path');
const assert = require('assert/strict');
const read = p => fs.readFileSync(path.join(__dirname, '../..', p), 'utf8');
const migration = read('src/config/legacy_config_migration.h');
// Execute the production migration, translating the small ArduinoJson adapter to JS.
let keyBody = migration.match(/inline bool migrateConfigKey\([^]*?\) \{([^]*?)\n\}/)[1]
  .replaceAll('object.containsKey(oldKey)', 'Object.hasOwn(object, oldKey)')
  .replaceAll('object.containsKey(newKey)', 'Object.hasOwn(object, newKey)')
  .replace('object[newKey].set(object[oldKey]);', 'object[newKey] = object[oldKey];')
  .replace('object.remove(oldKey);', 'delete object[oldKey];');
const migrateConfigKey = new Function('object', 'oldKey', 'newKey', 'if (!object) return false;'+keyBody);
let body = migration.match(/inline bool migrateGenericConfig\([^]*?\) \{([^]*?)\n\}/)[1]
  .replaceAll('bool changed = false;', 'let changed = false;')
  .replace(/strcmp\(path, ("[^"]+")\) == 0/g, 'path === $1')
  .replace(/JsonObject (\w+) = ([^;]+);/g, 'const $1 = $2 || {};')
  .replace(/for \(JsonObject (\w+) : ([^\n]+?)\.as<JsonArray>\(\)\)/g, 'for (const $1 of ($2 || []))')
  .replace(/for \(const char \*(\w+) : \{([^}]+)\}\)/g, 'for (const $1 of [$2])')
  .replace(/const char \*(\w+)\[\] = \{([^}]+)\};/g, 'const $1 = [$2];')
  .replace(/for \(size_t /g, 'for (let ')
  .replaceAll(' | ', ' || ')
  .replace(/actuator\.containsKey\(([^)]+)\)/g, 'Object.hasOwn(actuator, $1)')
  .replace(/actuator\.remove\(([^)]+)\)/g, 'delete actuator[$1]');
const migrate = new Function('path', 'root', 'migrateConfigKey', body);
const clean = (name, doc) => migrate('/config/'+name+'.json', doc, migrateConfigKey);
const system = {wifi:{ssid:'heater',password:'tankSafetyC'},router:{heaterMaxPowerW:2200,outputMaxPowerW:1800,tankMaxC:61,tankSafetyC:72},safety:{blockOnMissingTopSensor:true},dashboard:{graphs:[{series:[{valueKey:'heaterPowerW',label:'Custom'},{valueKey:'tankTempC',label:'Ballon'}]}]}};
assert.ok(clean('system',system));
assert.deepEqual(system.router,{outputMaxPowerW:1800,temperatureMaxC:61,tempSafetyMaxC:72});
assert.deepEqual(system.wifi,{ssid:'heater',password:'tankSafetyC'});
assert.equal(system.dashboard.graphs[0].series[0].valueKey,'outputPowerW');
assert.equal(system.dashboard.graphs[0].series[1].valueKey,'primaryTemperatureC');
assert.deepEqual(system.safety,{blockOnMissingPrimarySensor:true});
assert.ok(!clean('system',system),'idempotent');
const actuators={actuators:[{id:'ssr1_water_heater',name:'My heater',usage:'water_heater',gpio:26,critical:true,heater:true},{id:'pump',usage:'auxiliary',waterHeater:true,isWaterHeater:false}]};
assert.ok(clean('actuators',actuators));
assert.deepEqual(actuators.actuators,[{id:'ssr1_water_heater',name:'My heater',usage:'routing',gpio:26,critical:true},{id:'pump',usage:'auxiliary'}]);
assert.ok(!clean('actuators',actuators));
const sensors={ds18b20:[{id:'custom',role:'ballon_haut',critical:true},{id:'second',role:'ballon_milieu'},{id:'third',role:'ballon_bas'}],sensors:[{id:'remote',role:'grid'}]};
assert.ok(clean('sensors',sensors));
assert.deepEqual(sensors.ds18b20.map(s=>s.role),['temperature_1','temperature_2','temperature_3']);
assert.equal(sensors.ds18b20[0].critical,true);
assert.equal(sensors.sensors[0].role,'grid');
const rules={rules:[{id:'custom',name:'My heater rule',actions:[{actuatorId:'ssr1_water_heater',command:'setPowerFromSurplus',maxHeaterPowerW:2200}]}]};
assert.ok(clean('rules',rules));
assert.deepEqual(rules.rules[0].actions[0],{actuatorId:'ssr1_water_heater',command:'setPowerFromSurplus',maxOutputPowerW:2200});
assert.equal(rules.rules[0].name,'My heater rule');
assert.ok(!clean('rules',rules));
const safety=read('src/safety/safety_manager.cpp');
let safetyBody=safety.match(/bool SafetyManager::configError\(\) \{([^]*?)\n\}/)[1]
 .replace('const String target','const target').replace('bool found','let found')
 .replace('for (JsonObject actuator : config.actuators())','for (const actuator of config.actuators())')
 .replace('String id','const id').replaceAll(' | ',' || ').replaceAll('.length()', '.length');
const configError=new Function('config',safetyBody);
for(const id of ['pump','fan','ssr1_output'])assert.equal(configError({routingActuatorId:()=>id,actuators:()=>[{id,enabled:true}]}),false);
assert.equal(configError({routingActuatorId:()=>'',actuators:()=>[]}),false);
assert.equal(configError({routingActuatorId:()=> 'missing',actuators:()=>[]}),true);
assert.equal(configError({routingActuatorId:()=> 'pump',actuators:()=>[{id:'pump',enabled:false}]}),true);
let missing=safety.match(/bool SafetyManager::primarySensorMissing\(\) \{([^]*?)\n\}/)[1]
 .replace('JsonArray ds = config.sensorsDoc()["ds18b20"].as<JsonArray>();','const ds = config.sensorsDoc().ds18b20;')
 .replace('size_t index','let index').replace('const size_t slot','const slot')
 .replace('for (JsonObject sensor : ds)','for (const sensor of ds)').replaceAll(' | ',' || ');
const primaryMissing=new Function('config','state',missing);
assert.equal(primaryMissing({sensorsDoc:()=>({ds18b20:[{id:'custom-probe',role:'temperature_1',enabled:true}]})},{ds18b20Available:[false]}),true);
assert.equal(primaryMissing({sensorsDoc:()=>({ds18b20:[{id:'custom-probe',role:'temperature_1',enabled:true}]})},{ds18b20Available:[true]}),false);
assert.doesNotMatch(read('data/www/app.js'),/heaterMaxPowerW|tankMaxC|tankSafetyC|blockOnMissingTopSensor|ssr1_water_heater/);
assert.doesNotMatch(read('src/logic/automate_manager.cpp'),/ssr1_output|water_heater/);
assert.match(read('src/config/config_manager.cpp'),/migrateRetiredConfig\(RULES_PATH, rulesConfig\)/);
console.log('PASS: generic migration preserves values, identities, links, custom names and GPIO; new keys win; idempotence; arbitrary/disabled/missing outputs; custom primary probe safety');

const vm = require('vm');
const lite = read('src/web/web_ui.cpp').match(/R"HTML\(([^]*?)\)HTML"/)[1];
const liteJs = lite.match(/<script>([^]*?)<\/script>/)[1];
new vm.Script(liteJs);
console.log('PASS: embedded Lite JavaScript extracted from firmware parses');

let hot=safety.match(/bool SafetyManager::temperatureHigh\(float safetyC\) \{([^]*?)\n\}/)[1]
 .replace('size_t index','let index')
 .replace('for (JsonObject sensor : config.sensorsDoc()["ds18b20"].as<JsonArray>())','for (const sensor of config.sensorsDoc().ds18b20)')
 .replaceAll(' | ',' || ').replaceAll('isnan(', 'Number.isNaN(');
const temperatureHigh=new Function('config','state','safetyC',hot);
const thermal={ds18b20Temps:[20,75,30],temperature1C:NaN,temperature2C:NaN,temperature3C:NaN};
assert.equal(temperatureHigh({sensorsDoc:()=>({ds18b20:[{enabled:true},{enabled:true},{enabled:true}]})},thermal,70),true);
assert.equal(temperatureHigh({sensorsDoc:()=>({ds18b20:[{enabled:true},{enabled:false},{enabled:true}]})},thermal,70),false);
thermal.ds18b20Temps=[20,25,30];
assert.equal(temperatureHigh({sensorsDoc:()=>({ds18b20:[{enabled:true},{enabled:true},{enabled:true}]})},thermal,70),false);
console.log('PASS: high temperature on any configured probe cuts power; disabled probe excluded; normal temperatures accepted');

let selector=read('data/www/app.js').match(/function routingActuatorOptions\(selected\) \{([^]*?)\n\}/)[1];
const routingOptions=new Function('cache','esc','selected',selector);
const escape=s=>String(s).replaceAll('<','&lt;').replaceAll('"','&quot;');
assert.match(routingOptions({actuators:{actuators:[]}},escape,'missing'),/selected value="missing"[^]*indisponible/);
assert.match(routingOptions({actuators:{actuators:[{id:'custom',enabled:false}]}},escape,'custom'),/value="custom" selected[^]*desactive/);
assert.doesNotMatch(routingOptions({actuators:{actuators:[]}},escape,'<script>'),/<script>/);
console.log('PASS: settings preserve missing target and show disabled target instead of silently clearing selection');
