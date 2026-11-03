// Offline analysis only. Never connects to or changes an ESP.
// node tools/analyse_jsy_capture.js capture.json output-directory
const fs = require('fs');
const path = require('path');

function average(samples, now, windowMs, maxGapMs = 1000) {
  const begin = now - windowMs;
  let sum = 0, covered = 0;
  for (let i = 0; i + 1 < samples.length; ++i) {
    const a = Math.max(begin, samples[i].t);
    const b = Math.min(now, samples[i + 1].t);
    if (b <= a) continue;
    // Missing periods are not filled indefinitely with the last good watts.
    if (samples[i + 1].t - samples[i].t > maxGapMs || !Number.isFinite(samples[i].w)) return null;
    sum += samples[i].w * (b - a);
    covered += b - a;
  }
  return covered === windowMs ? sum / covered : null;
}

function stats(xs) {
  xs = xs.filter(Number.isFinite);
  if (!xs.length) return null;
  const mean = xs.reduce((a,b) => a+b, 0) / xs.length;
  const sorted = [...xs].sort((a,b)=>a-b);
  return {count:xs.length, mean, min:sorted[0], max:sorted.at(-1),
    std:Math.sqrt(xs.reduce((a,b)=>a+(b-mean)**2,0)/xs.length),
    median:sorted[Math.floor(sorted.length/2)], p95:sorted[Math.floor((sorted.length-1)*.95)]};
}

function analyse(capture) {
  const samples = [], pidSamples = [], exports = [];
  let duplicates = 0;
  const incoherentSnapshots = [];
  for (const r of capture.rows) {
    const t = r.remote ? r.remote.lastUpdateMs : r.lastJsyReadMs;
    const w = r.remote ? r.remote.values.CH2_W_RAW : r.powerW;
    const power = r.remote ? r.remote.values.CH2_POWER : r.powerW;
    // Outside the documented near-zero fallback, both keys must agree.
    // A callback may update shared fields while HTTP serializes the snapshot.
    const incoherent = r.remote && Math.abs(w) >= 5 && Math.abs(power-w) > .01;
    if (incoherent) incoherentSnapshots.push({wallMs:r.wallMs,t,rawW:w,powerW:power});
    if (Number.isFinite(t) && Number.isFinite(w) && !incoherent) {
      if (samples.at(-1)?.t === t) duplicates++;
      else samples.push({t,w,power,pidW:r.pidW,globalW:r.globalFilteredW,command:r.command,wallMs:r.wallMs});
    }
    const p = r.pids?.[0];
    if (p?.lastMeasurementMs && p.lastMeasurementMs !== pidSamples.at(-1)?.t)
      pidSamples.push({t:p.lastMeasurementMs, measuredW:p.measuredW});
    for (const e of r.exports || []) {
      if (e.sensorId === 2 && e.lastSentMs && e.lastSentMs !== exports.at(-1)) exports.push(e.lastSentMs);
    }
  }
  for (const s of samples) {
    s.mean1sW = average(samples,s.t,1000);
    s.mean2sW = average(samples,s.t,2000);
  }
  // Compare on exactly the same timestamps after both windows have filled.
  const common = samples.filter(s=>s.mean1sW !== null && s.mean2sW !== null);
  const deltas = xs => xs.slice(1).map((x,i)=>x-xs[i]);
  return {samples,summary:{meta:capture.meta,rows:capture.rows.length,
    errors:capture.rows.filter(r=>r.error),incoherentSnapshots,repeatedSnapshots:duplicates,
    observedSamples:samples.length,
    observedSampleIntervalsMs:stats(deltas(samples.map(s=>s.t))),
    observedExportIntervalsMs:stats(deltas(exports)),
    observedPIDAcquisitionIntervalsMs:stats(deltas(pidSamples.map(s=>s.t))),
    requestDurationMs:stats(capture.rows.map(r=>r.requestMs)),
    comparison:{rawW:stats(common.map(s=>s.w)),mean1sW:stats(common.map(s=>s.mean1sW)),mean2sW:stats(common.map(s=>s.mean2sW)),
      existingPIDW:stats(common.map(s=>s.pidW)),existingGlobalW:stats(common.map(s=>s.globalW)),
      historicalPowerMinusRawW:stats(common.map(s=>s.power-s.w)),commandPercent:stats(common.map(s=>s.command))},
    limits:['Observed intervals may contain skipped acquisitions between HTTP snapshots.',
      'Receiver time is sensor-slot reception time, not the per-key acquisition identity.',
      'Source powerW is historical CH2_POWER, not necessarily raw meter watts.',
      'Window means use causal zero-order hold of observed samples; no claim about internal JSY integration.',
      'Statistics are per observed acquisition, not continuous-time energy metrology.',
      '1000 ms gap limit is for offline comparison only; no firmware protection is changed.']}};
}

if (require.main === module) {
  const [input, output] = process.argv.slice(2);
  if (!input || !output) throw Error('Usage: node tools/analyse_jsy_capture.js capture.json output-directory');
  const result = analyse(JSON.parse(fs.readFileSync(input,'utf8')));
  fs.mkdirSync(output,{recursive:true});
  const name=path.basename(input,'.json');
  fs.writeFileSync(path.join(output,name+'_summary.json'),JSON.stringify(result.summary,null,2));
  const keys=['t','wallMs','w','power','globalW','pidW','mean1sW','mean2sW','command'];
  fs.writeFileSync(path.join(output,name+'_comparison.csv'),keys.join(',')+'\n'+result.samples.map(s=>keys.map(k=>s[k]??'').join(',')).join('\n'));
  console.log(JSON.stringify(result.summary,null,2));
}
module.exports={average,stats,analyse};
