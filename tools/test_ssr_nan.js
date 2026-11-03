const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const root=path.join(__dirname,'..'),app=fs.readFileSync(path.join(root,'data/www/app.js'),'utf8');
const mathContext={constrain:(v,lo,hi)=>Math.max(lo,Math.min(v,hi))};
// Exercise the actual actuator input guard and the SSR OFF guard before duration conversion.
const actuators=fs.readFileSync(path.join(root,"src/actuators/actuator_manager.cpp"),"utf8");
const inputStart=actuators.indexOf("void ActuatorManager::setCommandPercent(");
const inputBody=actuators.slice(actuators.indexOf("{",inputStart)+1,actuators.indexOf("  JsonObject actuator;",inputStart))
  .replace(/\b(\d+\.\d+)f\b/g,"$1");
const actuatorContext={isfinite:Number.isFinite,constrain:mathContext.constrain,state:{logEvent:()=>{}}};
vm.createContext(actuatorContext);
vm.runInContext("function sanitize(id,percent){"+inputBody+"return percent;}",actuatorContext);
for(const value of [NaN,Infinity,-Infinity]) assert.equal(actuatorContext.sanitize("ssr2",value),0);
assert.equal(actuatorContext.sanitize("ssr2",125),100);
assert.equal(actuatorContext.sanitize("ssr2",25),25);
const ssrStart=actuators.indexOf("void ActuatorManager::updateSSR(");
const ssrBody=actuators.slice(ssrStart);
const guard=ssrBody.match(/if \(!isfinite\(percent\)[\s\S]+?\n  }/)[0];
let pinOn=true;
const pinContext={percent:NaN,mode:"BURST_FIRE",channel:{},pin:17,isfinite:Number.isFinite,setOutput:(_channel,pin,on)=>{assert.equal(pin,17);pinOn=on;}};
vm.createContext(pinContext);
vm.runInContext("(function(){"+guard+"throw new Error('Invalid value reached burst timing');})()",pinContext);
assert.equal(pinOn,false);

function jsFunction(name) {
  const start=app.search(new RegExp("(?:async )?function "+name+"\\("));
  assert.ok(start>=0);
  return app.slice(start,app.indexOf("\n}",start)+2);
}
let fetches=0;
const row={className:"",children:Array.from({length:8},()=>({innerHTML:"unchanged"})),getAttribute:()=>"ssr2"};
const runtime={id:"ssr2",commandPercent:25,outputPercent:25,outputOn:true,maxPowerW:1500,estimatedPowerW:375};
const ui={
  cache:{}, $:()=>null,
  page:"actuators",actuatorRuntimePollBusy:false,
  hasValue:v=>v!==null && v!==undefined,
  esc:String,actuatorValueText:(value,unit)=>value+" "+unit,
  actuatorRuntimeById:()=>runtime,
  actuatorRuntimeStatus:()=>({cls:"ok",label:"OK",detail:"pret"}),
  document:{querySelectorAll:selector=>{assert.equal(selector,".actuatorRow[data-actuator-id]");return [row];}},
  refreshActuatorRuntime:async redraw=>{assert.equal(redraw,false);fetches++;}
};
vm.createContext(ui);
vm.runInContext(["actuatorPercentBar","updateActuatorRuntimeCells","updateManualActuatorState","pollActuatorRuntime"].map(jsFunction).join("\n"),ui);
for(const invalid of [null,NaN,Infinity,-Infinity]) {
  const html=ui.actuatorPercentBar(invalid,invalid);
  assert.match(html,/Commande invalide/);
  assert.doesNotMatch(html,/>0 %</);
}
(async()=>{
  await ui.pollActuatorRuntime();
  assert.equal(fetches,1);
  assert.match(row.children[4].innerHTML,/25 %/);
  assert.match(row.children[4].innerHTML,/instantane : ON/);
  assert.match(row.children[5].innerHTML,/375 W/);
  assert.equal(row.children[7].innerHTML,"unchanged", "action buttons must survive polling");
  ui.cache.actuatorRuntimeError=true;
  ui.updateActuatorRuntimeCells();
  assert.equal(row.children[4].innerHTML,"-- %", "a network failure must not look like zero consumption");
  ui.cache.actuatorRuntimeError=false;
  runtime.commandPercent=60;runtime.outputPercent=60;runtime.outputOn=false;
  await ui.pollActuatorRuntime();
  assert.match(row.children[4].innerHTML,/60 %/);
  assert.match(row.children[4].innerHTML,/instantane : OFF/);
  ui.page="logic";
  await ui.pollActuatorRuntime();
  assert.equal(fetches,2,"no polling outside actuator page");
  ui.page="actuators";ui.actuatorRuntimePollBusy=true;
  await ui.pollActuatorRuntime();
  assert.equal(fetches,2,"no overlapping polls");
  console.log("PASS: invalid measure/config/result => zero, PID recovery, actuator rejects NaN/Infinity, SSR OFF, live percentages, invalid UI values, polling preserves controls");
})().catch(error=>{console.error(error);process.exitCode=1;});
