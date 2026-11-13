const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert/strict');
const source = fs.readFileSync(path.join(__dirname, '../../data/www/app.js'), 'utf8');
function fn(name) {
  const start = source.indexOf('function ' + name + '(');
  assert.ok(start >= 0, name);
  return source.slice(start, source.indexOf('\n}', start) + 2);
}
const ctx = {console, state:{pid:{}}, cache:{}, dashHistory:[], localStorage:{setItem(){}},
  dashboardPlotHistory:()=>ctx.dashHistory, isRemovedDashboardGraphId:()=>false, rgbaFromHex:(color, opacity)=>color+':'+opacity};
vm.createContext(ctx);
for (const name of ['defaultDashboardConfig','dashboardGraphDefaultsById','graphHasValue','mergeDashboardConfig','seriesSourceKey','dataSourceKey','dedupeDashboardSeries','effectiveDashboardGraph','drawSignedGridCurve']) vm.runInContext(fn(name), ctx);
for (const name of ['hasValue','finiteOrNull','boundedFiniteOrNull','powerOrNull','networkGraphPower']) vm.runInContext(fn(name), ctx);
assert.equal(ctx.networkGraphPower({gridGraphPowerW: 3.958}, 1056.884), 3.958);
assert.equal(ctx.networkGraphPower({gridGraphPowerW: -20}, 1056.884), -20);
assert.equal(ctx.networkGraphPower({gridGraphPowerW: 0}, 1056.884), 0);
assert.equal(ctx.networkGraphPower({gridGraphPowerW: null}, 1056.884), null);
assert.equal(ctx.networkGraphPower({}, 1056.884), 1056.884);
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
// Repeated draws on a Retina/mobile display must not multiply canvas height.
const highDpiCanvas = {width:0,height:220,parentNode:{clientWidth:400},getBoundingClientRect:()=>({width:400}),getAttribute(name){return name==='height' ? String(this.height) : null;},getContext:()=>({setTransform(){},clearRect(){},fillRect(){}})};
ctx.$=()=>highDpiCanvas;ctx.window={devicePixelRatio:2};ctx.drawLineChartWithValues=()=>{};ctx.isPlausibleChartValue=(_,v)=>typeof v==='number';
ctx.dashHistory=[{data:{grid:20}},{data:{grid:-20}}];
vm.runInContext(fn('drawSourceChart'),ctx);
for(let i=0;i<5;i++)ctx.drawSourceChart('chart',[{key:'grid',sourceSeries:{valueKey:'gridPowerW'}}],{});
assert.equal(highDpiCanvas.height,440,'logical chart height stays 220px across draws at DPR 2');
const now = 1800000000000;
ctx.Date = {now:()=>now};
ctx.dashboardArchiveMs = 86400000;
ctx.dashboardArchiveStepMs = 60000;
ctx.historySampleIntervalMs = 5000;
ctx.dashboardShowDay = true;
ctx.dashboardPrecisionEnabled = false;
ctx.dashboardArchive = [
  {t:now - 86400001},
  {t:now - 86400000},
  {t:now - 120000},
  {t:now - 60000},
  {t:now + 1}
];
vm.runInContext(fn('dashboardPlotHistory'), ctx);
const day = ctx.dashboardPlotHistory();
assert.deepEqual(Array.from(day, point=>point.t), [now-86400000, now-43260000, now-120000, now-60000]);
assert.equal(day[1].gap,true,'a long interruption breaks the curve');
ctx.dashboardShowDay = false;
ctx.dashHistory = [{t:now-900001},{t:now-5000},{t:now}];
assert.deepEqual(Array.from(ctx.dashboardPlotHistory(), point=>point.t), [now-5000, now]);
ctx.dashboardPrecisionEnabled = true;
ctx.dashboardPrecisionMs = 500;
ctx.historyMaxPoints = 360;
ctx.dashHistory = [{t:now-45000},{t:now-10000},{t:now-5000},{t:now}];
ctx.dashboardPrecisionHistory = [180001, 3000, 1000, 500, 0].map(age => ({t:now-age, precision:true}));
const precision = ctx.dashboardPlotHistory();
assert.deepEqual(Array.from(precision, point=>point.t),
  [45000,27500,10000,5000,3000,2000,1000,500,0].map(age => now-age));
assert.equal(precision[1].gap, true, 'a break between standard samples stays visible');
assert.equal(precision[5].gap, true, 'a missed stretch between precision samples stays visible');
assert.equal(precision.at(-1).precision, true, 'precision wins at an identical timestamp');
let tick;
const refreshCalls = [];
Object.assign(ctx, {
  page:'dashboard', dashboardRefreshMs:1000, dashboardRefreshTimer:null,
  dashboardRefreshInFlight:null, refreshLabelPatch(){}, clearInterval(){},
  refresh: graphsOnly => refreshCalls.push(graphsOnly),
  setInterval(callback, ms) { assert.equal(ms, 500); tick = callback; return 1; }
});
vm.runInContext(fn('scheduleDashboardRefresh'), ctx);
ctx.scheduleDashboardRefresh();
tick();
ctx.dashboardRefreshInFlight = true;
tick();
ctx.dashboardRefreshInFlight = null;
tick();
assert.deepEqual(refreshCalls, [true, false], 'precision polling skips a busy request and preserves the next full refresh');
const configured = [{id:'one', type:'SSR', name:'SSR principal'}, {id:'relay', type:'RELAY', name:'Relais exclu'}];
const ssrRuntime = {id:'one', commandPercent:42, outputPercent:40};
Object.assign(ctx, {
  actuatorConfigList:()=>configured,
  actuatorRuntimeList:()=>[ssrRuntime, {id:'unconfigured'}],
  actuatorOrigin: cfg=>cfg.source || 'LOCAL',
  actuatorRuntimeStatus: runtime=>runtime ? {label:'OK', cls:'ok'} : {label:'Inconnu', cls:'muted'},
  actuatorValueText:(value, unit)=>value + ' ' + unit,
  esc:String
});
for (const name of ['actuatorUsage','isOutputActuator','energyIcon','flowElectrons','dashboardSsrNodes']) vm.runInContext(fn(name), ctx);
assert.match(ctx.dashboardSsrNodes().markup, /SSR principal.*42 %.*40 %/);
assert.doesNotMatch(ctx.dashboardSsrNodes().markup, /Relais exclu|unconfigured/);
ssrRuntime.outputPercent = 0;
assert.doesNotMatch(ctx.dashboardSsrNodes().markup, /animateMotion/);
ssrRuntime.outputPercent = 1;
const slowFlow = ctx.dashboardSsrNodes().markup;
assert.match(slowFlow, /rgb\(255,218,0\)/);
ssrRuntime.outputPercent = 100;
const fastFlow = ctx.dashboardSsrNodes().markup;
assert.match(fastFlow, /rgb\(255,0,0\)/);
assert.match(fastFlow, /<path class="ssrFlowBranch" d="M380 130 V202 H344"/);
assert.match(fastFlow, /<animateMotion path="M380 130 V202 H344"/);
assert.ok(Number(fastFlow.match(/dur="([\d.]+)s"/)[1]) < Number(slowFlow.match(/dur="([\d.]+)s"/)[1]));
configured.push({id:'two', type:'SSR', name:'SSR ajouté', source:'ESP-NOW'});
assert.match(ctx.dashboardSsrNodes().markup, /SSR ajouté.*Inconnu/);

configured.push({id:'three', type:'SSR', name:'SSR sur la ligne suivante'});
assert.equal(ctx.dashboardSsrNodes().height, 350);
assert.match(ctx.dashboardSsrNodes().markup, /SSR sur la ligne suivante/);

Object.assign(ctx, {
  stateEnergy:()=>({gridPowerW:150, productionW:300, consumptionW:450}),
  hasValue:value=>value != null, fmt:String,
  gridJsyApparentPowerVA:()=>null,
  dashboardValue:(value, unit)=>value + ' ' + unit, helpIcon:()=> '',
  stopGraphPolling(){}, dashboardHeader:()=>'', modernDashboardGraphs:()=>'',
  activeSensorsPanel:()=>'', systemHealthPanel:()=>''
});
for (const name of ['energyFlowSvg','dashboard']) vm.runInContext(fn(name), ctx);
const dashboard = ctx.dashboard();
assert.match(dashboard, /Synoptique/);
assert.match(dashboard, /viewBox="0 0 760 350"/);
assert.match(dashboard, /SSR principal.*SSR ajouté.*SSR sur la ligne suivante/);
assert.match(dashboard, /synopticIcon icon-home/);
assert.match(dashboard, /synopticIcon icon-grid/);
assert.doesNotMatch(dashboard, /icon-radiator|icon-tank/);
configured[0].usage = 'home_heating';
configured[2].usage = 'hot_water';
assert.match(ctx.dashboardSsrNodes().markup, /icon-radiator.*SSR principal/);
assert.match(ctx.dashboardSsrNodes().markup, /icon-tank.*SSR ajouté/);
assert.equal(ctx.isOutputActuator(configured[2]), true);
configured.length = 0;
assert.match(ctx.dashboard(), /Aucun SSR configuré/);
for (const [productionW, color] of [[0,'128,128,128'],[500,'192,170,91'],[1000,'255,211,54'],[1300,'255,211,54']]) {
  ctx.stateEnergy = () => ({productionW});
  const svg = ctx.energyFlowSvg();
  assert.ok(svg.includes('class="sunNode" transform="translate(96 82)" color="rgb(' + color + ')"'));
  assert.ok(svg.includes('class="flowValue solarValue" x="205" y="68">' + productionW + ' W production'));
}
console.log('PASS dashboard graphs and synoptic rendering with zero or multiple configured SSRs.');
