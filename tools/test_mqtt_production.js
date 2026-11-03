// Test the real WebUI save handler without a device: node tools/test_mqtt_production.js
const fs = require("fs");
const path = require("path");
const vm = require("vm");
const assert = require("assert/strict");
const source = fs.readFileSync(path.join(__dirname, "../data/www/app.js"), "utf8");
const start = source.indexOf("async function saveMqtt()");
const handler = source.slice(start, source.indexOf("\n}", start) + 2);
const fields = {};
for (const id of handler.matchAll(/\$\("([^"]+)"\)/g)) fields[id[1]] = {value: ""};
Object.assign(fields, {
  mqttProductionTopic: {value: "maison/solax/productionW"},
  mqttProductionTimeout: {value: "900"},
  mqttBaseTopic: {value: "routeurSolaire/linky"},
  mqttStateTopic: {value: "routeurSolaire/state"},
  mqttCommandTopic: {value: "routeurSolaire/command"},
  mqttAvailabilityTopic: {value: "routeurSolaire/availability"}
});
const booleans = {mqttEnabled: true, mqttProductionEnabled: true, mqttIndividual: true};
const original = {wifi: {ssid: "keep"}, router: {kp: 0.02}, mqtt: {password: "keep", extra: 42}};
const posts = [];
const context = {
  cache: {system: structuredClone(original)}, $: id => fields[id],
  boolField: id => !!booleans[id], alert: () => {}, refresh: async () => {},
  postJson: async (url, data) => {
    assert.equal(url, "/api/system");
    posts.push(structuredClone(data));
    return {ok: true};
  }
};
vm.createContext(context);
vm.runInContext(handler, context);
(async () => {
  await context.saveMqtt();
  assert.equal(posts.length, 1);
  assert.equal(posts[0].mqtt.productionSensor.timeoutMs, 900000);
  assert.equal(posts[0].mqtt.productionSensor.topic, "maison/solax/productionW");
  assert.equal(posts[0].mqtt.productionSensor.enabled, true);
  assert.deepEqual(posts[0].wifi, original.wifi);
  assert.deepEqual(posts[0].router, original.router);
  assert.equal(posts[0].mqtt.password, "keep", "blank password must preserve the current credential");
  assert.equal(posts[0].mqtt.extra, 42);
  for (const topic of ["", "maison/#", "maison/+/power", "a b", "routeurSolaire/linky/productionW", "routeurSolaire/state", "routeurSolaire/command"]) {
    fields.mqttProductionTopic.value = topic;
    await context.saveMqtt();
  }
  assert.equal(posts.length, 1, "invalid, command and self-published topics must not be saved");
  fields.mqttProductionTopic.value = "maison/solax/productionW";
  for (const timeout of ["", "0", "-1", "86401", "NaN", "Infinity"]) {
    fields.mqttProductionTimeout.value = timeout;
    await context.saveMqtt();
  }
  assert.equal(posts.length, 1, "invalid timeout must not be saved");
  fields.mqttProductionTimeout.value = "1";
  await context.saveMqtt();
  assert.equal(posts.at(-1).mqtt.productionSensor.timeoutMs, 1000);
  fields.mqttProductionTimeout.value = "86400";
  booleans.mqttProductionEnabled = false;
  await context.saveMqtt();
  assert.equal(posts.at(-1).mqtt.productionSensor.enabled, false);
  assert.equal(posts.at(-1).mqtt.productionSensor.timeoutMs, 86400000);
  console.log("MQTT production WebUI: configuration, preservation and validation OK");
})().catch(error => { console.error(error); process.exitCode = 1; });
