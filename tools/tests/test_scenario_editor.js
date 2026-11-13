// Exercise the production editor's serialization, including hidden metadata and SINON.
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const ui=fs.readFileSync(path.join(__dirname,'../../data/www/app.js'),'utf8');
function fn(name){const start=ui.indexOf('function '+name+'(');assert.ok(start>=0,name);return ui.slice(start,ui.indexOf('\n}',start)+2);}
function row(kind,values,original={}){
  return {getAttribute:key=>key==='data-original'?JSON.stringify(original):null,
    querySelectorAll:()=>Object.entries(values).map(([key,value])=>({value,getAttribute:()=>key}))};
}
const elements={ruleIndex:{value:'0'},ruleId:{value:'existing'},ruleName:{value:'Scénario modifié'},
  ruleEnabled:{checked:true},rulePriority:{value:'20'},ruleLogic:{value:'AND'}};
const lists={
  '#condRows .ruleRow':[row('c',{source:'solar',measure:'available',type:'boolean',value:'true',operator:'=='},{customCondition:{preserved:true}})],
  '#actionRows .ruleActionRow':[row('a',{actuatorId:'ssr2',command:'setPowerFromSurplus',maxOutputPowerW:'2000',gridSetpointW:'0',maxPercent:'80',regulation:'PID',value:'0',valueMode:'fixed',kp:'0',ki:'0.003',kd:'0',pidSchema:'1',pidSensorId:'custom_meter',pidMeasurement:'gridPowerW',pidEnabled:'false',requireProduction:'true',pidIntervalMs:'200',measurementTimeoutMs:'5000'},{customPID:{kp:.02},sourceVariable:'obsolete',sourceSensorId:'obsolete'})],
  '#elseActionRows .ruleActionRow':[row('a',{actuatorId:'ssr2',command:'stop',value:'0'},{customElse:true})]
};
const ctx={cache:{rules:{version:2,rules:[{id:'existing',customRule:{preserved:true},else:[{command:'on'}]}]}},
  $:id=>elements[id],document:{querySelectorAll:s=>lists[s]||[]},measureDef:()=>['available','boolean',''],
  alert:message=>{throw Error(message);},markDirty:()=>{},drawLogicPage:()=>{},esc:String};
vm.createContext(ctx);
for(const name of ['canonicalRuleCommand','readRuleForm','stashRuleEditor','scenarioBranchWarning','scenarioActionBranch'])vm.runInContext(fn(name),ctx);
const value=ctx.readRuleForm();
assert.equal(value.name,'Scénario modifié');assert.equal(value.priority,20);
assert.equal(value.conditions[0].value,true);
assert.equal(value.actions[0].maxOutputPowerW,2000);
assert.equal(value.actions[0].gridSetpointW,0);assert.equal(value.actions[0].maxPercent,80);
assert.equal(value.actions[0].customPID.kp,.02);
assert.equal(value.actions[0].kp,0);assert.equal(value.actions[0].ki,.003);assert.equal(value.actions[0].pidEnabled,false);assert.equal(value.actions[0].requireProduction,true);assert.equal(value.actions[0].pidSensorId,'custom_meter');assert.equal(value.actions[0].pidIntervalMs,200);
assert.ok(!('sourceVariable' in value.actions[0]));
assert.ok(!('sourceSensorId' in value.actions[0]));
assert.ok(value.conditions[0].customCondition.preserved);
assert.ok(value.customRule.preserved);
assert.equal(value.elseActions[0].command,'stop');assert.equal(value.elseActions[0].customElse,true);
assert.ok(!('else' in value),'canonical SINON must replace the old alias');
assert.ok(ctx.stashRuleEditor());assert.equal(ctx.cache.rules.rules[0].elseActions[0].command,'stop');
// Deleting the last SINON line must save an explicit empty branch, not revive it.
lists['#elseActionRows .ruleActionRow']=[];
assert.deepEqual(Array.from(ctx.readRuleForm().elseActions),[]);
// Log-only actions work without an actuator; they remain in the selected branch.
lists['#elseActionRows .ruleActionRow']=[row('a',{command:'logEvent',message:'Sans sortie',value:'0'})];
assert.equal(ctx.readRuleForm().elseActions[0].message,'Sans sortie');
lists['#elseActionRows .ruleActionRow']=[row('a',{actuatorId:'ssr2',command:'setActuatorPercent',value:'3749',valueMode:'fixed'})];
assert.throws(()=>ctx.readRuleForm(),/limitée à 100/);
assert.equal(ctx.scenarioBranchWarning([{command:'logEvent'},{command:'stop'}]),'');
assert.equal(ctx.scenarioBranchWarning([{command:'stop',actuatorId:'a'},{command:'logEvent'}]),'');
assert.match(ctx.scenarioBranchWarning([{command:'stop',actuatorId:'a'},{command:'on',actuatorId:'a'}]),/première commande/);
assert.equal(ctx.scenarioBranchWarning([{command:'stop',actuatorId:'a'},{command:'on',actuatorId:'b'}]),'');
ctx.actionFormRow=()=>'<div>action</div>';
assert.match(ctx.scenarioActionBranch('SINON','elseActionRows',[],'scenarioElse'),/elseActionRows/);
// A card click closes only the currently open scenario and stashes edits first.
vm.runInContext(fn('toggleScenario'),ctx);
let stashes=0,closes=0,opened=[];
ctx.stashRuleEditor=()=>{stashes++;return true;};
ctx.drawLogicPage=()=>{closes++;};
ctx.editRule=index=>opened.push(index);
ctx.toggleScenario(0);
assert.equal(stashes,1);assert.equal(closes,1);assert.deepEqual(opened,[]);
delete elements.ruleIndex;
ctx.toggleScenario(0);
assert.deepEqual(opened,[0],'a closed card opens on the next click');
elements.ruleIndex={value:'0'};
ctx.toggleScenario(1);
assert.deepEqual(opened,[0,1],'another card switches the editor');
ctx.stashRuleEditor=()=>false;
ctx.toggleScenario(0);
assert.equal(closes,1,'invalid edits must not be discarded by closing');
const draw=fn('drawLogicPage');
assert.doesNotMatch(draw,/>Ouvrir<\/button>/);
assert.match(draw,/onclick="toggleScenario/);
assert.match(draw,/aria-expanded="false"/);
console.log('PASS: SINON, typed values, metadata, aliases, card open/close, invalid edit protection and first-command warning');
