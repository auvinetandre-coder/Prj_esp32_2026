const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const ui=fs.readFileSync(path.join(__dirname,'../../data/www/app.js'),'utf8');
const elements=new Map();
const element=id=>{if(!elements.has(id))elements.set(id,{innerHTML:'',textContent:'',value:'',style:{},classList:{add(){},remove(){},toggle(){}},querySelectorAll(){return []},getAttribute(){return null},addEventListener(){}});return elements.get(id);};
const ctx={console,Date,Math,JSON,URLSearchParams,localStorage:{getItem:()=>null,setItem(){},removeItem(){}},document:{getElementById:element,querySelectorAll:()=>[],querySelector:()=>null,addEventListener(){},documentElement:{clientWidth:1280},body:{classList:{add(){},remove(){}}}},setTimeout:()=>1,clearTimeout(){},setInterval:()=>1,clearInterval(){},requestAnimationFrame:()=>1,cancelAnimationFrame(){},alert(){},confirm:()=>false,location:{hostname:'preview.local'},navigator:{},devicePixelRatio:1,innerWidth:1280,innerHeight:900,addEventListener(){},matchMedia:()=>({matches:false})};
ctx.window=ctx;vm.createContext(ctx);
vm.runInContext(ui.replace(/render\(\);\s*setInterval\(pollActuatorRuntime, 1000\);\s*scheduleDashboardRefresh\(\);\s*refresh\(\);\s*$/,''),ctx);
ctx.api=async url=>url==='/api/system-info'?{device:{name:'ESP <script>bad</script>'},firmwareVersion:'A',littleFsVersion:'B',storage:{status:'OK'},ota:{runningLabel:'ota_0'}}:{};
ctx.cache.sensors={sensors:[],ds18b20:[]};ctx.cache.actuators={actuators:[]};
(async()=>{
 ctx.state={systemHealth:{espnow:'OK'}};assert.equal(ctx.espNowServiceLabel(),'pret','status-lite canonical health must not report ESP-NOW unavailable');
 ctx.state={espNowReady:false,systemHealth:{espnow:'OK'}};assert.equal(ctx.espNowServiceLabel(),'indisponible','explicit boolean wins for legacy API');
 ctx.state={};assert.equal(ctx.espNowServiceLabel(),'etat inconnu','missing state is unknown, not an outage');

 await ctx.systemPage();let html=element('app').innerHTML;
 assert.equal((html.match(/onclick="restartSystemPage\(\)"/g)||[]).length,1);
 assert.equal((html.match(/onclick="bootRecoveryPage\(\)"/g)||[]).length,1);
 assert.match(html,/ESP &lt;script&gt;bad&lt;\/script&gt;/);
 assert.doesNotMatch(html,/versions differentes<|Heap libre|Simulation|Boite noire/);
 assert.match(html,/indisponible/,'missing values must not become zero');
 assert.equal(ctx.workbenchBytes(0),'0 o');assert.equal(ctx.workbenchBytes(null),'indisponible');
 for(const sample of [{safetyLevel:'OK',safetyTripped:false},{safetyLevel:'INIT',safetyTripped:false},{safetyLevel:'CRITICAL',safetyTripped:true,safetyReason:'Test <b>critique</b>'}]){
  Object.assign(ctx.state,sample,{sensors:[{id:'remote',name:'Compteur distant',origin:'ESP-NOW',type:'JSY-MK-194T',ok:false,neverSeen:true,values:{}}]});
  ctx.diagnosticPage();html=element('app').innerHTML;
  assert.match(html,/ESP-NOW/);assert.match(html,/Jamais recu/);assert.match(html,/serialConsoleSlot/);
  assert.doesNotMatch(html,/restartSystemPage|bootRecoveryPage|simEnable|simulationControlsHtml/);
  assert.doesNotMatch(html,/Aucun defaut signale<\/span>/,'unknown or missing sensor cannot show all clear');
  if(sample.safetyTripped)assert.match(html,/Test &lt;b&gt;critique&lt;\/b&gt;/);
 }
 const wifi=ctx.wifiDiagnosticsHtml({retained:1,totalEvents:40,associated:false,connected:false,rssi:null,rssiMin:-90,rssiMax:-50,lastConnectedSnapshot:{ip:'192.168.0.90',channel:6,rssi:-61,ageMs:5000},history:[{uptimeMs:0,ageMs:200,event:'<script>bad</script>',reason:201,reasonLabel:'AP absent'}]});
 assert.match(wifi,/Derniere photo connectee/);assert.match(wifi,/&lt;script&gt;bad&lt;\/script&gt;/);assert.match(wifi,/Boot \? \+ 0 s/);
 console.log('PASS: pages separees, commandes uniques, etats absents/degrades/critiques, origine radio et echappement HTML');
})().catch(e=>{console.error(e);process.exitCode=1});
