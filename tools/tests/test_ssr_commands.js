const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const read=p=>fs.readFileSync(path.join(__dirname,'../..',p),'utf8');
const actuator=read('src/actuators/actuator_manager.cpp'),rule=read('src/logic/rule_engine.cpp'),automate=read('src/logic/automate_manager.cpp'),ui=read('data/www/app.js');
function body(source,signature){const start=source.indexOf(signature);assert.ok(start>=0,signature);return source.slice(source.indexOf('{',start)+1,source.indexOf('\n}',start));}
const ctx={fixtures:{ssr:{maxPowerW:2000}},isfinite:Number.isFinite,constrain:(v,lo,hi)=>Math.min(hi,Math.max(lo,v)),readOr:(o,k,d)=>o[k]==null?d:o[k]};
let convert=body(actuator,'float ActuatorManager::powerWattsToPercent(')
 .replace('JsonObject actuator;','var actuator = fixtures[actuatorId];')
 .replace('!findActuator(actuatorId, actuator)','!actuator')
 .replace(/\bfloat\s+/g,'var ').replace(/(\d+(?:\.\d+)?)f\b/g,'$1')
 .replace('actuator["maxPowerW"] | 1000.0','readOr(actuator,"maxPowerW",1000)');
vm.createContext(ctx);vm.runInContext('function convert(actuatorId,watts,maxPowerOverrideW=0){'+convert+'}',ctx);
for(const [watts,percent] of [[0,0],[600,30],[1200,60],[2000,100],[3000,100],[-10,0],[NaN,0],[Infinity,0]])assert.equal(ctx.convert('ssr',watts),percent);
assert.equal(ctx.convert('ssr',600,3000),20);
assert.equal(ctx.convert('missing',600),0);
assert.equal(ctx.convert('ssr',600,NaN),0);
ctx.fixtures.ssr.maxPowerW=NaN;assert.equal(ctx.convert('ssr',600),0);ctx.fixtures.ssr.maxPowerW=2000;
// Run the actual watts branch and percentage decision tail, then AutomateManager.
let routing=rule.match(/else if \(command == "setPowerWatts"[^]*?\n  \}/)[0]
 .replace(/^else /,'').replace('action["maxOutputPowerW"] | 0.0f','readOr(action,"maxOutputPowerW",0)');
let tail=body(rule,'bool RuleEngine::actionToDecision(').split('  decision.hasCommand = actuatorId.length() && command.length();')[1];
tail='decision.hasCommand = actuatorId.length() && command.length();'+tail;
tail=tail.replaceAll('.length()', '.length').replace(/(\d+(?:\.\d+)?)f\b/g,'$1');
ctx.actuators={powerWattsToPercent:(...args)=>ctx.convert(...args)};ctx.logCommandChange=()=>{};
const fixed=rule.match(/  if \(command == "on"\) value = 100\.0f;\n  else if[^\n]+value = 0\.0f;/)[0].replace(/(\d+(?:\.\d+)?)f\b/g,'$1');
vm.runInContext('function decisionFor(action){var command=action.command,actuatorId=action.actuatorId,value=action.value,decision={},ruleName="test";'+routing+fixed+tail.replace('return decision.hasCommand;','return decision;')+'}',ctx);
let finalBody=body(automate,'AutomateManager::FinalCommand AutomateManager::ruleCommand(')
 .replace('FinalCommand command;','var command={};').replaceAll('.length()', '.length').replace(/(\d+(?:\.\d+)?)f\b/g,'$1');
vm.runInContext('function finalFor(decision){'+finalBody+'}',ctx);
for(const command of ['setPowerWatts','setPowerFromWatts','setPowerFromSensorWatts']){
 const decision=ctx.decisionFor({command,actuatorId:'ssr',value:1200,maxOutputPowerW:2000});
 const final=ctx.finalFor(decision);
 assert.equal(final.command,'setPower');assert.equal(final.commandPercent,60,'1200 W must remain 60%, not become 100 W then 5%');
}
assert.equal(ctx.finalFor(ctx.decisionFor({command:'on',actuatorId:'ssr',value:0})).commandPercent,100);
for(const command of ['stop','off','safetyShutdown'])assert.equal(ctx.finalFor(ctx.decisionFor({command,actuatorId:'ssr',value:80})).commandPercent,0);
// UI menus depend on target; aliases and unsupported legacy selections survive opening.
const escape=s=>String(s).replaceAll('&','&amp;').replaceAll('"','&quot;').replaceAll('<','&lt;');
const browser={cache:{actuators:{actuators:[{id:'ssr',type:'SSR',maxPowerW:2000},{id:'relay',type:'Relay'}]}},esc:escape,
 commandLabels:{stop:'Arrêter',on:'Pleine puissance',setActuatorPercent:'Puissance fixe',setPowerWatts:'Watts',setPowerFromSurplus:'Surplus',setPowerFromLinky:'Linky',logEvent:'Log',setSafetyWarning:'Alerte',setMode:'Mode'}};
vm.createContext(browser);
for(const name of ['canonicalRuleCommand','isSystemRuleCommand','ruleActuator','commandsForActuator','commandOptions','actionNominalPowerControl']){
 const start=ui.indexOf('function '+name+'(');vm.runInContext(ui.slice(start,ui.indexOf('\n}',start)+2),browser);
}
assert.deepEqual(Array.from(browser.commandsForActuator('ssr')),['stop','on','setActuatorPercent','setPowerWatts','setPowerFromSurplus']);
assert.deepEqual(Array.from(browser.commandsForActuator('')),['logEvent','setSafetyWarning']);
assert.deepEqual(Array.from(browser.commandsForActuator('relay')),['stop','on']);
for(const legacy of ['off','safetyShutdown','setPower']){
 const html=browser.commandOptions(legacy,'ssr');assert.match(html,new RegExp('value="'+legacy+'" selected'));
 assert.equal((html.match(/<option/g)||[]).length,5);
}
assert.match(browser.commandOptions('setMode','ssr'),/Ancienne commande conservée/);
assert.doesNotMatch(browser.commandOptions('stop','ssr'),/value="(?:toggle|setMode|safetyShutdown|logEvent|setSafetyWarning)"/);
assert.match(browser.actionNominalPowerControl({actuatorId:'ssr'}),/value="2000"/);
assert.match(browser.actionNominalPowerControl({actuatorId:'ssr',maxOutputPowerW:3000}),/value="3000"/);
console.log('PASS: six SSR commands, separate system actions, legacy aliases, configured watts, safety bounds and complete watts-to-final-percent path');
