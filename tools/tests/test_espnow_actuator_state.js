const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const read=p=>fs.readFileSync(path.join(__dirname,'../..',p),'utf8');
const radio=read('src/communication/espnow_manager.cpp'),actuator=read('src/actuators/actuator_manager.cpp'),app=read('data/www/app.js');
const dispatch=radio.slice(radio.indexOf('void EspNowManager::handleReceive(')).match(/if \((len >= 2[^\n]+)\) \{/)[1];
const generic=new Function('len','data','ESPNOW_PROTOCOL_VERSION','return '+dispatch);
assert.equal(generic(244,[4,255],4),false,'legacy STATUS=4 must not be confused with generic protocol v4');
assert.equal(generic(68,[4,1],4),true);
function body(text,signature){const start=text.indexOf(signature);assert.ok(start>=0,signature);return text.slice(text.indexOf('{',start)+1,text.indexOf('\n}',start));}
function cpp(s){return s
 .replace(/uint8_t address\[6\];/g,'var address=[];')
 .replace(/for \(const auto &item : actuatorStates\)/g,'for (const item of actuatorStates)')
 .replace(/for \(auto &item : actuatorStates\)/g,'for (const item of actuatorStates)')
 .replace(/for \(JsonObject actuator : config.actuators\(\)\)/g,'for (const actuator of config.actuators())')
 .replace('EspNowActuatorState *slot = nullptr;','var slot = null;')
 .replace(/\b(?:String|bool) (source|peer|id|mapped) =/g,'var $1 =')
 .replace('actuator["source"] | "local"','(actuator.source ?? "local")')
 .replace('actuator["mac"] | ""','(actuator.mac ?? "")')
 .replace('actuator["remoteActuatorId"] | actuator["id"] | ""','(actuator.remoteActuatorId ?? actuator.id ?? "")')
 .replace('peer.equalsIgnoreCase(mac)','peer.toLowerCase() === mac.toLowerCase()')
 .replace(/slot = &item/g,'slot = item').replace(/slot->/g,'slot.')
 .replace('memcpy(slot.mac, address, 6);','slot.mac = Array.from(address);')
 .replace(/strlcpy\(slot\.(actuatorId|source|controlMode), ([^,]+), sizeof\(slot\.\1\)\);/g,'slot.$1 = $2;')
 .replace(/sizeof\(message\.(actuatorId|command)\)/g,'24').replace(/sizeof\(message.mode\)/g,'20')
 .replaceAll('uint32_t(millis() - item.receivedMs)','((millis() - item.receivedMs) >>> 0)')
 .replace(/slot\.(outputOn|enabled|safetyLocked|legacyOutputInhibited) = message.reservedCounter & (\d+)U;/g,'slot.$1 = Boolean(message.reservedCounter & $2);')
 .replace(/(\d+)UL/g,'$1').replace('out = item;','Object.assign(out,item);');}
let now=100,known=true;
const slots=Array.from({length:16},()=>({used:false,mac:[],actuatorId:''}));
const imports=[{id:'remote',source:'espnow',mac:'AA:BB:CC:DD:EE:01',remoteActuatorId:'ssr'}];
const ctx={actuatorStates:slots,config:{actuators:()=>imports},isPeerKnown:()=>known,millis:()=>now,isfinite:Number.isFinite,
 parseMac:(mac,out)=>{const parts=mac.split(':').map(x=>parseInt(x,16));if(parts.length!==6||parts.some(Number.isNaN))return false;out.push(...parts);return true;},
 memcmp:(a,b)=>a.join(',')===b.join(',')?0:1,strcmp:(a,b)=>a===b?0:1,memchr:(s,_zero,size)=>s.length<size};
vm.createContext(ctx);
vm.runInContext('function receive(mac,message){'+cpp(body(radio,'void EspNowManager::receiveActuatorState('))+'}\nfunction get(mac,id,out){'+cpp(body(radio,'bool EspNowManager::remoteActuatorState('))+'}',ctx);
const mac='AA:BB:CC:DD:EE:01';
const message={actuatorId:'ssr',command:'MANUAL',commandPercent:60,value:60,reservedCounter:3,mode:'MANUAL',ttlMs:123};
ctx.receive(mac,message);let out={};assert.equal(ctx.get(mac,'ssr',out),true);assert.equal(out.percent,60);assert.equal(out.source,'MANUAL');assert.equal(out.controlMode,'MANUAL');assert.equal(out.controlToken,123);
// State changes on the physical owner propagate without commanding it again.
now++;ctx.receive(mac,{...message,commandPercent:25,value:25,command:'ESP-NOW'});out={};ctx.get(mac,'ssr',out);assert.equal(out.percent,25);
for(const invalid of [{value:NaN},{value:101},{commandPercent:Infinity},{actuatorId:'x'.repeat(24)},{command:'x'.repeat(24)},{mode:'x'.repeat(20)},{actuatorId:'other'}]) {
 ctx.receive(mac,{...message,...invalid});out={};ctx.get(mac,'ssr',out);assert.equal(out.percent,25,'invalid/unmapped status must not replace last valid state');
}
known=false;ctx.receive(mac,message);assert.equal(ctx.get(mac,'ssr',{}),false);known=true;
ctx.receive('AA:BB:CC:DD:EE:02',message);assert.equal(slots.filter(x=>x.used).length,1,'MAC and remote ID are both required');
assert.equal(imports.length,1,'receiving state cannot import equipment');
now=3601;assert.equal(ctx.get(mac,'ssr',{}),false,'state expires after 3.5 s even if node still announces discovery');
now=0xfffffff0;ctx.receive(mac,message);now=20;assert.equal(ctx.get(mac,'ssr',{}),true);now=3484;assert.equal(ctx.get(mac,'ssr',{}),false);
now=100;ctx.receive(mac,message);
for(let index=0;index<16;index++) {
 const id='extra'+index;imports.push({id,source:'espnow',mac,remoteActuatorId:id});
 ctx.receive(mac,{...message,actuatorId:id});
}
assert.equal(slots.filter(x=>x.used).length,16,'runtime storage remains bounded');
assert.equal(ctx.get(mac,'extra15',{}),false,'full fresh table cannot silently replace another actuator');
now+=3500;ctx.receive(mac,{...message,actuatorId:'extra15'});assert.equal(ctx.get(mac,'extra15',{}),true,'expired storage is reusable');
// Check API projection uses reported state rather than optimistic requested power.
const start=actuator.indexOf('    item["requestedPercent"] = commandPercent;');
const end=actuator.indexOf('    JsonObject values',start);
assert.ok(start>0&&end>start);
const projection=actuator.slice(start,end).replace('EspNowActuatorState reported;','var reported={};')
 .replace(/channel->/g,'channel.')
 .replace('const bool fresh','const fresh').replace('actuator["remoteActuatorId"] | id.c_str()','(actuator.remoteActuatorId ?? id)')
 .replaceAll('uint32_t(millis() - reported.receivedMs)','((millis() - reported.receivedMs) >>> 0)')
 .replace('item["ok"].as<bool>()','Boolean(item.ok)').replace(/nullptr/g,'null').replace(/(\d+\.\d+)f/g,'$1');
Object.assign(ctx,{item:{},channel:null,remote:true,remoteMac:mac,actuator:{remoteActuatorId:'ssr'},id:'remote',commandPercent:75,enabled:true,
 channelSafetyLocked:false,remoteNodeLost:false,remoteIncomplete:false,maxPowerW:2000,espnow:{remoteActuatorState:ctx.get}});
now=10;ctx.receive(mac,{...message,commandPercent:40,value:40,reservedCounter:3});
vm.runInContext(projection,ctx);assert.equal(ctx.item.requestedPercent,75);assert.equal(ctx.item.outputPercent,40);assert.equal(ctx.item.source,'MANUAL');assert.equal(ctx.item.estimatedPowerW,800);
ctx.receive(mac,{...message,commandPercent:0,value:0,reservedCounter:6,command:'SECURITY'});vm.runInContext(projection,ctx);assert.equal(ctx.item.outputPercent,0);assert.equal(ctx.item.ok,false);assert.equal(ctx.item.safetyLocked,true);
now+=3500;vm.runInContext(projection,ctx);assert.equal(ctx.item.outputPercent,null);assert.equal(ctx.item.commandPercent,null);assert.equal(ctx.item.ok,false);
// Diagnostic/dashboard must never prefer an old actuator-page cache.
function js(name){const s=app.indexOf('function '+name+'(');assert.ok(s>=0,name);return app.slice(s,app.indexOf('\n}',s)+2);}
const fields={manualActuatorId:{value:'remote'},manualAppliedState:{},manualPowerFeedback:{},manualPowerRange:{value:75},manualPowerNumber:{value:75},manualPowerReadout:{}};
const ui={page:'diagnostic',manualPowerPending:null,document:{activeElement:null},cache:{actuatorRuntime:{actuators:[{id:'remote',outputPercent:10}]}},state:{actuators:[{id:'remote',commandPercent:40,outputPercent:40,source:'MANUAL',requestedPercent:75}]},
 $:id=>fields[id],actuatorRuntimeById:()=>ui.state.actuators[0],actuatorValueText:(v,u)=>v+' '+u,hasValue:v=>v!=null};
vm.createContext(ui);vm.runInContext(js('actuatorRuntimeList')+'\n'+js('setManualPowerValue')+'\n'+js('updateManualActuatorState'),ui);
assert.equal(ui.actuatorRuntimeList()[0].outputPercent,40);ui.page='dashboard';assert.equal(ui.actuatorRuntimeList()[0].outputPercent,40);
ui.page='actuators';assert.equal(ui.actuatorRuntimeList()[0].outputPercent,10);
ui.updateManualActuatorState();assert.match(fields.manualAppliedState.textContent,/Commande appliquee : 40 %.*Sortie annoncee : 40 %.*MANUAL/);
assert.equal(fields.manualPowerRange.value,40,'an idle panel must mirror the physical owner');
assert.equal(fields.manualPowerNumber.value,40);assert.equal(fields.manualPowerReadout.textContent,40);
assert.doesNotMatch(fields.manualAppliedState.textContent,/Derniere consigne locale/);
for(const guard of ['typing','dragging','queued','http','grace','error']) {
 fields.manualPowerRange.value=75;
 ui.document.activeElement=guard==='typing'?fields.manualPowerNumber:null;
 fields.manualPowerRange.matches=()=>guard==='dragging';
 ui.manualPowerPending=guard==='queued'?{}:null;
 fields.manualActuatorId.manualRequestsPending=guard==='http'?1:0;
 fields.manualActuatorId.manualSyncAfterMs=guard==='grace'?Date.now()+5000:0;
 ui.cache.actuatorRuntimeError=guard==='error';
 ui.updateManualActuatorState();assert.equal(fields.manualPowerRange.value,75,guard+' must preserve the active edit/request');
}
ui.cache.actuatorRuntimeError=false;ui.document.activeElement=null;ui.manualPowerPending=null;
fields.manualActuatorId.manualRequestsPending=0;fields.manualActuatorId.manualSyncAfterMs=0;fields.manualPowerRange.matches=()=>false;
// Both panels follow changes initiated on either ESP, including OFF and return AUTO.
for(const [percent,source] of [[83,'ESP-NOW'],[0,'MANUAL'],[40,'MANUAL'],[25,'AUTOMATE']]) {
 const controlMode=percent===0?'OFF':source==='AUTOMATE'?'AUTO':'MANUAL';
 Object.assign(ui.state.actuators[0],{commandPercent:percent,outputPercent:percent,source,controlMode});
 ui.updateManualActuatorState();assert.equal(fields.manualPowerRange.value,percent);assert.equal(fields.manualPowerNumber.value,percent);assert.equal(fields.manualPowerReadout.textContent,percent);
 assert.ok(fields.manualAppliedState.textContent.includes('Mode : '+controlMode));
 assert.ok(fields.manualPowerFeedback.textContent.includes('Mode partage : '+controlMode));
}
Object.assign(ui.state.actuators[0],{commandPercent:null,outputPercent:null,source:'UNKNOWN'});
ui.updateManualActuatorState();assert.equal(fields.manualPowerRange.value,25,'an unknown state cannot masquerade as zero');
console.log('PASS: owner state both directions, MAC/ID mapping, invalid frames, expiry/wrap, Safety, fresh diagnostics, live sliders/OFF/AUTO, editing/in-flight guards and no command echo');
