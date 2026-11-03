// Run the real node-loss controller body after mechanical C++ -> JS translation.
const fs=require('fs'), path=require('path'), vm=require('vm'), assert=require('assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../src/communication/espnow_manager.cpp'),'utf8');
const start=source.indexOf('void EspNowManager::updateDiscoveredNodeLoss(');
assert.ok(start>=0);
const body=source.slice(source.indexOf('{',start)+1,source.indexOf('\n}',start))
  .replace(/\b(?:const )?(?:uint8_t|uint32_t|bool|String)\s+/g,'var ')
  .replace('EspNowDiscoveredNode &node = discoveredNodes[i];','var node = discoveredNodes[i];')
  .replace(/sensorManager->/g,'sensorManager.')
  .replace(/\b(\d+)UL\b/g,'$1')
  .replace(/now - lastSeenMs/g,'((now - lastSeenMs) >>> 0)');
let checkedAt=1002;
const node={used:true,lastSeenMs:1001,lossReported:false,mac:'LINKY'};
const actions=[], logs=[];
const ctx={discoveredNodes:[node],ESPNOW_MAX_DISCOVERED_NODES:1,
  millis:()=>checkedAt,macToString:mac=>mac,String,
  state:{addLog:line=>logs.push(line)},
  sensorManager:{setRemoteNodeLost:(mac,lost)=>actions.push({mac,lost})}};
vm.createContext(ctx);
vm.runInContext('function check(now){'+body+'}',ctx);

// RX can run after loop() sampled now but before the node-loss check.
ctx.check(1000);
assert.equal(node.lossReported,false,'a newly received frame newer than loop time must not mark Linky lost');
assert.deepEqual(actions,[]);

checkedAt=31001;
ctx.check(1000);
assert.equal(node.lossReported,false,'30 s boundary remains alive');
checkedAt=31002;
ctx.check(1000);
assert.equal(node.lossReported,true,'a real timeout still marks the node lost');
assert.deepEqual(actions.at(-1),{mac:'LINKY',lost:true});
ctx.check(1000);
assert.equal(actions.length,1,'no repeated loss notifications');
node.lastSeenMs=31003;
checkedAt=31004;
ctx.check(31002);
assert.equal(node.lossReported,false,'reception restores a node even if loop time is older');
assert.deepEqual(actions.at(-1),{mac:'LINKY',lost:false});
ctx.check(31002);
assert.equal(actions.length,2,'no repeated restoration notifications');

// The comparison also works over the 32-bit millis() wrap.
node.lastSeenMs=0xfffffe00;
checkedAt=200;
ctx.check(100);
assert.equal(node.lossReported,false);
checkedAt=40000;
ctx.check(39999);
assert.equal(node.lossReported,true,'real timeout across millis rollover is preserved');
node.lastSeenMs=40001;
checkedAt=40002;
ctx.check(40000);
assert.equal(node.lossReported,false);
node.used=false;
checkedAt=100000;
const beforeUnused=actions.length;
ctx.check(100000);
assert.equal(actions.length,beforeUnused);
console.log('PASS: fresh RX newer than loop clock, real timeout, restore, notification deduplication, millis rollover');
