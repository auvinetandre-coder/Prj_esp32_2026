const assert=require('assert/strict');
const {state,settings,step,valid}=require('./pid_test_runtime');
function tick(s,c,measurement,t,changed=false) { const ok=step(s,c,measurement,t,t,changed); s.appliedPercent=s.outputPercent; return ok; }
// Position form: P alone must not accumulate at a constant error.
let c=settings({kp:.1,ki:0,risePerSecond:10000,fallPerSecond:10000,deadbandW:0}),s=state();
for(let t=1000;t<=10000;t+=250) { assert.ok(tick(s,c,-100,t)); assert.equal(s.outputPercent,10); }
assert.ok(tick(s,c,100,10250)); assert.equal(s.outputPercent,0,'positive import reduces load');
// I uses real elapsed time, independent of packet frequency.
function integrate(period) {const x=state(), cfg=settings({kp:0,ki:.01,deadbandW:0,risePerSecond:10000});tick(x,cfg,-100,1000);for(let t=1000+period;t<=5000;t+=period)tick(x,cfg,-100,t);return x.outputPercent;}
assert.ok(Math.abs(integrate(250)-integrate(1000))<1e-8);
// Identical sample timestamps never repeat I/D or ramps.
s=state(); c=settings(); tick(s,c,-1000,1000);const first={...s};
for(let t=1100;t<5900;t+=100)assert.ok(step(s,c,-1000,1000,t));
assert.deepEqual(s,first);assert.equal(step(s,c,-1000,1000,6000),false);assert.equal(s.outputPercent,0);
// Stale/invalid input cuts immediately, without a ramp.
for(const bad of [NaN,Infinity,-Infinity]){s=state();tick(s,c,-1000,1000);assert.equal(step(s,c,bad,1200,1200),false);assert.equal(s.outputPercent,0);}
for(const bad of [{kp:NaN},{ki:-1},{kd:Infinity},{maxPercent:101},{timeoutMs:10},{intervalMs:0}])assert.equal(valid(settings(bad)),false);
// Anti-windup prevents accumulation at a ceiling and during a slow ramp.
s=state();c=settings({kp:1,ki:1,maxPercent:25,risePerSecond:5,deadbandW:0});
for(let t=1000;t<=30000;t+=250){tick(s,c,-1000,t);assert.equal(s.integral,0);assert.ok(s.outputPercent<=25);}
tick(s,c,1000,30250);assert.equal(s.outputPercent,0,'fast retreat from saturation');
// Deadband holds; lowering the configured limit is immediate even in deadband.
s=state();c=settings({kp:.1,ki:0,risePerSecond:10000});tick(s,c,-500,1000);tick(s,c,0,1250);assert.equal(s.outputPercent,50);
c.maxPercent=10;tick(s,c,0,1500,true);assert.equal(s.outputPercent,10);
// Setpoint changes produce no derivative kick; gain changes track the accepted command.
s=state();c=settings({kp:.1,ki:0,kd:1,risePerSecond:10000,deadbandW:0});tick(s,c,-100,1000);c.setpointW=100;tick(s,c,-100,1250,true);assert.ok(s.d===0);assert.equal(s.outputPercent,10);
// D is taken on the measurement and filtered.
tick(s,c,-50,1500);assert.ok(s.d<0 && s.d>-200);
// Separate SSR states never share the integral or derivative.
const a=state(),b=state();c=settings({kp:.01,ki:.1,deadbandW:0,risePerSecond:10000});tick(a,c,-100,1000);tick(a,c,-100,1250);tick(b,c,100,1000);assert.equal(b.outputPercent,0);assert.ok(a.integral>0);assert.equal(b.integral,0);
// Millis rollover uses unsigned time differences.
s=state();c=settings({kp:0,ki:.01,deadbandW:0,risePerSecond:10000});tick(s,c,-100,0xffffff00);tick(s,c,-100,244);assert.ok(Math.abs(s.outputPercent-.7)<1e-6);
// A simple closed-loop resistive load converges without repeated-output accumulation.
s=state();c=settings({kp:.02,ki:.02,deadbandW:10,risePerSecond:10,fallPerSecond:100});
let grid=-600;for(let t=1000;t<180000;t+=250){tick(s,c,grid,t);grid=-600+1500*s.outputPercent/100;}
assert.ok(Math.abs(grid)<=10,'absorbs a 600 W surplus');
for(let t=180000;t<240000;t+=250){grid=300+1500*s.outputPercent/100;tick(s,c,grid,t);}
assert.equal(s.outputPercent,0,'cuts after surplus disappears');
console.log('PASS positional PID: sign, position form, real dt, sample gating, stale/NaN OFF, anti-windup, ramps, transitions, filtered D, independent SSRs, rollover, closed loop');
