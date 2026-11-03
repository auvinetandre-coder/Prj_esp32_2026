const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../src/logic/rule_engine.cpp'),'utf8');
function body(signature){const start=source.indexOf(signature);assert.ok(start>=0);return source.slice(source.indexOf('{',start)+1,source.indexOf('\n}',start));}
const ctx={MAX_RULES:24,eventBranch:Array(24).fill(0),eventPending:Array(24).fill(false),eventLogged:Array(24).fill(false),lastEventMs:Array(24).fill(0)};
vm.createContext(ctx);
vm.runInContext('function gate(index,branch,now){'+body('bool RuleEngine::shouldLogBranch(')
 .replace('static_cast<uint32_t>(now - lastEventMs[index])','((now - lastEventMs[index]) >>> 0)').replace('5000UL','5000')+'}',ctx);
assert.equal(ctx.gate(0,1,0),true,'first entry at boot is logged');
for(let now=250;now<100000;now+=250)assert.equal(ctx.gate(0,1,now),false,'stable branch never floods logs');
assert.equal(ctx.gate(0,2,100000),true,'branch transition logs again');
assert.equal(ctx.gate(0,1,100250),false,'rapid reversal waits');
assert.equal(ctx.gate(0,2,100500),false,'latest branch replaces pending branch');
assert.equal(ctx.gate(0,2,104999),false);
assert.equal(ctx.gate(0,2,105000),true,'pending latest branch logs once when allowed');
assert.equal(ctx.gate(0,2,110000),false,'no periodic repeat on unchanged state');
assert.equal(ctx.gate(1,1,1),true,'independent rules, even with identical names');
ctx.eventBranch[0]=0;ctx.eventPending[0]=false;
assert.equal(ctx.gate(0,2,110000),true,'resume after disabled/preempted branch');
assert.equal(ctx.gate(24,1,1),false,'bounded rule storage');
ctx.lastEventMs[2]=0xfffffff0;ctx.eventLogged[2]=true;ctx.eventBranch[2]=1;
assert.equal(ctx.gate(2,2,100),false);assert.equal(ctx.gate(2,2,4984),true,'millis rollover');
// Exercise the actual system-action branches: only their logging is gated.
let actionBody=body('bool RuleEngine::actionToDecision(').split('  if (command == "setMode")')[0]
 .replace(/String (\w+) = action\["(\w+)"\] \| "";/g,'let $1 = action.$2 || "";')
 .replace('action["message"] | "evenement"','action.message == null ? "evenement" : action.message')
 .replace('action["message"] | "Alerte regle"','action.message == null ? "Alerte regle" : action.message');
const logs=[];ctx.state={safetyTripped:false,addLog:line=>logs.push(line)};
vm.runInContext('function execute(action,ruleName,emitEvent){'+actionBody+'}',ctx);
ctx.execute({command:'logEvent',message:'Test'},'R',true);
for(let n=0;n<10000;n++)ctx.execute({command:'logEvent',message:'Test'},'R',false);
assert.equal(logs.length,1);
ctx.execute({command:'setSafetyWarning',message:'Warning'},'R',true);
ctx.state.safetyLevel='OK';ctx.state.safetyReason='';
ctx.execute({command:'setSafetyWarning',message:'Warning'},'R',false);
assert.equal(logs.length,2);assert.equal(ctx.state.safetyLevel,'WARNING');assert.equal(ctx.state.safetyReason,'Warning');
ctx.state.safetyTripped=true;ctx.execute({command:'logEvent'},'R',true);assert.equal(logs.length,2);
assert.match(source,/actionToDecision\(action, candidate.ruleName, candidate, emitEvent\)/);
assert.match(source,/if \(!visited\[i\]\) \{ eventBranch\[i\] = 0; eventPending\[i\] = false; \}/);
assert.match(fs.readFileSync(path.join(__dirname,'../src/web/web_ui.cpp'),'utf8'),/if \(ok && strcmp\(name, "rules"\) == 0\) rules.loadRules\(\);/);
console.log('PASS: stable logs once, ALORS/SINON transitions, 5s chatter limit, pending latest branch, independent rules, resume, bounded storage, rollover, alert remains live and safety wins');
