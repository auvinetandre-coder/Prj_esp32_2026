// Execute the production arithmetic after mechanical C++ -> JS syntax translation.
const fs=require('fs'),path=require('path'),vm=require('vm');
const source=fs.readFileSync(path.join(__dirname,'../../src/logic/positional_pid.h'),'utf8');
function body(name) { const start=source.indexOf(name+'('), brace=source.indexOf('{',start); let depth=1,end=brace+1; for(;depth;end++){if(source[end]==='{')depth++;if(source[end]==='}')depth--;} return source.slice(brace+1,end-1); }
function state(){return {integral:0,previousError:0,previousMeasurement:0,filteredDerivative:0,outputPercent:0,appliedPercent:0,measuredW:0,p:0,d:0,lastUpdateMs:0,lastMeasurementMs:0,initialized:false,saturated:false,rampLimited:false};}
function settings(overrides={}) { return Object.assign({kp:.02,ki:.002,kd:0,setpointW:0,deadbandW:30,biasPercent:0,maxPercent:100,risePerSecond:5,fallPerSecond:100,measurementFilterMs:0,derivativeFilterMs:500,intervalMs:200,timeoutMs:5000},overrides); }
function translate(text) {return text.replace(/s = PIDState\(\)/g,'Object.assign(s, newState())').replace(/uint32_t\(([^()]*)\)/g,'(($1) >>> 0)').replace(/float\(([^()]*)\)/g,'Number($1)').replace(/\b(?:const )?(?:float|bool|uint32_t)\s+/g,'let ').replace(/\b(\d+(?:\.\d+)?)(?:f|UL)\b/g,'$1');}
const ctx={newState:state,isfinite:Number.isFinite,fminf:Math.min,fmaxf:Math.max,fabsf:Math.abs,expf:Math.exp,pidClamp:(x,lo,hi)=>Math.min(hi,Math.max(lo,x))};
vm.createContext(ctx);
vm.runInContext('function validPIDSettings(c){'+translate(body('validPIDSettings'))+'}\nfunction stepPID(s,c,measurement,sampleMs,now,reconfigure=false){'+translate(body('stepPID'))+'}',ctx);
module.exports={state,settings,step:ctx.stepPID,valid:ctx.validPIDSettings};
