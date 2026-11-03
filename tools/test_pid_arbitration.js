const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const src=fs.readFileSync(path.join(__dirname,'../src/logic/rule_engine.cpp'),'utf8');
const start=src.indexOf('RuleEngine::RuleDecision RuleEngine::evaluateDecision(');
let body=src.slice(src.indexOf('{',start)+1,src.indexOf('\n}',start));
body=body.replace(/for \(JsonObject rule : config.rules\(\)\)/g,'for (const rule of config.rules())')
 .replace(/for \(JsonObject (\w+) : ([^\n]+?)\.as<JsonArray>\(\)\)/g,'for (const $1 of ($2 || []))')
 .replace(/for \(JsonObject action : actions\)/g,'for (const action of actions)')
 .replace(/    DynamicJsonDocument oneRule\(4096\);[^]*?    if \(oneRule.overflowed\(\) \|\| !validateRulesDocument\(oneRule, errors\)\) continue;/,'')
 .replace(/RuleDecision (\w+);/g,'let $1 = {hasCommand:false,ruleName:""};')
 .replace('uint8_t order[MAX_RULES];','let order=[];').replace('bool visited[MAX_RULES] = {};','let visited=[];')
 .replace(/\b(?:const )?(?:String|bool|uint8_t|JsonObject|JsonArray)\s+/g,'let ')
 .replace(/(rule\["name"\]) \| (rule\["id"\]) \| "regle"/g,'first($1,$2,"regle")')
 .replace(/((?:rule|action|a|b)\["[^"]+"\]) \| ("[^"]*"|0)/g,'first($1,$2)')
 .replace(/(rule\[[^\]]+\])\.as<JsonArray>\(\)/g,'($1 || [])')
 .replace(/actions.isNull\(\)/g,'!actions.length').replace(/decision.ruleName.length\(\)/g,'decision.ruleName.length');
let rules=[];const ctx={MAX_RULES:24,state:{safetyTripped:false},pid:{reset(){}},config:{rules:()=>rules},selected:[],selectedCount:0,eventBranch:[],eventPending:[],
 ruleAt:n=>rules[n],first:(...xs)=>xs.find(x=>x!=null),shouldLogBranch:()=>true,evaluateCondition:c=>c.ok,
 actionToDecision:(a,name,d)=>{if(!a.actuatorId)return false;Object.assign(d,{hasCommand:true,actuatorId:a.actuatorId,command:a.command});return true;}};
vm.createContext(ctx);vm.runInContext('function evaluate(now){'+body+'}',ctx);
function rule(id,priority,actions,ok=true,elseActions=[]) {return {id,name:id,enabled:true,priority,logic:'AND',conditions:[{ok}],actions,elseActions};}
rules=[rule('low',1,[{actuatorId:'ssr1',command:'setPowerFromSurplus'},{actuatorId:'ssr2',command:'setPowerFromSurplus'}]),rule('stop',100,[{actuatorId:'ssr1',command:'stop'}])];
ctx.evaluate(1000);assert.equal(ctx.selectedCount,2);assert.equal(ctx.selected[0].command,'stop');assert.equal(ctx.selected[1].actuatorId,'ssr2');assert.equal(ctx.selected[1].ruleId,'low');
rules.reverse();ctx.evaluate(1250);assert.equal(ctx.selected[0].ruleId,'stop');assert.equal(ctx.selectedCount,2,'reordering does not change priority');
rules[0].enabled=false;ctx.evaluate(1500);assert.equal(ctx.selected[0].ruleId,'low');assert.equal(ctx.selectedCount,2);
rules=[rule('fallback',1,[{actuatorId:'ssr1',command:'on'}],false,[{actuatorId:'ssr2',command:'stop'}])];ctx.evaluate(1750);assert.equal(ctx.selected[0].actuatorId,'ssr2');assert.equal(ctx.selected[0].elseBranch,true);
ctx.state.safetyTripped=true;ctx.evaluate(2000);assert.equal(ctx.selectedCount,0);
// Exercise actual output-release/application loop, not a copy of its algorithm.
const automate=fs.readFileSync(path.join(__dirname,'../src/logic/automate_manager.cpp'),'utf8');
let cleanup=automate.slice(automate.indexOf('  // Release every output'),automate.indexOf('\n}',automate.indexOf('  // Release every output')));
cleanup=cleanup.replace(/\b(?:const )?(?:uint8_t|bool|FinalCommand)\s+/g,'let ').replace(/auto &selected/g,'let selected');
const stops=[],applied=[],computed=[];let decisions=[{actuatorId:'a'},{actuatorId:'b'}];
Object.assign(ctx,{ruleActuatorIds:['old'],ruleActuatorCount:1,priority:2,PRIORITY_RULE_ENGINE:2,now:1000,command:{},
 actuators:{acceptedPercent:()=>10,hasEspNowCommand:()=>false,command:(...xs)=>stops.push(xs)},rules:{decisionCount:()=>decisions.length,selectedDecision:n=>decisions[n],resolvePID:d=>computed.push(d.actuatorId),pidApplied(){},endPIDCycle(){}},
 ruleCommand:d=>({actuatorId:d.actuatorId,commandPercent:10}),applyFinalCommand:c=>applied.push(c)});
vm.runInContext('function apply(){'+cleanup+'}',ctx);ctx.apply();assert.equal(stops[0][0],'old');assert.deepEqual(computed,['a','b']);assert.equal(ctx.ruleActuatorCount,2);
decisions=[{actuatorId:'b'}];ctx.apply();assert.equal(stops.at(-1)[0],'a');ctx.priority=0;ctx.apply();assert.equal(stops.at(-1)[0],'b');assert.equal(ctx.ruleActuatorCount,0);
console.log('PASS PID arbitration: priorities per output, simultaneous SSRs, list reorder, SINON, safety, release/delete, effective PID computation only');

// Exercise the production instance lookup and lifecycle with stable output identities.
const controller=fs.readFileSync(path.join(__dirname,'../src/logic/pid_controller.cpp'),'utf8');
function controllerBody(name) {const begin=controller.indexOf('PIDController::'+name+'(');return controller.slice(controller.indexOf('{',begin)+1,controller.indexOf('\n}',begin));}
function controllerJS(text) {return text.replace(/for \(auto &i : instances\)/g,'for (let i of instances)').replace(/i = Instance\(\)/g,'Object.assign(i, newInstance())').replace(/return &i/g,'return i').replace(/nullptr/g,'null').replace(/\.length\(\)/g,'.length').replace(/\bconst float\s+/g,'let ').replace(/\b(\d+(?:\.\d+)?)f\b/g,'$1');}
const newInstance=()=>({id:'',used:false,pid:{integral:0,appliedPercent:0,outputPercent:0}});
const map={NAN:NaN,instances:Array.from({length:16},newInstance),state:{},newInstance,isfinite:Number.isFinite,fabsf:Math.abs,pidClamp:(v,lo,hi)=>Math.max(lo,Math.min(v,hi)),publish(){}};vm.createContext(map);
for(const [name,args] of [['instanceFor','id'],['beginCycle',''],['endCycle',''],['applied','id,percent']])vm.runInContext('function '+name+'('+args+'){'+controllerJS(controllerBody(name))+'}',map);
const one=map.instanceFor('ssr-one'),two=map.instanceFor('ssr-two');one.pid.integral=12;two.pid.integral=34;
assert.equal(map.instanceFor('ssr-two'),two);assert.equal(map.instanceFor('ssr-one').pid.integral,12);
map.beginCycle();one.used=true;one.pid.outputPercent=60;map.applied('ssr-one',0);assert.equal(one.pid.outputPercent,0);assert.equal(one.pid.appliedPercent,0);map.endCycle();
assert.equal(map.instanceFor('ssr-two').pid.integral,0,'inactive output never transfers its old integral');
for(let n=0;n<14;n++)map.instanceFor('extra-'+n);assert.equal(map.instanceFor('overflow'),null,'full table never evicts a running output');
// OFF is evaluated before persistent manual requests and cancels them.
const offStart=automate.indexOf('  if (mode == "OFF") {');
assert.ok(offStart < automate.indexOf('  if (state.manualCommandPending)'));
const off=automate.slice(offStart,automate.indexOf('\n  }',offStart)+4);
const offCtx={mode:'OFF',priority:9,nextState:9,command:{},PRIORITY_IDLE:0,IDLE:0,state:{manualCommandPending:true,manualCommandPersistent:true}};vm.createContext(offCtx);vm.runInContext('function off(){'+off+'}',offCtx);
assert.equal(offCtx.off().command,'allOff');assert.equal(offCtx.state.manualCommandPersistent,false);
console.log('PASS PID lifecycle: stable SSR ids, accepted-command tracking, unused-state release, capacity guard and global OFF priority');
