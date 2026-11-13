// Regression de retrait : pas d'injection de mesures ni d'API active ; securites conservees.
const fs=require('fs'),path=require('path'),assert=require('assert/strict');
const read=p=>fs.readFileSync(path.join(__dirname,'../..',p),'utf8');
for(const p of ['RouteurSolaireESP32.ino','src/web/web_ui.h','src/web/web_ui.cpp','src/runtime/runtime_state.h','src/runtime/runtime_state.cpp','src/sensors/sensor_manager.cpp','src/actuators/actuator_manager.cpp'])assert.doesNotMatch(read(p),/SimulationManager|state\.simulationMode|api\/simulation|PERF_SIMULATION/,p);
assert.doesNotMatch(read('src/logic/rule_engine.cpp'),/\{"Systeme", "simulationMode"/);
assert.match(read('src/logic/rule_engine.cpp'),/!validateRulesDocument\(oneRule, errors\)\) continue;/,'invalid retired conditions skip entire rule before ALORS/SINON');
assert.match(read('src/actuators/actuator_manager.cpp'),/if \(state.safetyTripped\) lockAllForSafety/);
assert.match(read('src/actuators/actuator_manager.cpp'),/!reported.legacyOutputInhibited/,'old radio inhibition still rejects fake output state');
assert.match(read('src/communication/espnow_manager.cpp'),/legacyOutputInhibited = message.reservedCounter & 8U/);
assert.match(read('src/config/config_manager.cpp'),/"simulationMode", "simulation"/);
assert.doesNotMatch(read('data/www/app.js'),/api\/simulation|function simulationPage|function simEnable|Diagnostic & Simulation/);
console.log('PASS: simulation locale retiree, regles invalides ignorees, Safety et inhibition radio historique conservees');
