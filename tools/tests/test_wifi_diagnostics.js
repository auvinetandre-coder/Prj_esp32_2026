// Execute the production event capture and ring copy with fixed-size C++ adapters.
const fs=require('fs'),path=require('path'),assert=require('assert/strict');
const src=fs.readFileSync(path.join(__dirname,'../../src/network/wifi_diagnostics.cpp'),'utf8');
const method=name=>src.match(new RegExp('(?:void|uint8_t) SolarWiFiManager::'+name+'\\([^]*?\\) \\{([^]*?)\\n\\}'))[1];
const kinds=['STA_START','STA_STOP','STA_CONNECTED','STA_DISCONNECTED','STA_GOT_IP','STA_LOST_IP','AP_START','AP_STOP','AP_STACONNECTED','AP_STADISCONNECTED'];
const constants=Object.fromEntries(kinds.map((s,i)=>['ARDUINO_EVENT_WIFI_'+s,i+1]));
let body=method('recordDiagnosticEvent').replace(/port(?:ENTER|EXIT)_CRITICAL\(&diagnosticMux\);/g,'')
 .replace(/const uint(?:8|16|32|64)_t /g,'const ')
 .replace('WifiDiagnosticSnapshot &d = diagnosticState;','const d=diagnosticState;')
 .replace('WifiDiagnosticEvent &entry = diagnosticEvents[diagnosticHead];','const entry=diagnosticEvents[diagnosticHead];')
 .replace('static_cast<uint16_t>(event)','event')
 .replace('memset(entry.bssid, 0, sizeof(entry.bssid));','entry.bssid=[0,0,0,0,0,0];')
 .replace('memcpy(entry.bssid, info.wifi_sta_connected.bssid, sizeof(entry.bssid));','entry.bssid=[...info.wifi_sta_connected.bssid];')
 .replace('memcpy(entry.bssid, info.wifi_sta_disconnected.bssid, sizeof(entry.bssid));','entry.bssid=[...info.wifi_sta_disconnected.bssid];')
 .replace('uint32_t(now - d.downSinceMs)','((now-d.downSinceMs)>>>0)')
 .replace('entry.sequence = ++diagnosticTotal','entry.sequence = (diagnosticTotal = (diagnosticTotal + 1) >>> 0)');
const capture=new Function('ctx','event','info',`with(ctx){${body}}`);
let copy=method('copyDiagnostics').replace(/port(?:ENTER|EXIT)_CRITICAL\(&diagnosticMux\);/g,'')
 .replace('snapshot = diagnosticState; total = diagnosticTotal;','Object.assign(snapshot,diagnosticState);total.value=diagnosticTotal;')
 .replace('uint8_t count','const count').replace('uint8_t i','let i')
 .replace('events[i] = diagnosticEvents[(diagnosticHead + diagnosticCapacity - count + i) % diagnosticCapacity];','events[i] = {...diagnosticEvents[(diagnosticHead + diagnosticCapacity - count + i) % diagnosticCapacity]};');
const snapshot=new Function('ctx','snapshot','events','limit','total',`with(ctx){${copy}}`);
let now=0;const ctx={...constants,millis:()=>now,consoleEpochMs:()=>0,sampleDiagnostics(){},diagnosticBootId:1,diagnosticLastConnectedBoot:0,diagnosticHead:0,diagnosticCount:0,diagnosticTotal:0,diagnosticCapacity:32,diagnosticEvents:Array.from({length:32},()=>({})),diagnosticState:{hasIp:false,hadIp:false,hasDisconnect:false,associated:false,outageActive:false,connections:0,disconnects:0,lostIp:0,lastOutageMs:0},state:{wifiDisconnectCount:0}};
const event=(name,time,reason=0)=>{now=time;capture(ctx,constants['ARDUINO_EVENT_WIFI_'+name],{wifi_sta_disconnected:{reason,rssi:-76,bssid:[1,2,3,4,5,6]},wifi_sta_connected:{channel:6,bssid:[1,2,3,4,5,6]}});};
event('STA_CONNECTED',0);assert.equal(ctx.diagnosticState.associated,true);assert.equal(ctx.diagnosticState.hasIp,false,'association is not DHCP success');
event('STA_GOT_IP',1);assert.equal(ctx.diagnosticState.connections,1);
event('STA_DISCONNECTED',100,200);event('STA_DISCONNECTED',120,201);assert.equal(ctx.diagnosticState.downSinceMs,100,'repeated disconnect must not restart outage duration');
event('STA_GOT_IP',300);assert.equal(ctx.diagnosticState.lastOutageMs,200);assert.equal(ctx.diagnosticState.outageActive,false);
event('STA_LOST_IP',0xfffffff0);assert.equal(ctx.diagnosticState.associated,true,'losing IP is distinct from losing association');event('STA_GOT_IP',20);assert.equal(ctx.diagnosticState.lastOutageMs,36,'outage survives millis wrap');
for(let i=0;i<45;i++)event(i%2?'AP_STADISCONNECTED':'AP_STACONNECTED',1000+i);
assert.equal(ctx.diagnosticCount,32);const output=[],state={},total={};snapshot(ctx,state,output,32,total);assert.equal(output.length,32);assert.equal(output[0].sequence,total.value-31);assert.equal(output[31].sequence,total.value);
const old=JSON.stringify(output);for(let i=0;i<40;i++)event('STA_DISCONNECTED',2000+i,202);assert.equal(JSON.stringify(output),old,'snapshot stable while new events arrive');
const limited=[];assert.equal(snapshot(ctx,{},limited,4,{}),4);assert.equal(limited[3].sequence,ctx.diagnosticTotal);
assert.equal(snapshot(ctx,{},null,32,{}),0,'summary-only copy needs no event array');
assert.doesNotMatch(method('recordDiagnosticEvent'),/Serial\.|LittleFS|String\(|serializeJson|config\./,'callback remains bounded and does not write flash');
assert.doesNotMatch(src,/WiFi\.(?:begin|disconnect|reconnect|scanNetworks|mode)\(/,'diagnostics must not change radio policy');
let sample = method('sampleDiagnostics').split('portENTER_CRITICAL(&diagnosticMux);')[1].split('portEXIT_CRITICAL(&diagnosticMux);')[0]
 .replace('WifiDiagnosticSnapshot &d = diagnosticState;', 'const d=diagnosticState;')
 .replace('memcpy(d.bssid, bssid, sizeof(bssid));','d.bssid=[...bssid];')
 .replace('lastConnectedState = d;', 'lastConnectedState={...d,bssid:[...d.bssid]};');
const sampleState=new Function('ctx','reading',`with(ctx){with(reading){${sample}}}`);
ctx.lastConnectedAvailable=false;ctx.lastConnectedState={};
const reading={now:3000,status:3,mode:3,channel:6,clients:0,ap:true,rssi:-61,connected:true,ip:123,gateway:1,subnet:2,dns1:3,dns2:4,bssid:[1,2,3,4,5,6]};
sampleState(ctx,reading);assert.equal(ctx.lastConnectedAvailable,true);assert.equal(ctx.lastConnectedState.rssi,-61);
reading.connected=false;reading.rssi=0;reading.ip=0;reading.bssid=[0,0,0,0,0,0];reading.now=4000;sampleState(ctx,reading);
assert.equal(ctx.diagnosticState.ip,0);assert.equal(ctx.lastConnectedState.ip,123,'last connected photo survives a disconnection');assert.equal(ctx.lastConnectedState.sampledMs,3000);
console.log('PASS WiFi: association/IP distinction, outage, reasons, bounded chronological RAM ring, stable snapshot and rollover');
