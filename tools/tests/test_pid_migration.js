const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const src=fs.readFileSync(path.join(__dirname,'../../src/config/pid_config_migration.h'),'utf8');
let body=src.slice(src.indexOf('{',src.indexOf('inline bool migratePIDActions'))+1,src.lastIndexOf('}'));
body=body.replace(/rule\["(actions|then|elseActions|else)"\]\.is<JsonArray>\(\)/g,'Array.isArray(rule["$1"])').replace(/rule\["(actions|elseActions)"\]\.set\(rule\["(then|else)"\]\)/g,'rule["$1"] = JSON.parse(JSON.stringify(rule["$2"]))');
body=body.replace(/for \(JsonObject (\w+) : ([^\n]+?)\.as<JsonArray>\(\)\)/g,'for (const $1 of ($2 || []))')
 .replace(/for \(JsonObject sensor : sensors\)/g,'for (const sensor of sensors)')
 .replace(/for \(const char \*branch : \{([^}]+)\}\)/g,'for (const branch of [$1])')
 .replace(/\b(?:const )?(?:String|bool|float|unsigned)\s+/g,'let ')
 .replace(/a.containsKey\(([^)]+)\)/g,'Object.hasOwn(a,$1)')
 .replace(/(router\["kp"\]) \| (router\["pidKp"\]) \| 0.02f/g,'first($1,$2,0.02)')
 .replace(/(router\["ki"\]) \| (router\["pidKi"\]) \| 0.002f/g,'first($1,$2,0.002)')
 .replace(/(router\["kd"\]) \| (router\["pidKd"\]) \| 0.0f/g,'first($1,$2,0)')
 .replace(/((?:a|sensor|router)\["[^"]+"\]) \| ("[^"]*"|true|false|[\d.]+f?)/g,'first($1,$2)')
 .replace(/\b(\d+(?:\.\d+)?)(?:f|UL)\b/g,'$1');
const ctx={first:(...xs)=>xs.find(x=>x!=null),isfinite:Number.isFinite,logf:Math.log};vm.createContext(ctx);
vm.runInContext('function migrate(root,router,sensors){'+body+'}',ctx);
const doc={version:2,rules:[{id:'a',conditions:[{custom:true}],actions:[{actuatorId:'one',command:'setPowerFromLinky',maxPercent:80,custom:{keep:true}}],elseActions:[{actuatorId:'two',command:'setPowerFromSurplus',gridSetpointW:0,kp:0}]}]};
const router={kp:.04,ki:.003,kd:0,pidEnabled:false,gridSetpointW:50,maxOutputRampPercentPerSecond:3};
assert.equal(ctx.migrate(doc,router,[{id:'custom_tic',type:'TIC Linky',enabled:true}]),true);
const a=doc.rules[0].actions[0],b=doc.rules[0].elseActions[0];
assert.equal(a.command,'setPowerFromSurplus');assert.equal(a.pidSensorId,'custom_tic');assert.equal(a.gridSetpointW,100);assert.equal(a.requireProduction,true);assert.equal(a.pidEnabled,false);assert.equal(a.maxPercent,80);assert.equal(a.custom.keep,true);
assert.equal(b.gridSetpointW,0);assert.equal(b.kp,0);assert.equal(b.ki,.003);assert.equal(b.maxOutputDropPercentPerSecond,3);
const json=JSON.stringify(doc);assert.equal(ctx.migrate(doc,{kp:99},[]),false);assert.equal(JSON.stringify(doc),json,'idempotent, global changes do not overwrite local settings');
const ambiguous={rules:[{then:[{command:'setPowerFromLinky'}],else:[{command:'setPowerFromSurplus',regulation:'PROPORTIONAL'}]}]};
ctx.migrate(ambiguous,{},[{id:'a',type:'TIC Linky'},{id:'b',type:'TIC Linky'}]);assert.equal(ambiguous.rules[0].then[0].pidSensorId,'unavailable');assert.equal(ambiguous.rules[0].else[0].regulation,'PROPORTIONAL');
console.log('PASS PID migration: all branches, Linky -> generic, custom sensor id, +100 W, production guard, gains, explicit zero, limits, disabled state, metadata, ambiguity, idempotence');
