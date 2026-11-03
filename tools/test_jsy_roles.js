// Execute production role resolution after mechanical Arduino/ArduinoJson adapters.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const read=p=>fs.readFileSync(path.join(__dirname,'..',p),'utf8');
const source=read('src/sensors/sensor_manager.cpp'),ui=read('data/www/app.js');
function body(s,signature){const start=s.indexOf(signature);assert.ok(start>=0,signature);return s.slice(s.indexOf('{',start)+1,s.indexOf('\n}',start));}
const local={NAN:NaN,reading:{available:true,lastValidReadMs:4000},millis:()=>5000,
 channelPower:[920,-300],channelMeasuredPower:[0,-300],channelCurrent:[4,1.3],channelPf:[1,1],channelEnergy:[123,456]};
vm.createContext(local);
vm.runInContext('function channelValue(index,metric){'+body(read('src/sensors/jsy_mk194t_manager.cpp'),'float JSYMK194TManager::channelValue(').replaceAll('3000UL','3000')+'}',local);
assert.equal(local.channelValue(0,'W_RAW'),0);assert.equal(local.channelValue(1,'POWER'),-300);
local.reading.lastValidReadMs=2000;assert.ok(Number.isNaN(local.channelValue(0,'W_RAW')));
local.reading.lastValidReadMs=4000;local.reading.available=false;assert.ok(Number.isNaN(local.channelValue(0,'W_RAW')));
const mergeContext={MAX_REMOTE_SENSOR_VALUES:16,millis:()=>5000};
vm.createContext(mergeContext);
let mergeCode=body(source,'void SensorManager::mergeRemoteValue(')
 .replace(/\b(?:String|uint8_t)\s+/g,'let ')
 .replace(/(key|existingKey)\.trim\(\);/g,'$1 = $1.trim();')
 .replace(/(key|existingKey)\.toUpperCase\(\);/g,'$1 = $1.toUpperCase();')
 .replaceAll('.length()', '.length');
vm.runInContext('function merge(sensor,value,sample=true){'+mergeCode+'}',mergeContext);
const merged={values:[],valueUpdatedMs:[],valueCount:0};
mergeContext.merge(merged,{key:'CH1_W_RAW',value:0},false);
assert.equal(merged.valueUpdatedMs[0],0);
mergeContext.merge(merged,{key:'CH1_W_RAW',value:900});
assert.equal(merged.values[0].value,900);assert.equal(merged.valueUpdatedMs[0],5000);
mergeContext.merge(merged,{key:'CH1_W_RAW',value:0},false);
assert.equal(merged.values[0].value,900,'discovery must not overwrite measured watts');
let code=body(source,'float SensorManager::jsyRoleValue(')
 .replace(/for \(JsonObject (\w+) : ([^)]+\([^)]*\)|\w+)\)/g,'for (const $1 of $2)')
 .replace('for (auto &slot : remoteSensors)','for (const slot of remoteSensors)')
 .replace('RemoteSensorRuntime *runtime = nullptr;','let runtime = null;')
 .replaceAll('remoteTimeoutForSensor(*runtime)','remoteTimeoutForSensor(runtime)').replaceAll('runtime->','runtime.').replace('runtime = &slot','runtime = slot')
 .replace(/JsonArray channels = cfg\["channels"\]\.as<JsonArray>\(\);/,'const channels = cfg.channels || [];')
 .replace('JsonObject channel;','let channel = {};')
 .replace(/\b(?:const )?(?:float|unsigned|uint8_t|uint32_t|bool|String)\s+/g,'let ')
 .replace(/\b(\d+)UL\b/g,'$1').replace('assignedRole.toLowerCase();','assignedRole = assignedRole.toLowerCase();')
 .replace('origin.equalsIgnoreCase("espnow")','origin.toLowerCase() === "espnow"')
 .replaceAll('actuatorId.length()','actuatorId.length')
 .replace('cfg["enabled"] | true','readOr(cfg,"enabled",true)')
 .replace('cfg["type"] | ""','readOr(cfg,"type","")')
 .replace('cfg["source"] | "local"','readOr(cfg,"source","local")')
 .replace('item["id"] | ""','readOr(item,"id","")')
 .replace('channel["role"] | (index == 0 ? "production" : "grid")','readOr(channel,"role",index == 0 ? "production" : "grid")')
 .replace('channel["actuatorId"] | ""','readOr(channel,"actuatorId","")');
const ctx={NAN:NaN,SENSOR_JSY:2,config:{sensors:()=>ctx.fixtures},remoteSensors:[],fixtures:[],
  jsyStarted:true,jsy:{channelValue:(index,metric)=>ctx.physical[index][metric]},physical:[],
  millis:()=>5000,isfinite:Number.isFinite,readOr:(o,k,d)=>o[k]==null?d:o[k],
  configuredEspNowSensorFor:s=>s.config,remoteTimeoutForSensor:()=>1000};
vm.createContext(ctx);vm.runInContext('function resolve(role,metric,actuatorId=""){'+code+'}',ctx);
const channel=(id,role,actuatorId='')=>({id,role,actuatorId});
ctx.fixtures=[{type:'JSY-MK-194T',channels:[channel('clamp1','load','ssr2'),channel('clamp2','grid')]}];
ctx.physical=[{POWER:920,W_RAW:900,CURR:4,ENERGY_POS:123},{POWER:-300,W_RAW:-300}];
assert.equal(ctx.resolve('grid','POWER'),-300);
assert.equal(ctx.resolve('load','POWER','ssr2'),900,'real active watts must not use U*I*PF fallback');
assert.ok(Number.isNaN(ctx.resolve('load','POWER','wrong-output')));
// Swap physical clamps AND roles: semantic measurements keep their meaning.
ctx.fixtures[0].channels=[channel('clamp2','load','ssr2'),channel('clamp1','grid')];
ctx.physical.reverse();
assert.equal(ctx.resolve('grid','POWER'),-300);assert.equal(ctx.resolve('load','POWER','ssr2'),900);
ctx.physical[1].W_RAW=0;assert.equal(ctx.resolve('load','POWER','ssr2'),0,'zero is valid');
ctx.fixtures[0].channels[1].role='load';assert.ok(Number.isNaN(ctx.resolve('grid','POWER')));
ctx.fixtures[0].channels[1].role='grid';ctx.fixtures[0].channels[0].role='grid';
assert.ok(Number.isNaN(ctx.resolve('grid','POWER')),'duplicate roles are unavailable');
ctx.fixtures[0].enabled=false;assert.ok(Number.isNaN(ctx.resolve('grid','POWER')));
const cfg={type:'JSY-MK-194T',source:'espnow',channels:[channel('clamp1','load','ssr2'),channel('clamp2','grid')]};
ctx.fixtures=[cfg];const remote={used:true,sensorType:2,config:cfg,ok:true,timedOut:false,nodeLost:false,
 lastUpdateMs:4900,values:[{key:'CH1_W_RAW',value:800},{key:'CH2_POWER',value:-200}],valueUpdatedMs:[4900,4900],valueCount:2};
ctx.remoteSensors=[remote];assert.equal(ctx.resolve('load','POWER','ssr2'),800);assert.equal(ctx.resolve('grid','POWER'),-200);
remote.valueUpdatedMs[0]=1000;assert.ok(Number.isNaN(ctx.resolve('load','POWER','ssr2')),'fresh other channel must not revive stale watts');
assert.equal(ctx.resolve('grid','POWER'),-200);
remote.valueUpdatedMs[1]=0;assert.ok(Number.isNaN(ctx.resolve('grid','POWER')),'discovery is not a measurement');
remote.valueUpdatedMs[1]=4900;remote.nodeLost=true;assert.ok(Number.isNaN(ctx.resolve('grid','POWER')));
// UI round trip preserves channel metadata and association independent of array order.
const elements={jsyCh1Role:{value:'grid'},jsyCh2Role:{value:'load'},jsyCh2Actuator:{value:'ssr2'}};
const browser={$:id=>elements[id],cache:{actuators:{actuators:[{id:'ssr2',name:'Charge'}]},system:{router:{gridPowerSource:'JSY',gridSetpointW:0}}},esc:String,options:()=>'',jsyClampRoles:['grid','load','production','custom']};
vm.createContext(browser);
for(const name of ['defaultJsyChannels','readJsyChannels','actuatorOptions','jsyChannelsEditor','canonicalRuleCommand','isSystemRuleCommand','actionValueControl','pidActionControls','actionNominalPowerControl','ruleActuator']) {
 const start=ui.indexOf('function '+name+'(');vm.runInContext(ui.slice(start,ui.indexOf('\n}',start)+2),browser);
}
const channels=browser.readJsyChannels({channels:[{id:'clamp2',role:'grid',name:'B',custom:123},{id:'clamp1',role:'load',name:'A',measures:['power']}]});
assert.equal(channels[0].role,'grid');assert.equal(channels[0].name,'A');assert.equal(channels[1].custom,123);
assert.equal(channels[1].actuatorId,'ssr2');
assert.match(browser.jsyChannelsEditor({channels}),/Sans association/);
const html=browser.actionValueControl({command:'setPowerFromSurplus',gridSetpointW:0,maxPercent:80});
assert.match(html,/data-a="gridSetpointW"/);assert.match(html,/data-a="maxPercent"[^>]+value="80"/);assert.match(html,/Capteur réseau/);
assert.match(read('src/communication/espnow_manager.cpp'),/"CH1_W_RAW"/);
assert.match(read('src/web/web_ui.cpp'),/setObjectNumber\(item, "measuredPowerW", measured\)/);
assert.match(read('src/web/web_ui.cpp'),/appendActuatorMeasurements\(out\["actuators"\]\.as<JsonArray>\(\)\)/);
assert.match(ui,/async function sensorsPage\(\)[^]*?cache.actuators = await api\("\/api\/actuators"\)/);
console.log('PASS: inverted roles, raw measured load watts, association, zero, absent/duplicate/disabled roles, stale/lost remote channel, discovery, UI round trip and routing controls');
