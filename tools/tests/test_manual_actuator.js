// Exercise actual controller blocks (mechanically translated from C++) and UI commands.
const fs=require("fs"), path=require("path"), vm=require("vm"), assert=require("assert/strict");
const root=path.join(__dirname,"../..");
const cpp=fs.readFileSync(path.join(root,"src/logic/automate_manager.cpp"),"utf8");
function translate(block) {
  return block.replace(/\bbool\s+/g,"var ")
    .replace(/for \(JsonObject actuator : config.actuators\(\)\)/g,"for (const actuator of config.actuators())")
    .replace(/actuator\["(id|enabled)"\]\s*\|\s*(""|false)/g,'readOr(actuator,"$1",$2)')
    .replace(/\.length\(\)/g,".length")
    .replace(/\b(\d+\.\d+)f\b/g,"$1");
}
const manualStart=cpp.indexOf("  if (state.manualCommandPending) {");
const manualBlock=cpp.slice(manualStart,cpp.indexOf('  if (mode == "FORCED"',manualStart));
const cancelStart=cpp.indexOf("  if (state.manualCommandPersistent &&");
const cancelBlock=cpp.slice(cancelStart,cpp.indexOf("  if (state.safetyActive)",cancelStart));
const cleanupStart=cpp.indexOf("  if (heldManualActuatorId.length()");
const cleanupBlock=cpp.slice(cleanupStart,cpp.indexOf("  // Release every output",cleanupStart));
const state={manualCommandPending:true,manualCommandPersistent:true,manualCommandSimulationMode:false,
  manualCommandActuatorId:"ssr2",manualCommand:"setActuatorPercent",manualCommandPercent:50,
  manualCommandSource:"WEB",manualCommandReason:"test",simulationMode:false,safetyActive:false,errorActive:false};
const configs=[{id:"ssr1",enabled:true},{id:"ssr2",enabled:true}];
const stops=[];
const ctx={state,config:{actuators:()=>configs},recoveryRequested:false,command:{},priority:0,nextState:0,
  heldManualActuatorId:"",ruleActuatorId:"",PRIORITY_RULE_ENGINE:2,PRIORITY_MANUAL_FORCE:4,MANUAL_FORCE:4,isfinite:Number.isFinite,
  constrain:(v,lo,hi)=>v<lo?lo:(v>hi?hi:v),readOr:(o,k,f)=>Object.hasOwn(o,k)?o[k]:f,
  actuators:{hasEspNowCommand:()=>false,command:(id,command,value)=>stops.push({id,command,value})}};
vm.createContext(ctx);
vm.runInContext("function choose(){"+translate(cancelBlock+manualBlock)+"return null;}\nfunction finish(){"+translate(cleanupBlock)+"}",ctx);
for(let tick=0;tick<120;tick++) {
  ctx.command=ctx.choose();
  assert.equal(ctx.command.commandPercent,50,"hold must survive 30 seconds without browser renewals");
  ctx.finish();
}
assert.equal(state.manualCommandPending,true);
assert.equal(ctx.heldManualActuatorId,"ssr2");
state.manualCommandPercent=0;
ctx.command=ctx.choose();ctx.finish();
assert.equal(ctx.command.commandPercent,0);
assert.equal(state.manualCommandPending,true,"OFF must stay latched against rules");
state.manualCommandPercent=75;state.manualCommandActuatorId="ssr1";
ctx.command=ctx.choose();ctx.finish();
assert.deepEqual(stops.at(-1),{id:"ssr2",command:"stop",value:0});
state.manualCommandPersistent=false;state.manualCommand="stop";state.manualCommandPercent=0;
ctx.command=ctx.choose();ctx.finish();
assert.equal(state.manualCommandPending,false,"AUTO/legacy request clears the hold");
assert.equal(ctx.heldManualActuatorId,"");
for(const reason of ["safety","error","recovery","disabled","nan"]) {
  Object.assign(state,{manualCommandPending:true,manualCommandPersistent:true,manualCommandActuatorId:"ssr2",
    manualCommandPercent:50,manualCommand:"setActuatorPercent",safetyActive:reason==="safety",
    errorActive:reason==="error",simulationMode:reason==="simulation"});
  ctx.recoveryRequested=reason==="recovery";
  configs[1].enabled=reason!=="disabled";
  if(reason==="nan") state.manualCommandPercent=NaN;
  const selected=ctx.choose();
  assert.equal(state.manualCommandPersistent,false,reason+" must cancel persistent power");
  assert.equal(state.manualCommandPending,false);
  if(selected) assert.equal(selected.commandPercent,0);
}

const app=fs.readFileSync(path.join(root,"data/www/app.js"),"utf8");
assert.doesNotMatch(app,/>Test 25%<\/button>/);
function extract(name){const s=app.search(new RegExp("(?:async )?function "+name+"\\("));assert.ok(s>=0);return app.slice(s,app.indexOf("\n}",s)+2);}
const fields={manualPowerRange:{},manualPowerNumber:{value:"70"},manualPowerReadout:{},manualActuatorId:{value:"ssr2"},manualPowerFeedback:{}};
const requests=[];let accepted=true;
const timers=new Map();let nextTimer=0;
let blockNext=false,unblock;
const ui={ $:id=>fields[id],Number,URLSearchParams,
  manualPowerTimer:null,manualPowerPending:null,manualPowerGeneration:0,manualPowerRevision:0,manualPowerSendChain:Promise.resolve(),
  setTimeout:(fn,delay)=>{assert.equal(delay,150);timers.set(++nextTimer,fn);return nextTimer;},clearTimeout:id=>timers.delete(id),
  fetch:async(url,options)=>{requests.push({url,params:new URLSearchParams(options.body)});if(blockNext){blockNext=false;await new Promise(resolve=>{unblock=resolve;});}return {ok:accepted,text:async()=>"denied"};},
  alert:()=>{},refreshActuatorRuntime:async redraw=>assert.equal(redraw,false),updateActuatorRuntimeCells:()=>{}};
vm.createContext(ui);
vm.runInContext(["setManualPowerValue","cancelPendingManualPower","enqueueManualActuatorCommand","flushManualPowerValue","sendManualActuatorCommand","forceOff","manualActuatorOff","releaseManualActuatorControl"].map(extract).join("\n"),ui);
assert.doesNotMatch(extract("showManualActuatorControl"),/applyManualActuatorControl|>Appliquer</);
(async()=>{
  ui.setManualPowerValue(150);assert.equal(fields.manualPowerNumber.value,100);
  ui.setManualPowerValue(-10);assert.equal(fields.manualPowerNumber.value,0);
  ui.setManualPowerValue(70,true);ui.setManualPowerValue(80,true);ui.setManualPowerValue(90,true);
  assert.equal(requests.length,0,"input changes should be grouped before sending");
  assert.equal(timers.size,1);
  await ui.flushManualPowerValue();
  assert.equal(requests.at(-1).params.get("value"),"90");
  assert.equal(requests.at(-1).params.get("controlMode"),"manual");
  ui.setManualPowerValue("",true);assert.equal(timers.size,0,"empty numeric input must not send a command");
  ui.setManualPowerValue(60,true);
  await ui.manualActuatorOff();
  assert.equal(requests.at(-1).params.get("value"),"0");
  assert.equal(fields.manualPowerNumber.value,0);
  const stoppedCount=requests.length;
  await ui.flushManualPowerValue();assert.equal(requests.length,stoppedCount,"OFF must cancel unsent slider values");
  ui.setManualPowerValue(40,true);
  await ui.releaseManualActuatorControl();
  assert.equal(requests.at(-1).params.get("controlMode"),"auto");
  assert.equal(fields.manualPowerRange.value,0,"AUTO resets the slider as well as the sent value");
  assert.equal(fields.manualPowerNumber.value,0);
  assert.equal(fields.manualPowerReadout.textContent,0);
  const autoCount=requests.length;
  await ui.flushManualPowerValue();assert.equal(requests.length,autoCount,"AUTO must cancel unsent slider values");
  blockNext=true;
  ui.setManualPowerValue(20,true);
  const first=ui.flushManualPowerValue();
  await Promise.resolve();await Promise.resolve();
  assert.equal(typeof unblock,"function");
  ui.setManualPowerValue(30,true);const stale=ui.flushManualPowerValue();
  const off=ui.manualActuatorOff();
  const countWhileBlocked=requests.length;
  unblock();
  await Promise.all([first,stale,off]);
  assert.equal(requests.length,countWhileBlocked+1,"queued intermediate values must be skipped before OFF");
  assert.equal(requests.at(-1).params.get("value"),"0");
  // Reproduce 47% -> OFF/AUTO with a slow HTTP response and an offline remote SSR.
  for (const action of ["manualActuatorOff","releaseManualActuatorControl"]) {
    blockNext=true;
    ui.setManualPowerValue(47,true);
    const reset=ui[action]();
    assert.equal(fields.manualPowerRange.value,0,action+" resets immediately, before HTTP completes");
    assert.equal(fields.manualPowerNumber.value,0);
    assert.equal(fields.manualPowerReadout.textContent,0);
    await Promise.resolve();await Promise.resolve();
    assert.equal(typeof unblock,"function");
    // A late OFF/AUTO response must not erase the user's next slider edit.
    ui.setManualPowerValue(35,true);
    unblock();await reset;
    assert.equal(fields.manualPowerRange.value,35,action+" response preserves a newer edit");
    assert.equal(requests.at(-1).params.get("value"),"0");
    ui.cancelPendingManualPower();
  }
  const before=requests.length;
  await ui.sendManualActuatorCommand("ssr2",NaN,"manual");assert.equal(requests.length,before);
  accepted=false;assert.equal(await ui.sendManualActuatorCommand("ssr2",50,"manual"),false);
  console.log("PASS: hold/OFF/AUTO/safety, automatic slider batching, no Apply button, serialized requests, OFF cancels queued values");
})().catch(error=>{console.error(error);process.exitCode=1;});
