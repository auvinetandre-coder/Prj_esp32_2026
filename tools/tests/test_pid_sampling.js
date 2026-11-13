const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const src=fs.readFileSync(path.join(__dirname,'../../src/sensors/sensor_manager.cpp'),'utf8');
const start=src.indexOf('SensorManager::PowerSample SensorManager::gridPowerSample(');
let body=src.slice(src.indexOf('{',start)+1,src.indexOf('\n}',start));
body=body.replace('PowerSample result;','let result = sample();').replace('PowerSample jsySample, ticSample;','let jsySample, ticSample;')
 .replace(/for \(JsonObject (\w+) : config.sensors\(\)\)/g,'for (const $1 of config.sensors())')
 .replace(/for \(JsonObject (\w+) : ([^\n]+?)\.as<JsonArray>\(\)\)/g,'for (const $1 of ($2 || []))')
 .replace('for (auto &slot : remoteSensors)','for (const slot of remoteSensors)')
 .replace(/JsonObject (\w+);/g,'let $1 = {};')
 .replace(/\b(?:const )?(?:String|bool|unsigned|int|uint8_t|uint32_t|PowerSample)\s+/g,'let ')
 .replace(/(cfg\["remoteKey"\]) \| (cfg\["key"\]) \| "ALL"/g,'first($1,$2,"ALL")')
 .replace(/cfg\["(measurementTimeoutMs|timeoutMs)"\] \| (\d+)UL/g,'first(cfg["$1"],$2)')
 .replace(/((?:cfg|item|channel)\["[^"]+"\]) \| ("[^"]*"|true)/g,'first($1,$2)')
 .replace(/channel\["role"\] \| \(n == 0 \? "production" : "grid"\)/g,'first(channel["role"],n == 0 ? "production" : "grid")')
 .replace(/cfg.isNull\(\)/g,'Object.keys(cfg).length === 0').replace('origin.equalsIgnoreCase("espnow")','origin.toLowerCase() === "espnow"')
 .replace('imported.toUpperCase();','imported = imported.toUpperCase();').replace(/imported.length\(\)/g,'imported.length')
 .replace(/uint32_t\(([^()]*)\)/g,'(($1) >>> 0)').replace(/\b(\d+)UL\b/g,'$1');
let sensors=[],choice='JSY';
const ctx={config:{sensors:()=>sensors},sample:()=>({watts:NaN,timestampMs:0,source:'',valid:false}),NAN:NaN,isfinite:Number.isFinite,min:Math.min,uint32_t:x=>x>>>0,
 state:{simulationMode:false},first:(...xs)=>xs.find(x=>x!=null),configuredGridPowerSource:()=>choice,
 jsyStarted:true,ticStarted:true,jsy:{channelValue:(n,metric)=>metric==='W_RAW'?(n===0?-200:700):985,powerSampleMs:()=>4900},tic:{isAvailable:()=>true,getGridPowerW:()=>0,powerSampleMs:()=>4000},
 remoteSensors:[],configuredEspNowSensorFor:s=>s.cfg,remoteTimeoutForSensor:()=>10000};
ctx.clockNow=0;ctx.clockOffset=0;ctx.millis=()=>ctx.clockNow+ctx.clockOffset;
vm.createContext(ctx);vm.runInContext('function gridPowerSample(sensorId,variable,now){clockNow=now;'+body+'}',ctx);
const jsy={id:'meter',type:'JSY-MK-194T',enabled:true,channels:[{id:'clamp1',role:'grid'},{id:'clamp2',role:'load'}]};
const tic={id:'custom_tic',type:'TIC Linky',enabled:true};sensors=[jsy,tic];
assert.equal(ctx.gridPowerSample('meter','gridPowerW',5000).watts,-200);assert.equal(ctx.gridPowerSample('meter','loadPowerW',5000).valid,false);
assert.equal(ctx.gridPowerSample('custom_tic','gridPowerW',5000).timestampMs,4000,'TIC power acquisition timestamp, not unrelated labels');
choice='AUTO';assert.equal(ctx.gridPowerSample('grid','gridPowerW',5000).source,'custom_tic:GRID');
ctx.tic.isAvailable=()=>false;assert.equal(ctx.gridPowerSample('grid','gridPowerW',5000).source,'meter:CH1_W_RAW');
jsy.timeoutMs=300;ctx.jsy.powerSampleMs=()=>4600;
assert.equal(ctx.gridPowerSample('meter','gridPowerW',5000).valid,true,'Modbus timeout is not the JSY sample TTL');
assert.equal(ctx.gridPowerSample('meter','gridPowerW',7600).valid,false,'JSY expires at 3 seconds even with a valid cached value');
ctx.jsy.powerSampleMs=()=>4900;
ctx.jsy.powerSampleMs=()=>5001;ctx.clockOffset=2;
assert.equal(ctx.gridPowerSample('meter','gridPowerW',5000).valid,true,'callback timestamp newer than main-loop tick is not stale');
ctx.jsy.powerSampleMs=()=>4900;ctx.clockOffset=0;
jsy.channels[0].role='load';jsy.channels[1].role='grid';
assert.equal(ctx.gridPowerSample('meter','gridPowerW',5000).watts,700,'clamp 2 uses its own active raw watts');
jsy.channels[0].role='grid';jsy.channels[1].role='load';
jsy.channels[1].role='grid';assert.equal(ctx.gridPowerSample('meter','gridPowerW',5000).valid,false,'ambiguous network channels fail closed');jsy.channels[1].role='load';
const remote={id:'remote',type:'JSY-MK-194T',source:'espnow',channels:jsy.channels};sensors=[remote];ctx.remoteSensors=[{used:true,ok:true,timedOut:false,nodeLost:false,cfg:remote,valueCount:3,values:[{key:'CH1_W_RAW',value:2.5227},{key:'CH2_POWER',value:100},{key:'CH1_POWER',value:985.5209}],valueUpdatedMs:[1000,4900,4900]}];
assert.equal(ctx.gridPowerSample('remote','gridPowerW',5000).valid,false,'fresh fallback/load cannot revive stale raw network watts');ctx.remoteSensors[0].valueUpdatedMs[0]=4900;assert.equal(ctx.gridPowerSample('remote','gridPowerW',5000).watts,2.5227,'near-zero active watts must not become 985 W');
remote.remoteKey='CH1_POWER';assert.equal(ctx.gridPowerSample('remote','gridPowerW',5000).watts,2.5227,'historical import of same clamp permits its raw watts');
remote.remoteKey='CH2_POWER';assert.equal(ctx.gridPowerSample('remote','gridPowerW',5000).valid,false,'manual per-key imports enforced');
delete remote.remoteKey;ctx.remoteSensors[0].values[0].value=0;assert.equal(ctx.gridPowerSample('remote','gridPowerW',5000).watts,0,'real zero is valid');
for (const flag of ['timedOut','nodeLost']) {ctx.remoteSensors[0][flag]=true;assert.equal(ctx.gridPowerSample('remote','gridPowerW',5000).valid,false,'loss invalidates raw watts immediately');ctx.remoteSensors[0][flag]=false;}
ctx.remoteSensors[0].values[0].value=NaN;assert.equal(ctx.gridPowerSample('remote','gridPowerW',5000).valid,false,'invalid raw watts never fall back to estimated power');
ctx.remoteSensors[0].values[0].value=0;
ctx.remoteSensors[0].values[0].key='CH1_POWER';assert.equal(ctx.gridPowerSample('remote','gridPowerW',5000).valid,false,'old emitter without raw watts fails closed');
// Execute actual radio acquisition guard: packet cadence never creates new readings.
const radio=fs.readFileSync(path.join(__dirname,'../../src/communication/espnow_manager.cpp'),'utf8');
let guard=radio.slice(radio.indexOf('  const bool meteredPower ='),radio.indexOf('  if (sensorId == SENSOR_JSY)',radio.indexOf('  const bool meteredPower =')));
guard=guard.replace(/\bconst (?:bool|uint32_t|int8_t)\s+/g,'let ');
const r={SENSOR_JSY:1,SENSOR_LINKY:2,state:{lastJsyReadMs:4000,lastTicPowerReadMs:3000},exportLastSampleMs:[3000],ensureExportRuntimeIndex:()=>0};vm.createContext(r);vm.runInContext('function allow(sensorId){'+guard+'return true;}',r);
assert.equal(r.allow(2),undefined);r.state.lastTicPowerReadMs=4500;assert.equal(r.allow(2),true);r.state.lastTicPowerReadMs=0;assert.equal(r.allow(2),undefined);
console.log('PASS PID sampling: signed grid semantics, configured channel, TIC acquisition time, AUTO, per-value TTL, ambiguity, manual imports, radio acquisition gating');
