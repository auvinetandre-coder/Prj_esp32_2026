// Two ESP contexts execute production control, command and observation bodies.
// Only hardware, Arduino JSON/String adapters and radio delivery are substituted.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const read=p=>fs.readFileSync(path.join(__dirname,'../..',p),'utf8');
const cpp=read('src/actuators/actuator_manager.cpp'),header=read('src/actuators/shared_actuator_control.h');
function body(source,signature){const p=source.indexOf(signature);assert.ok(p>=0,signature);return source.slice(source.indexOf('{',p)+1,source.indexOf('\n}',p));}
function member(name){const p=header.indexOf(name+'('),start=header.indexOf('{',p),end=header.indexOf('}',start);return header.slice(start+1,end);}
const helper={AUTO:'AUTO',MANUAL:'MANUAL',OFF:'OFF',strcmp:(a,b)=>a===b?0:1};vm.createContext(helper);
function adaptMember(s){return s.replace('peer[0] = 0;','peer = "";').replace('strncpy(peer, controller, sizeof(peer) - 1);','peer = controller.slice(0,17);').replace('peer[sizeof(peer) - 1] = 0;','');}
vm.runInContext('function Shared(){this.mode=AUTO;this.token=0;this.peer="";}\n'+
 ['reset','claim','accepts','name'].map((name,i)=>'Shared.prototype.'+name+'=function('+['','next,session,controller','controller,session',''][i]+'){with(this){'+adaptMember(member(name))+'}};').join('\n')+
 '\nShared.AUTO=AUTO;Shared.MANUAL=MANUAL;Shared.OFF=OFF;',helper);
let now=100,nonce=1000;
function translate(s,request=false){return s
 .replace('ActuatorManager *self = static_cast<ActuatorManager *>(context);','var self=context;')
 .replace('JsonObject actuator;',request?'var actuator=fixtures[id];':'var actuator=self.fixtures[actuatorId];')
 .replace('!findActuator(id, actuator)','!actuator').replace('!self->findActuator(actuatorId, actuator)','!actuator')
 .replace(/->/g,'.').replace(/\.as<String>\(\)/g,'').replace(/\.c_str\(\)/g,'')
 .replace(/SharedActuatorControl::/g,'SharedActuatorControl.')
 .replace(/\b(?:const )?(?:String|bool|float|uint32_t)\s+/g,'var ')
 .replace(/Actuator \*channel =/g,'var channel =').replace('const auto next','const next')
 .replace(/(actuator\["\w+"\]) \| ("[^"]*"|\d+)/g,'($1 ?? $2)')
 .replace('actuator["remoteActuatorId"] | id','(actuator.remoteActuatorId ?? id)')
 .replace('self.config.system()["router"]["mode"] | "AUTO"','(self.config.system().router.mode ?? "AUTO")')
 .replace('self.config.system()["router"]["commandTimeoutMs"] | 5000UL','(self.config.system().router.commandTimeoutMs ?? 5000)')
 .replace('routerMode.toUpperCase();','routerMode = routerMode.toUpperCase();')
 .replace(/\.length\(\)/g,'.length').replace(/(\d+(?:\.\d+)?)f\b/g,'$1').replace(/(\d+)UL\b/g,'$1')
 .replaceAll('uint32_t(millis() - channel.controlIssuedMs)','((millis() - channel.controlIssuedMs) >>> 0)');}
function endpoint(local){
 const channel={id:local?'ssr':'imported',sharedControl:new helper.Shared(),commandPercent:0,espNowLeaseMs:0,lastCommandMs:0,controlIssuedMs:0};
 const actuator={id:channel.id,source:local?'local':'espnow',enabled:true,mode:'BURST_FIRE',mac:'owner',remoteActuatorId:'ssr'};
 const events=[],sends=[];
 const state={simulationMode:false,safetyTripped:false,safetyActive:false,manualCommandPending:false,manualCommandPersistent:false,manualCommandActuatorId:'',manualCommandPercent:0,logEvent:(...a)=>events.push(a)};
 const ctx={channel,fixtures:{[channel.id]:actuator},state,safetyLocked:false,config:{system:()=>({router:{mode:'AUTO',commandTimeoutMs:5000}})},
  isfinite:Number.isFinite,millis:()=>now,esp_random:()=>++nonce,SharedActuatorControl:helper.Shared,
  channelFor:id=>id===channel.id?channel:null,channelForExisting:id=>id===channel.id?channel:null,
  normalizeMode:x=>x,setCommandPercent:(_id,value)=>{channel.commandPercent=value;channel.lastCommandMs=now;channel.espNowLeaseMs=0;},
  command:(_id,_cmd,value)=>{channel.commandPercent=Math.min(100,Math.max(0,value));channel.lastCommandMs=now;channel.espNowLeaseMs=0;return true;},
  applyLocal:()=>{},limitTimedSsrLease:()=>{},espnow:{sendActuatorCommand:(...a)=>{sends.push(a);return true;}}};
 vm.createContext(ctx);
 vm.runInContext('function request(id,mode,value){'+translate(body(cpp,'bool ActuatorManager::requestSharedControl('),true)+'}\n'+
  'function receive(context,actuatorId,cmd,value,mode,ttlMs,peer,token){'+translate(body(cpp,'bool ActuatorManager::handleEspNowActuatorCommand('))+'}\n'+
  'function observe(channel,reported){'+translate(body(cpp,'void ActuatorManager::observeSharedControl('))+'}',ctx);
 ctx.sends=sends;ctx.events=events;return ctx;
}
const owner=endpoint(true),pilot=endpoint(false);
pilot.espnow.sendActuatorCommand=(mac,id,value,mode,ttl,token,cmd)=>{
 pilot.sends.push({mac,id,value,token,cmd});owner.receive(owner,id,cmd,value,mode,ttl,'pilot',token);return true;
};
function user(esp,mode,value=0){
 assert.equal(esp.request(esp.channel.id,mode,value),true);
 Object.assign(esp.state,{manualCommandActuatorId:esp.channel.id,manualCommandPersistent:mode==='manual',manualCommandPending:mode==='manual',manualCommandPercent:value});
 if(esp===owner)esp.setCommandPercent('ssr',value);
}
function report(){return {controlMode:owner.channel.sharedControl.name(),controlToken:owner.channel.sharedControl.token};}
function refresh(token=pilot.channel.sharedControl.token,value=pilot.state.manualCommandPercent,peer='pilot'){
 return owner.receive(owner,'ssr','setPower',value,'BURST_FIRE',1000,peer,token);
}
// Pilot MANUAL -> owner, then owner's AUTO -> pilot, exactly the reported bug.
user(pilot,'manual',49);const old49=pilot.channel.sharedControl.token;
assert.equal(owner.channel.commandPercent,49);assert.equal(owner.channel.sharedControl.mode,'MANUAL');assert.equal(refresh(),true);
user(owner,'auto');assert.equal(owner.channel.commandPercent,0);assert.equal(report().controlMode,'AUTO');
for(let i=0;i<20;i++)assert.equal(refresh(old49,49),false,'repeated old 49% packets must not resurrect MANUAL after local AUTO');
now+=2100;pilot.observe(pilot.channel,report());assert.equal(pilot.state.manualCommandPersistent,false);assert.equal(pilot.state.manualCommandPending,false);assert.equal(pilot.channel.sharedControl.mode,'AUTO');
// Owner MANUAL -> pilot state, then pilot AUTO -> owner's manual latch cancelled.
user(owner,'manual',40);assert.equal(report().controlMode,'MANUAL');
user(pilot,'auto');assert.equal(owner.state.manualCommandPersistent,false);assert.equal(owner.state.manualCommandPending,false);assert.equal(owner.channel.commandPercent,0);assert.equal(report().controlMode,'AUTO');
// OFF in either direction cancels the old controller; only a NEW user intent may claim again.
user(pilot,'manual',65);const old65=pilot.channel.sharedControl.token;
user(owner,'manual',0);assert.equal(report().controlMode,'OFF');assert.equal(owner.channel.commandPercent,0);assert.equal(refresh(old65,65),false);
now+=2100;pilot.observe(pilot.channel,report());assert.equal(pilot.state.manualCommandPersistent,false);
user(owner,'manual',30);user(pilot,'manual',0);assert.equal(owner.state.manualCommandPersistent,false);assert.equal(report().controlMode,'OFF');assert.equal(owner.channel.commandPercent,0);
assert.equal(refresh(),true,'the current OFF session may renew at zero');assert.equal(refresh(old65,65),false);
user(pilot,'manual',70);assert.equal(owner.channel.commandPercent,70,'a new explicit manual intent is distinct from a stale renewal');
const current=pilot.channel.sharedControl.token;assert.equal(refresh(current,90,'other'),false,'a token alone cannot authorise a different peer');
user(owner,'auto');now+=2100;pilot.state.manualCommandActuatorId='other-output';pilot.state.manualCommandPersistent=true;pilot.observe(pilot.channel,report());assert.equal(pilot.state.manualCommandPersistent,true,'mode changes only affect the corresponding actuator');
// Safety remain authoritative and do not activate the real SSR.
owner.channel.commandPercent=0;owner.state.safetyTripped=true;
assert.equal(owner.receive(owner,'ssr','controlManual',100,'BURST_FIRE',1000,'pilot',9999),false);assert.equal(owner.channel.commandPercent,0);
owner.state.safetyTripped=false;
assert.equal(owner.receive(owner,'absent','controlAuto',0,'BURST_FIRE',1000,'pilot',900),false,'unknown actuator cannot be controlled');
owner.state.simulationMode=false;pilot.state.simulationMode=false;
user(pilot,'manual',55);const lostToken=pilot.channel.sharedControl.token;
const expiryStart=cpp.indexOf('    if (channel->espNowLeaseMs &&');
const expiryEnd=cpp.indexOf('    if (channel->commandPercent > 0',expiryStart);
assert.ok(expiryStart>0&&expiryEnd>expiryStart);
const expiry=translate(cpp.slice(expiryStart,expiryEnd)).replaceAll('uint32_t(timeoutNow - channel.lastCommandMs)','((timeoutNow - channel.lastCommandMs) >>> 0)');
owner.publishPower=()=>{};owner.channel.lastCommandMs=0xfffffff0;now=20;owner.timeoutNow=now;
vm.runInContext(expiry,owner);assert.equal(owner.channel.commandPercent,55,'clock wrap alone does not expire a fresh session');
now=984;owner.timeoutNow=now;vm.runInContext(expiry,owner);
assert.equal(owner.channel.commandPercent,0);assert.equal(owner.channel.sharedControl.mode,'AUTO');
assert.equal(refresh(lostToken,55),false,'after timeout/reconnect old manual packets still cannot resume power');
const deliver=pilot.espnow.sendActuatorCommand;
pilot.espnow.sendActuatorCommand=()=>true; // Radio queue accepted, intent lost in transit.
user(pilot,'manual',80);const lostIntent=pilot.channel.sharedControl.token;
assert.equal(refresh(lostIntent,80),false,'a renewal cannot stand in for a lost explicit intent');
now+=2100;pilot.observe(pilot.channel,report());assert.equal(pilot.state.manualCommandPersistent,false);assert.equal(owner.channel.commandPercent,0);
pilot.espnow.sendActuatorCommand=deliver;
assert.match(read('src/web/web_ui.cpp'),/actuators.requestSharedControl\(id, controlMode, value\)/);
assert.match(read('src/communication/espnow_manager.cpp'),/msg\.mode, msg\.ttlMs, peer, msg\.reservedCounter/);
assert.match(cpp,/espnow.sendActuatorCommand/);
console.log('PASS: real two-ESP control bodies: MANUAL/OFF/AUTO both ways, old renewals rejected, other targets preserved, peer-bound sessions, Safety and simulation');
