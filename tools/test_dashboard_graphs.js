const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../data/www/app.js'), 'utf8');
function fn(name) {
  const start = source.indexOf('function ' + name + '(');
  assert.ok(start >= 0, name);
  return source.slice(start, source.indexOf('\n}', start) + 2);
}
const ctx = {console, state:{pid:{}}, cache:{}, dashHistory:[], pidGraphSelection:'', localStorage:{setItem(){}},
  isRemovedDashboardGraphId:()=>false, rgbaFromHex:(color, opacity)=>color+':'+opacity};
vm.createContext(ctx);
for (const name of ['defaultDashboardConfig','dashboardGraphDefaultsById','graphHasValue','mergeDashboardConfig','seriesSourceKey','dataSourceKey','dedupeDashboardSeries','effectiveDashboardGraph','drawSignedGridCurve','pidGraphContexts','selectedPidGraphContext','selectedPidGraphHistory']) vm.runInContext(fn(name), ctx);
const defaults = ctx.defaultDashboardConfig();
assert.equal(defaults.graphs[0].series.length, 1);
const old = {graphs:[{id:'graph_energy', title:'Mon reseau', type:'area', enabled:false, series:[
  {sourceType:'energy',sourceId:'energy',valueKey:'gridPowerW',unit:'W'},
  {sourceType:'energy',sourceId:'energy',valueKey:'gridBuyW',unit:'W'},
  {sourceType:'energy',sourceId:'energy',valueKey:'gridInjectionW',unit:'W'}]}]};
const migrated = ctx.mergeDashboardConfig(old).graphs[0];
assert.equal(migrated.series.length, 1);
assert.equal(migrated.title, 'Mon reseau'); assert.equal(migrated.enabled, false);
const strokes = []; let clip;
const canvas = {save(){},restore(){},beginPath(){},rect(x,y,w,h){clip={x,y,w,h};},clip(){},moveTo(){},lineTo(){},closePath(){},fill(){},stroke(){strokes.push({color:this.strokeStyle,clip:{...clip}});}};
ctx.drawSignedGridCurve(canvas,[{x:44,y:20},{x:144,y:120}],70,{l:44,t:10},200,130,true);
assert.deepEqual(strokes.map(s=>s.color), ['#ef4444','#22c55e']);
assert.equal(strokes[0].clip.y+strokes[0].clip.h,70);
assert.equal(strokes[1].clip.y,70,'both clips meet at exact zero crossing');
ctx.cache = {actuators:{actuators:[{id:'a',name:'Sortie A'}]}, rules:{rules:[{id:'r',name:'Scenario',enabled:true,
  actions:[{command:'logEvent'},{command:'setPowerFromSurplus',actuatorId:'a'}],
  elseActions:[{command:'setPowerFromSurplus',actuatorId:'b',regulation:'PROPORTIONAL'},{command:'setPowerFromSurplus',regulation:'PID',actuatorId:'b'}]}]}};
const contexts = ctx.pidGraphContexts();
assert.equal(contexts.length,2,'proportional is not presented as a PID');
assert.equal(contexts[0].id,'r:then:1:a'); assert.equal(contexts[1].id,'r:else:1:b');
assert.match(contexts[0].label,/Sortie A/);
ctx.pidGraphSelection='r:then:1:a';
ctx.dashHistory=[
  {t:1,pidContextId:'r:then:1:a',pidContextActive:true,pidContextErrorW:100,pidOutputPercent:20,commandPercent:20},
  {t:2,pidContextId:'r:else:1:b',pidContextActive:true,pidContextErrorW:-40,pidOutputPercent:50,commandPercent:50},
  {t:3,pidContextId:'r:then:1:a',pidContextActive:false,pidContextErrorW:10,pidOutputPercent:30,commandPercent:0},
  {t:4,pidOutputPercent:99}
];
const selected = ctx.selectedPidGraphHistory();
assert.equal(selected[0].data.output,20);
assert.equal(selected[1].data.output,null,'another scenario never contaminates the curve');
assert.equal(selected[2].data.error,null,'inactive/manual periods remain gaps');
assert.equal(selected[3].data.output,null,'legacy unlabelled history remains a gap');
ctx.pidGraphSelection='';ctx.state.pid.contextId='r:else:1:b';
assert.equal(ctx.selectedPidGraphHistory()[1].data.output,50,'automatic follows the current PID');
// Repeated draws on a Retina/mobile display must not multiply canvas height.
const highDpiCanvas = {width:0,height:220,parentNode:{clientWidth:400},getBoundingClientRect:()=>({width:400}),getAttribute(name){return name==='height' ? String(this.height) : null;},getContext:()=>({setTransform(){},clearRect(){},fillRect(){}})};
ctx.$=()=>highDpiCanvas;ctx.window={devicePixelRatio:2};ctx.drawLineChartWithValues=()=>{};ctx.isPlausibleChartValue=(_,v)=>typeof v==='number';
ctx.dashHistory=[{data:{grid:20}},{data:{grid:-20}}];
vm.runInContext(fn('drawSourceChart'),ctx);
for(let i=0;i<5;i++)ctx.drawSourceChart('chart',[{key:'grid',sourceSeries:{valueKey:'gridPowerW'}}],{});
assert.equal(highDpiCanvas.height,440,'logical chart height stays 220px across draws at DPR 2');
console.log('PASS dashboard graphs: single signed network series, migration, red/green clipping at zero, scenario/branch/actuator identities, isolation and inactive gaps.');
