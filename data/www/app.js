var page = "dashboard";
var dashboardRefreshMs = Number(localStorage.getItem("dashboardRefreshMs") || 5000);
var dashboardRefreshTimer = null;
var dashHistory = [];
var lastHistorySampleMs = 0;
var historySampleIntervalMs = 5000;
var historyMaxPoints = 360;
var realtimeGraphEnabled = false;
var realtimeGraphStepMs = Number(localStorage.getItem("realtimeGraphStepMs") || 300);
var realtimeGraphTimer = null;
var realtimeGraphStopTimer = null;
var realtimeGraphStartedAt = 0;
var realtimeHistory = [];
var graphData = [];
var graphPollTimer = null;
var lastGraphDrawMs = 0;
var graphConfig = loadGraphConfig();
var dashboardMetricKeys = loadDashboardList("dashboardMetrics", ["gridPowerW", "injectionW", "surplusW", "ssr1PowerPct"]);
var dashboardChartKeys = loadDashboardList("dashboardCharts", ["injectionW", "ssr1PowerPct", "tankTopC"]);
var dashboardGraphDefaults = {
  network: ["gridPowerW", "injectionW", "consumptionW", "surplusW"],
  outputs: ["ssr1PowerPct", "ssr2PowerPct", "robotDynPowerPct"],
  temps: ["tankTopC", "tankMiddleC", "tankBottomC"]
};
var dashboardGraphNetwork = loadDashboardList("dashboardGraphNetwork", dashboardGraphDefaults.network);
var dashboardGraphOutputs = loadDashboardList("dashboardGraphOutputs", dashboardGraphDefaults.outputs);
var dashboardGraphTemps = loadDashboardList("dashboardGraphTemps", dashboardGraphDefaults.temps);
var dirtyPages = {};
var state = {
  ok: false,
  networkMode: "chargement",
  stationIp: "-",
  apIp: "-",
  wifiConnected: false,
  wifiSsid: "-",
  rssi: null,
  safetyLevel: "INIT",
  safetyReason: "",
  safetyTripped: false,
  simulationMode: false,
  simulationType: "off",
  simulationRemainingMs: 0,
  mqttEnabled: false,
  mqttConnected: false,
  mqttStatus: "MQTT_DISABLED",
  lastMqttPublishAgeMs: 4294967295,
  moduleName: "Routeur solaire",
  role: "-",
  deviceId: "-",
  firmwareVersion: "-",
  arduinoCore: "-",
  idfVersion: "-",
  chipModel: "-",
  chipRevision: "-",
  cpuMhz: "-",
  flashBytes: 0,
  littleFsTotal: 0,
  littleFsUsed: 0,
  gridPowerW: 0,
  gridPowerRawW: 0,
  gridPowerFilteredW: 0,
  gridPowerSource: "JSY",
  jsyGridPowerW: null,
  ticGridPowerW: null,
  ticStatus: "TIC_NOT_CONFIGURED",
  ticApparentPowerVA: null,
  ticCurrentA: null,
  ticEnergyWh: 0,
  ticTariff: "",
  ticPeriod: "",
  lastTicReadMs: 0,
  ticErrorCount: 0,
  activePowerW1: 0,
  activePowerW2: 0,
  currentA1: 0,
  currentA2: 0,
  injectionW: 0,
  consumptionW: 0,
  surplusW: 0,
  tankTopC: null,
  tankMiddleC: null,
  tankBottomC: null,
  ssr1PowerPct: 0,
  ssr2PowerPct: 0,
  robotDynPowerPct: 0,
  pidOutputPercent: 0,
  pidErrorW: 0,
  pidMeasuredW: 0,
  gridSetpointW: 0,
  deadbandW: 30,
  pidKp: 0,
  pidKi: 0,
  pidKd: 0,
  maxOutputRampPercentPerSecond: 0,
  heaterMaxPowerW: 0,
  heaterPowerW: 0,
  pidStatus: "IDLE",
  heapFree: 0
};
var cache = {};

document.getElementById("app").innerHTML = "<h1>Initialisation WebUI...</h1>";

function $(id) { return document.getElementById(id); }

function esc(value) {
  return String(value == null ? "" : value).replace(/[&<>"']/g, function (m) {
    return {"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[m];
  });
}

function fmt(value) {
  if (value == null || value === "" || (typeof value === "number" && !isFinite(value))) return "-";
  return typeof value === "number" ? Math.round(value * 10) / 10 : value;
}

var roles = ["ballon_haut", "ballon_milieu", "ballon_bas", "depart_eau_chaude", "retour_eau_froide", "ambiance", "autre"];
var sensorTypes = ["JSY-MK-194T", "TIC Linky", "DS18B20", "Analog", "Digital", "Virtual"];
var sensorRolesByType = {
  "JSY-MK-194T": ["mesure_reseau_principal", "mesure_production", "mesure_charge", "diagnostic", "custom"],
  "TIC Linky": ["compteur_officiel", "diagnostic", "coherence_energie", "custom"],
  "DS18B20": roles,
  "Analog": ["mesure_analogique", "niveau", "pression", "luminosite", "custom"],
  "Digital": ["etat_contact", "presence", "alarme", "custom"],
  "Virtual": ["surplus", "production", "consumption", "custom"]
};
var actuatorTypes = ["SSR", "RobotDyn Triac", "Relay", "PWM", "Digital Output", "Virtual"];
var actuatorModes = ["OFF", "ON_OFF", "BURST_FIRE", "TRAIN_ONDES_ENTIERES", "ZERO_CROSS_BURST", "LOW_FREQ_PWM", "PHASE_ANGLE", "MANUAL_SAFE"];
var actuatorModeByType = {
  "SSR": ["OFF", "BURST_FIRE", "TRAIN_ONDES_ENTIERES", "ZERO_CROSS_BURST", "LOW_FREQ_PWM", "MANUAL_SAFE"],
  "RobotDyn Triac": ["OFF", "PHASE_ANGLE", "ZERO_CROSS_BURST", "BURST_FIRE", "MANUAL_SAFE"],
  "Relay": ["OFF", "ON_OFF", "MANUAL_SAFE"],
  "PWM": ["OFF", "LOW_FREQ_PWM", "MANUAL_SAFE"],
  "Digital Output": ["OFF", "ON_OFF", "MANUAL_SAFE"],
  "Virtual": ["OFF", "ON_OFF", "BURST_FIRE", "LOW_FREQ_PWM", "PHASE_ANGLE", "MANUAL_SAFE"]
};
var actuatorModeHelp = {
  OFF: "Sortie forcee a l'arret. Mode le plus sur pour tester ou neutraliser un actionneur.",
  ON_OFF: "Commande simple marche/arret. Adapte aux relais et sorties digitales, pas au dosage fin de puissance.",
  BURST_FIRE: "Modulation par trains d'impulsions sur une periode lente. Adapte aux SSR zero-cross pour chauffe-eau resistif.",
  TRAIN_ONDES_ENTIERES: "Variante SSR par trains d'ondes completes. Limite les parasites car la commutation reste proche du passage par zero.",
  ZERO_CROSS_BURST: "Commande SSR synchronisee passage par zero. Bon choix pour charges resistives et SSR zero-cross.",
  LOW_FREQ_PWM: "PWM lent avec millis(). Utilisable pour SSR ou sortie basse frequence, a eviter sur relais mecanique rapide.",
  PHASE_ANGLE: "Angle de phase pour RobotDyn/Triac avec detection zero-cross. Permet un dosage fin mais genere plus de parasites.",
  MANUAL_SAFE: "Mode manuel limite par les securites. Les protections temperature et arret critique restent prioritaires."
};
var jsyClampRoles = ["grid", "production", "load", "custom"];
var ruleSources = [
  {id:"JSY-MK-194T", label:"JSY-MK-194T", measures:[["gridPowerW","number","W"],["injectionW","number","W"],["consumptionW","number","W"],["surplusW","number","W"],["voltageV","number","V"],["currentA","number","A"],["activePowerW","number","W"],["voltageV1","number","V"],["currentA1","number","A"],["activePowerW1","number","W"],["powerFactor1","number",""],["voltageV2","number","V"],["currentA2","number","A"],["activePowerW2","number","W"],["powerFactor2","number",""],["powerFactor","number",""],["frequencyHz","number","Hz"],["available","boolean",""]]},
  {id:"TIC Linky", label:"TIC Linky", measures:[["gridPowerW","number","W"],["apparentPowerVA","number","VA"],["currentA","number","A"],["tariff","text",""],["available","boolean",""],["lastValidReadAgeMs","number","ms"]]},
  {id:"sonde1", label:"sonde1", measures:[["temperatureC","number","C"],["available","boolean",""],["lastValidReadAgeMs","number","ms"]]},
  {id:"sonde2", label:"sonde2", measures:[["temperatureC","number","C"],["available","boolean",""],["lastValidReadAgeMs","number","ms"]]},
  {id:"sonde3", label:"sonde3", measures:[["temperatureC","number","C"],["available","boolean",""],["lastValidReadAgeMs","number","ms"]]},
  {id:"Systeme", label:"Systeme", measures:[["simulationMode","boolean",""],["wifiStatus","enum","",["CONNECTED","AP_FALLBACK","DISCONNECTED"]],["uptimeMs","number","ms"],["freeHeap","number","B"],["role","enum","",["MASTER","BACKUP","NODE_SENSOR","NODE_ACTUATOR","NODE_MIXED"]]]},
  {id:"Securite", label:"Securite", measures:[["safetyLevel","enum","",["OK","WARNING","DEGRADED","CRITICAL"]],["safetyReason","text",""],["isCritical","boolean",""]]},
  {id:"Redondance", label:"Redondance", measures:[["activeRole","enum","",["MASTER","BACKUP","NODE_SENSOR","NODE_ACTUATOR","NODE_MIXED"]],["isActiveMaster","boolean",""],["activeMasterId","text",""],["epoch","number",""],["lastHeartbeatAgeMs","number","ms"]]}
];
var ruleCommands = ["setActuatorPercent", "setPower", "setPowerFromSurplus", "setPowerWatts", "stop", "off", "on", "toggle", "setMode", "safetyShutdown", "logEvent", "setSafetyWarning"];
var commandLabels = {
  setActuatorPercent:"Regler puissance %",
  setPower:"Regler puissance %",
  setPowerFromSurplus:"Suivre le surplus solaire",
  setPowerWatts:"Regler puissance W",
  stop:"Arreter",
  off:"Eteindre",
  on:"Allumer",
  toggle:"Basculer",
  setMode:"Changer mode",
  safetyShutdown:"Arret securite",
  logEvent:"Ajouter un log",
  setSafetyWarning:"Declarer alerte"
};
var commandHelps = {
  setActuatorPercent:"Commande l'actionneur avec un pourcentage fixe ou une mesure suivie.",
  setPower:"Commande l'actionneur avec un pourcentage fixe ou une mesure suivie.",
  setPowerFromSurplus:"Calcule automatiquement la puissance avec le surplus solaire disponible.",
  setPowerWatts:"Commande l'actionneur en watts.",
  stop:"Force l'actionneur a l'arret.",
  off:"Eteint une sortie simple.",
  on:"Allume une sortie simple, si la securite l'autorise.",
  toggle:"Inverse l'etat d'une sortie simple.",
  setMode:"Change le mode de pilotage de l'actionneur.",
  safetyShutdown:"Coupe l'actionneur comme en securite.",
  logEvent:"Ajoute un evenement dans les logs.",
  setSafetyWarning:"Passe le systeme en avertissement logiciel."
};
var measureLabels = {
  gridPowerW:"Puissance reseau",
  injectionW:"Injection reseau",
  consumptionW:"Consommation maison",
  surplusW:"Surplus solaire",
  voltageV:"Tension reseau",
  currentA:"Courant",
  activePowerW:"Puissance active",
  activePowerW1:"Puissance active 1",
  activePowerW2:"Puissance active 2",
  voltageV1:"Tension pince 1",
  voltageV2:"Tension pince 2",
  currentA1:"Courant pince 1",
  currentA2:"Courant pince 2",
  powerFactor1:"Facteur puissance 1",
  powerFactor2:"Facteur puissance 2",
  powerFactor:"Facteur de puissance",
  frequencyHz:"Frequence",
  apparentPowerVA:"Puissance apparente",
  temperatureC:"Temperature",
  available:"Capteur disponible",
  safetyLevel:"Niveau securite",
  simulationMode:"Mode simulation",
  role:"Role module"
};
var helpTexts = {
  dashboard: [
    ["Securite OK", "Aucun defaut critique detecte par le SafetyManager. Les regles peuvent commander les sorties."],
    ["WARNING", "Un probleme existe, mais le routeur peut continuer avec prudence."],
    ["DEGRADED", "Fonctionnement degrade. Certaines informations manquent, la puissance peut etre limitee."],
    ["CRITICAL", "Defaut critique. Les sorties SSR1, SSR2 et RobotDyn sont coupees."],
    ["Injection", "Puissance renvoyee vers le reseau. C'est cette energie que le routeur essaie d'utiliser."],
    ["Surplus", "Puissance disponible pour chauffer l'eau. Souvent identique a l'injection."]
  ],
  sensors: [
    ["OK", "Le capteur est actif et une valeur recente est disponible."],
    ["Absent / non lu", "Le capteur est configure, mais aucune valeur valide n'est disponible."],
    ["Critique", "Si cette sonde manque, le SafetyManager peut couper les sorties selon le mode Safety."],
    ["DS18B20", "Sonde de temperature OneWire. Les roles sonde1/2/3 sont configurables."]
  ],
  actuators: [
    ["Commande %", "Puissance demandee par les regles ou le mode manuel."],
    ["Safety verrouille", "La sortie est bloquee par securite et ne doit pas s'activer."],
    ["BURST_FIRE", "Commande SSR par cycles entiers, adaptee aux charges resistives."],
    ["PHASE_ANGLE", "Commande triac RobotDyn par angle de phase. Ne pas utiliser sur SSR classique."]
  ],
  logic: [
    ["SI", "Conditions a verifier avant d'executer la regle."],
    ["ALORS", "Actions lancees si les conditions sont vraies."],
    ["Priorite", "Les regles avec priorite plus haute sont traitees avant les autres."],
    ["Source/Mesure", "La source est le capteur ou le systeme. La mesure est la valeur disponible."]
  ],
  diagnostic: [
    ["Diagnostic", "Page pour comprendre rapidement ce qui fonctionne ou bloque."],
    ["Simulation", "Permet de tester les regles sans capteurs physiques et sans activer les sorties 230 V."],
    ["Logs", "Historique court des evenements importants du systeme."],
    ["Perte JSY", "Scenario utile pour verifier que les securites reagissent correctement."]
  ],
  settings: [
    ["Mode Safety strict", "Mode normal. Les capteurs critiques absents peuvent couper les sorties."],
    ["warning_only", "Les capteurs absents generent un avertissement mais ne coupent pas forcement."],
    ["missing_sensors_off", "Ne bloque pas sur absence capteurs/JSY. Utile pour tests sans capteurs."],
    ["off", "Desactive les securites logiciel. A utiliser uniquement sans charge 230 V."],
    ["AP local toujours actif", "Garde le WiFi de configuration disponible meme si l'ESP32 est connecte a la box."]
  ],
  mqtt: [
    ["Jeedom", "Adresse par defaut du broker MQTT Jeedom : 192.168.0.48, port 1883."],
    ["Publication", "Quand MQTT est active, l'ESP32 publie un JSON global et des topics individuels pour Jeedom."],
    ["Mot de passe", "Le mot de passe MQTT n'est jamais re-affiche. Laisse le champ vide pour conserver la valeur actuelle."],
    ["Reception", "Jeedom peut envoyer une commande JSON sur le topic commande ou une valeur % sur le topic actionneur."],
    ["Topic de base", "Prefixe utilise pour les messages, par exemple routeurSolaire/state ou routeurSolaire/gridPowerW."]
  ]
};

function uid(prefix) {
  return prefix + "_" + Date.now();
}

function options(list, selected) {
  return list.map(function (item) {
    return '<option value="' + esc(item) + '" ' + (String(item) === String(selected) ? "selected" : "") + '>' + esc(item) + '</option>';
  }).join("");
}

function loadDashboardList(name, defaults) {
  try {
    var data = JSON.parse(localStorage.getItem(name) || "null");
    return Array.isArray(data) && data.length ? data : defaults.slice();
  } catch (e) {
    return defaults.slice();
  }
}

function loadGraphConfig() {
  var defaults = {
    enabled: true,
    historySeconds: 60,
    refreshMs: 500,
    smoothingEnabled: true,
    smoothingAlpha: 0.2,
    autoScaleY: true,
    showEnergy: true,
    showTarget: true,
    showDeadband: true,
    showHeaterPower: true,
    showPid: true,
    showTemperatures: true
  };
  try {
    var saved = JSON.parse(localStorage.getItem("dashboardGraphConfig") || "{}");
    Object.keys(defaults).forEach(function (key) {
      if (saved[key] === undefined || saved[key] === null) saved[key] = defaults[key];
    });
    return saved;
  } catch (e) {
    return defaults;
  }
}

function saveGraphConfig() {
  localStorage.setItem("dashboardGraphConfig", JSON.stringify(graphConfig));
}

function checked(value) {
  return value !== false ? "checked" : "";
}

function readNumber(id, fallback) {
  var value = $(id).value;
  return value === "" ? fallback : Number(value);
}

function slugId(value, fallback) {
  var base = String(value || fallback || "item").toLowerCase();
  base = base.normalize ? base.normalize("NFD").replace(/[\u0300-\u036f]/g, "") : base;
  base = base.replace(/[^a-z0-9]+/g, "_").replace(/^_+|_+$/g, "");
  return base || fallback || "item";
}

function uniqueActuatorId(base, currentIndex) {
  var id = slugId(base, "actuator");
  var existing = ((cache.actuators || {}).actuators || []);
  var candidate = id;
  var n = 2;
  while (existing.some(function (a, i) { return i !== currentIndex && a.id === candidate; })) {
    candidate = id + "_" + n++;
  }
  return candidate;
}

function uniqueSensorId(base, currentIndex, group) {
  var id = slugId(base, group === "ds" ? "sonde" : "sensor");
  var existing = group === "ds" ? ((cache.sensors || {}).ds18b20 || []) : ((cache.sensors || {}).sensors || []);
  var candidate = id;
  var n = 2;
  while (existing.some(function (s, i) { return i !== currentIndex && s.id === candidate; })) {
    candidate = id + "_" + n++;
  }
  return candidate;
}

async function api(path, options) {
  var timeout = new Promise(function (_, reject) {
    setTimeout(function () { reject(new Error("Timeout API " + path)); }, 6000);
  });
  var response = await Promise.race([fetch(path, options), timeout]);
  var text = await response.text();
  if (!response.ok) throw new Error("HTTP " + response.status + " " + text);
  try { return JSON.parse(text); } catch (e) { return text; }
}

function postJson(path, data) {
  return fetch(path, {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify(data)});
}

function markDirty(name) {
  dirtyPages[name || page] = true;
}

function clearDirty(name) {
  dirtyPages[name || page] = false;
}

function dirtyNotice(name) {
  return dirtyPages[name || page] ? '<div class="pendingBox">Modifications appliquees dans la page, pense a cliquer sur Sauvegarder.</div>' : "";
}

function flashButton(button) {
  if (!button) return;
  button.classList.add("clicked");
  setTimeout(function () { button.classList.remove("clicked"); }, 450);
}

function card(label, value, unit) {
  return '<div class="card"><div class="label">' + esc(label) + '</div><div class="value">' + esc(fmt(value)) + ' ' + (unit || '') + '</div></div>';
}

function statusClass(ok, warn) {
  if (ok) return "ok";
  if (warn) return "warn";
  return "bad";
}

function pill(label, value, cls) {
  return '<span class="pill ' + esc(cls || "") + '"><b>' + esc(label) + '</b> ' + esc(value) + '</span>';
}

function dashMetric(label, value, unit, cls) {
  return '<div class="dashMetric ' + esc(cls || "") + '"><span>' + esc(label) + '</span><b>' + esc(fmt(value)) + '</b><em>' + esc(unit || "") + '</em></div>';
}

function miniState(label, value, cls, tip) {
  return '<div class="miniState' + (tip ? ' hasTip' : '') + '"><span>' + esc(label) + '</span><b class="' + esc(cls || "") + '">' + esc(value) + '</b>' + (tip ? '<div class="hoverTip">' + tip + '</div>' : '') + '</div>';
}

function bytesHuman(value) {
  value = Number(value) || 0;
  if (value >= 1048576) return Math.round(value / 104857.6) / 10 + " Mo";
  if (value >= 1024) return Math.round(value / 102.4) / 10 + " Ko";
  return value + " o";
}

function uptimeHuman(ms) {
  ms = Number(ms) || 0;
  var total = Math.floor(ms / 1000);
  var days = Math.floor(total / 86400);
  var hours = Math.floor((total % 86400) / 3600);
  var minutes = Math.floor((total % 3600) / 60);
  var seconds = total % 60;
  return (days ? days + " j " : "") + hours + " h " + minutes + " min " + seconds + " s";
}

function stateClass(value) {
  var v = String(value || "").toUpperCase();
  if (v === "OK" || v === "EXCELLENT" || v === "BON") return "ok";
  if (v === "ATTENTION" || v === "MOYEN" || v === "WARNING" || v === "DEGRADED") return "warn";
  if (v === "ERREUR" || v === "CRITICAL" || v === "FAIBLE") return "bad";
  return "muted";
}

function valueMissing(value) {
  return value == null || value === "" || (typeof value === "number" && !isFinite(value));
}

function sensorBadge(available, enabled) {
  if (enabled === false) return '<span class="badge muted">desactive</span>';
  return available ? '<span class="badge ok">OK</span>' : '<span class="badge bad">Erreur</span>';
}

function dsAvailable(index, temp) {
  if (!dsConfigured(index)) return false;
  if (Array.isArray(state.ds18b20Available)) return state.ds18b20Available[index] === true;
  return !valueMissing(temp);
}

function dsConfigured(index) {
  var ds = cache.sensors && Array.isArray(cache.sensors.ds18b20) ? cache.sensors.ds18b20 : null;
  if (ds) return index < ds.length;
  if (Array.isArray(state.ds18b20)) return index < state.ds18b20.length;
  return true;
}

function dsValue(index, temp) {
  if (!dsAvailable(index, temp)) return '<span class="bad">Absent / non lu</span>';
  return '<span class="' + tempClass(temp) + '">' + esc(fmt(temp)) + ' C</span>';
}

function banner() {
  var html = "";
  if (state.safetyTripped) html += '<div class="banner">SECURITE: ' + esc(state.safetyReason) + '</div>';
  if (state.simulationMode) html += '<div class="sim">MODE SIMULATION ACTIF - sorties 230 V forcees OFF - arret auto dans ' + esc(simRemainingText()) + '</div>';
  return html;
}

function simRemainingText() {
  var ms = Number(state.simulationRemainingMs) || 0;
  if (!ms) return "5 min max";
  var s = Math.ceil(ms / 1000);
  var m = Math.floor(s / 60);
  var r = s % 60;
  return m + " min " + (r < 10 ? "0" : "") + r + " s";
}

function helpBox(name) {
  var items = helpTexts[name] || [];
  if (!items.length) return "";
  return '<details class="helpBox"><summary>Aide rapide</summary><div class="helpGrid">' + items.map(function (item) {
    return '<div><b>' + esc(item[0]) + '</b><p>' + esc(item[1]) + '</p></div>';
  }).join("") + '</div></details>';
}

function refreshControls() {
  return '<div class="refreshBox"><span>Rafraichissement Dashboard</span><select id="dashRefresh" onchange="setDashboardRefresh(this.value)">' +
    options(["5000", "10000", "30000", "0"], String(dashboardRefreshMs)) +
    '</select><button onclick="refresh()">Actualiser maintenant</button></div>';
}

function dashboardSeries() {
  var list = [
    {key:"gridPowerW", label:"Puissance reseau", unit:"W", cls:"gridCurve", value:state.gridPowerW},
    {key:"gridPowerRawW", label:"Reseau brut", unit:"W", cls:"gridCurve", value:state.gridPowerRawW},
    {key:"gridPowerFilteredW", label:"Reseau filtre", unit:"W", cls:"surplusCurve", value:state.gridPowerFilteredW},
    {key:"injectionW", label:"Injection", unit:"W", cls:"injectionCurve", value:state.injectionW, min:0},
    {key:"consumptionW", label:"Consommation", unit:"W", cls:"consumptionCurve", value:state.consumptionW, min:0},
    {key:"surplusW", label:"Surplus", unit:"W", cls:"surplusCurve", value:state.surplusW, min:0},
    {key:"targetW", label:"Consigne reseau", unit:"W", cls:"info", value:0},
    {key:"deadbandHighW", label:"Deadband haut", unit:"W", cls:"warn", value:null},
    {key:"deadbandLowW", label:"Deadband bas", unit:"W", cls:"warn", value:null},
    {key:"ssr1PowerPct", label:"SSR1", unit:"%", cls:"heat", value:state.ssr1PowerPct, min:0, max:100},
    {key:"ssr2PowerPct", label:"SSR2", unit:"%", cls:"info", value:state.ssr2PowerPct, min:0, max:100},
    {key:"robotDynPowerPct", label:"RobotDyn", unit:"%", cls:"warn", value:state.robotDynPowerPct, min:0, max:100},
    {key:"pidOutputPercent", label:"PID", unit:"%", cls:"info", value:state.pidOutputPercent, min:0, max:100},
    {key:"heaterPowerW", label:"Chauffe-eau", unit:"W", cls:"heat", value:state.heaterPowerW, min:0},
    {key:"tankTopC", label:"Sonde 1", unit:"C", cls:"tempCurve1", value:dsAvailable(0, state.tankTopC) ? state.tankTopC : null, min:0, max:80},
    {key:"tankMiddleC", label:"Sonde 2", unit:"C", cls:"tempCurve2", value:dsAvailable(1, state.tankMiddleC) ? state.tankMiddleC : null, min:0, max:80},
    {key:"tankBottomC", label:"Sonde 3", unit:"C", cls:"tempCurve3", value:dsAvailable(2, state.tankBottomC) ? state.tankBottomC : null, min:0, max:80},
    {key:"tempSafety", label:"Seuil securite", unit:"C", cls:"bad", value:null, min:0, max:80},
    {key:"heapFree", label:"Heap libre", unit:"o", cls:"ok", value:state.heapFree, min:0}
  ];
  return list.filter(function (item) {
    if (item.key === "tankTopC") return dsConfigured(0);
    if (item.key === "tankMiddleC") return dsConfigured(1);
    if (item.key === "tankBottomC") return dsConfigured(2);
    return true;
  });
}

function seriesByKey(key) {
  var list = dashboardSeries();
  for (var i = 0; i < list.length; i++) if (list[i].key === key) return list[i];
  return list[0];
}

function seriesByKeyOrNull(key) {
  var list = dashboardSeries();
  for (var i = 0; i < list.length; i++) if (list[i].key === key) return list[i];
  return null;
}

function configuredJsySensor() {
  var sensors = cache.sensors && Array.isArray(cache.sensors.sensors) ? cache.sensors.sensors : [];
  for (var i = 0; i < sensors.length; i++) {
    var sensor = sensors[i] || {};
    if ((sensor.id || "") === "jsy_grid" || (sensor.type || "") === "JSY-MK-194T") return sensor;
  }
  return null;
}

function jsyChannel(index) {
  var sensor = configuredJsySensor();
  var channels = sensor && Array.isArray(sensor.channels) ? sensor.channels : defaultJsyChannels();
  return channels[index] || defaultJsyChannels()[index] || {};
}

function jsyChannelName(index) {
  var channel = jsyChannel(index);
  return channel.name || channel.id || ("Pince " + (index + 1));
}

function jsyChannelRole(index) {
  var channel = jsyChannel(index);
  return channel.role || "custom";
}

function seriesOptions(selected) {
  return dashboardSeries().map(function (item) {
    return '<option value="' + esc(item.key) + '" ' + (item.key === selected ? "selected" : "") + '>' + esc(item.label) + '</option>';
  }).join("");
}

function dashboardCustomizeBox() {
  return '<details class="panel dashCustom"><summary>Personnaliser les blocs et courbes</summary>' +
    '<div class="customSection"><h2>Courbes reseau</h2><p>Choisis les mesures affichees dans le graphe energie.</p><div class="curveGrid">' + graphCheckboxes("network", dashboardGraphDefaults.network, dashboardGraphNetwork) + '</div></div>' +
    '<div class="customSection"><h2>Courbes actionneurs</h2><p>Choisis les sorties visibles dans le graphe de routage.</p><div class="curveGrid">' + graphCheckboxes("outputs", dashboardGraphDefaults.outputs, dashboardGraphOutputs) + '</div></div>' +
    '<div class="customSection"><h2>Courbes temperatures</h2><p>Les sondes non configurees sont masquees automatiquement.</p><div class="curveGrid">' + graphCheckboxes("temps", dashboardGraphDefaults.temps, dashboardGraphTemps) + '</div></div>' +
    '<p class="customActions"><button onclick="saveDashboardGraphs()">Appliquer</button> <button onclick="resetDashboardGraphs()">Par defaut</button></p></details>';
}

function graphCheckboxes(group, keys, selected) {
  return keys.map(function (key) {
    var meta = seriesByKeyOrNull(key);
    if (!meta) return "";
    var id = "graph_" + group + "_" + key;
    return '<label class="curveToggle" for="' + esc(id) + '">' +
      '<input id="' + esc(id) + '" type="checkbox" data-graph-group="' + esc(group) + '" value="' + esc(key) + '" ' + (selected.indexOf(key) >= 0 ? "checked" : "") + '>' +
      '<span class="curveDot ' + esc(meta.cls || "") + '"></span><span>' + esc(meta.label) + '</span></label>';
  }).join("");
}

function checkedGraphKeys(group) {
  var nodes = document.querySelectorAll('input[data-graph-group="' + group + '"]');
  return Array.prototype.map.call(nodes, function (node) {
    return node.checked ? node.value : "";
  }).filter(Boolean);
}

function saveDashboardGraphs() {
  dashboardGraphNetwork = checkedGraphKeys("network");
  dashboardGraphOutputs = checkedGraphKeys("outputs");
  dashboardGraphTemps = checkedGraphKeys("temps");
  localStorage.setItem("dashboardGraphNetwork", JSON.stringify(dashboardGraphNetwork));
  localStorage.setItem("dashboardGraphOutputs", JSON.stringify(dashboardGraphOutputs));
  localStorage.setItem("dashboardGraphTemps", JSON.stringify(dashboardGraphTemps));
  render();
}

function resetDashboardGraphs() {
  dashboardGraphNetwork = dashboardGraphDefaults.network.slice();
  dashboardGraphOutputs = dashboardGraphDefaults.outputs.slice();
  dashboardGraphTemps = dashboardGraphDefaults.temps.slice();
  localStorage.setItem("dashboardGraphNetwork", JSON.stringify(dashboardGraphNetwork));
  localStorage.setItem("dashboardGraphOutputs", JSON.stringify(dashboardGraphOutputs));
  localStorage.setItem("dashboardGraphTemps", JSON.stringify(dashboardGraphTemps));
  render();
}

function saveDashboardDisplay() {
  saveDashboardGraphs();
}

function resetDashboardDisplay() {
  resetDashboardGraphs();
}

function realtimeControls() {
  var pauseButton = graphConfig.enabled
    ? '<button class="danger" onclick="pauseGraphs()">Pause graphes</button>'
    : '<button onclick="resumeGraphs()">Reprendre graphes</button>';
  return '<section class="panel realtimePanel"><div class="wideChartHead"><b>Reglages graphes</b><span>' + (graphConfig.enabled ? "actifs" : "desactives") + " - " + esc(graphConfig.historySeconds) + ' s</span></div>' +
    '<div class="graphSettingsGrid">' +
      graphToggle("enabled", "Activer les graphes") +
      graphSelect("historySeconds", "Historique", [["5","5 s"],["10","10 s"],["30","30 s"],["60","60 s"],["300","300 s"]]) +
      graphSelect("refreshMs", "Rafraichissement", [["300","300 ms"],["500","500 ms"],["1000","1000 ms"]]) +
      graphToggle("smoothingEnabled", "Lissage visuel") +
      graphSelect("smoothingAlpha", "Alpha lissage", [["0.1","0.1"],["0.2","0.2"],["0.3","0.3"],["0.5","0.5"]]) +
      graphToggle("autoScaleY", "Auto-echelle Y") +
      graphToggle("showEnergy", "Graphe energie") +
      graphToggle("showTarget", "Consigne") +
      graphToggle("showDeadband", "Deadband") +
      graphToggle("showHeaterPower", "Chauffe-eau") +
      graphToggle("showPid", "Graphe PID") +
      graphToggle("showTemperatures", "Temperatures") +
    '</div>' +
    '<div class="toolbar"><button onclick="applyGraphSettings()">Appliquer</button>' + pauseButton + '<button onclick="clearGraphData()">Vider historique</button></div>' +
    '<small>Les graphes utilisent /api/realtime. Le PID reste independant : le lissage est uniquement visuel.</small></section>';
}

function graphToggle(key, label) {
  return '<label class="curveToggle"><input type="checkbox" data-graph-setting="' + esc(key) + '" ' + (graphConfig[key] ? "checked" : "") + '><span>' + esc(label) + '</span></label>';
}

function graphSelect(key, label, items) {
  var value = String(graphConfig[key]);
  return '<label class="graphSettingLabel"><span>' + esc(label) + '</span><select data-graph-setting="' + esc(key) + '">' +
    items.map(function (item) { return '<option value="' + esc(item[0]) + '" ' + (String(item[0]) === value ? "selected" : "") + '>' + esc(item[1]) + '</option>'; }).join("") +
    '</select></label>';
}

function applyGraphSettings() {
  document.querySelectorAll("[data-graph-setting]").forEach(function (node) {
    var key = node.getAttribute("data-graph-setting");
    if (!key) return;
    if (node.type === "checkbox") graphConfig[key] = node.checked;
    else if (key === "smoothingAlpha") graphConfig[key] = Number(node.value) || 0.2;
    else graphConfig[key] = Number(node.value) || graphConfig[key];
  });
  saveGraphConfig();
  cleanupGraphData();
  restartGraphPolling();
  render();
}

function clearGraphData() {
  graphData = [];
  render();
}

function pauseGraphs() {
  graphConfig.enabled = false;
  saveGraphConfig();
  stopGraphPolling();
  render();
}

function resumeGraphs() {
  graphConfig.enabled = true;
  saveGraphConfig();
  restartGraphPolling();
  render();
}

function restartGraphPolling() {
  stopGraphPolling();
  if (graphConfig.enabled && page === "dashboard") startGraphPolling();
}

function startGraphPolling() {
  if (graphPollTimer || !graphConfig.enabled || page !== "dashboard") return;
  graphPollTimer = setInterval(pollRealtime, Math.max(300, Number(graphConfig.refreshMs) || 500));
  pollRealtime();
}

function stopGraphPolling() {
  if (graphPollTimer) clearInterval(graphPollTimer);
  graphPollTimer = null;
}

async function pollRealtime() {
  if (!graphConfig.enabled || page !== "dashboard") return;
  try {
    var data = await api("/api/realtime");
    handleRealtimeGraphPoint(data);
  } catch (e) {
    // Le dashboard principal reste maitre; on ignore une erreur ponctuelle de polling graphes.
  }
}

function handleRealtimeGraphPoint(data) {
  var now = Date.now();
  var point = {
    t: now,
    gridPowerW: finiteOrNull(data.gridPowerFilteredW != null ? data.gridPowerFilteredW : data.gridPowerW),
    gridPowerRawW: finiteOrNull(data.gridPowerRawW),
    injectionW: finiteOrNull(data.injectionW),
    consumptionW: finiteOrNull(data.consumptionW),
    surplusW: finiteOrNull(data.surplusW),
    targetW: finiteOrNull(data.targetW),
    deadbandW: finiteOrNull(data.deadbandW),
    deadbandHighW: finiteOrNull(Number(data.targetW || 0) + Number(data.deadbandW || 0)),
    deadbandLowW: finiteOrNull(Number(data.targetW || 0) - Number(data.deadbandW || 0)),
    heaterPowerW: finiteOrNull(data.heaterPowerW),
    pidOutputPercent: finiteOrNull(data.pidOutputPercent),
    commandPercent: finiteOrNull(data.commandPercent),
    ssr1PowerPct: finiteOrNull(data.ssr1PowerPct),
    ssr2PowerPct: finiteOrNull(data.ssr2PowerPct),
    robotDynPowerPct: finiteOrNull(data.robotDynPowerPct),
    temp1: finiteOrNull(data.temp1),
    temp2: finiteOrNull(data.temp2),
    temp3: finiteOrNull(data.temp3),
    tempSafety: finiteOrNull(data.tempSafety)
  };
  appendSmoothedFields(point);
  graphData.push(point);
  cleanupGraphData();
  state.gridPowerFilteredW = data.gridPowerFilteredW;
  state.injectionW = data.injectionW;
  state.pidOutputPercent = data.pidOutputPercent;
  state.heaterPowerW = data.heaterPowerW;
  requestChartUpdate();
}

function finiteOrNull(value) {
  var n = Number(value);
  return isFinite(n) ? n : null;
}

function smoothValue(previous, current, alpha) {
  if (current == null || !isFinite(current)) return null;
  if (previous == null || !isFinite(previous)) return current;
  return previous * (1 - alpha) + current * alpha;
}

function appendSmoothedFields(point) {
  var previous = graphData.length ? graphData[graphData.length - 1] : null;
  var alpha = Number(graphConfig.smoothingAlpha) || 0.2;
  ["gridPowerW","heaterPowerW","pidOutputPercent","commandPercent","temp1","temp2","temp3"].forEach(function (key) {
    point[key + "Smooth"] = graphConfig.smoothingEnabled ? smoothValue(previous ? previous[key + "Smooth"] : null, point[key], alpha) : point[key];
  });
}

function cleanupGraphData() {
  var now = Date.now();
  var historyMs = Math.max(5, Number(graphConfig.historySeconds) || 60) * 1000;
  while (graphData.length && now - graphData[0].t > historyMs) graphData.shift();
}

function requestChartUpdate() {
  var now = Date.now();
  if (now - lastGraphDrawMs < Math.max(300, Number(graphConfig.refreshMs) || 500)) return;
  lastGraphDrawMs = now;
  var box = $("dashboardGraphBox");
  if (box) box.innerHTML = dashboardGraphsHtml();
}

function setDashboardRefresh(value) {
  dashboardRefreshMs = Number(value);
  localStorage.setItem("dashboardRefreshMs", String(dashboardRefreshMs));
  scheduleDashboardRefresh();
}

function refreshLabelPatch() {
  var select = $("dashRefresh");
  if (!select) return;
  Array.prototype.forEach.call(select.options, function (option) {
    if (option.value === "5000") option.textContent = "5 s";
    if (option.value === "10000") option.textContent = "10 s";
    if (option.value === "30000") option.textContent = "30 s";
    if (option.value === "0") option.textContent = "pause";
  });
}

function scaleLabel(value, unit) {
  if (value == null || !isFinite(value)) return "-";
  var n = Number(value);
  var text = Math.abs(n) >= 1000 ? Math.round(n).toString() : (Math.round(n * 10) / 10).toString();
  return text + (unit ? " " + unit : "");
}

function chartAxis(min, max, unit) {
  return '<div class="chartScale"></div>';
}

function chartStepForUnit(unit) {
  if (unit === "W") return 50;
  if (unit === "C" || unit === "°C") return 5;
  if (unit === "%") return 10;
  return 0;
}

function chartAxisLabelStep(min, max, unit) {
  if (unit === "C" || unit === "Â°C") return 5;
  if (unit === "%") return 10;
  if (unit === "W") {
    var span = Math.abs(max - min);
    if (span <= 300) return 50;
    if (span <= 800) return 100;
    if (span <= 2000) return 250;
    return 500;
  }
  var raw = Math.abs(max - min) / 5;
  if (raw <= 1) return 1;
  if (raw <= 5) return 5;
  if (raw <= 10) return 10;
  if (raw <= 50) return 50;
  return 100;
}

function chartGrid(min, max, w, h, unit) {
  var step = chartStepForUnit(unit);
  var lines = "";
  var timeStep = getTimeStepSize(graphConfig.historySeconds);
  for (var sx = -graphConfig.historySeconds; sx <= 0; sx += timeStep) {
    var x = Math.round(w + sx * w / graphConfig.historySeconds);
    var xcls = sx === 0 ? "nowLine" : "timeLine";
    lines += '<line class="' + xcls + '" x1="' + x + '" y1="0" x2="' + x + '" y2="' + h + '"></line>';
  }
  if (step > 0 && isFinite(min) && isFinite(max) && max > min) {
    var first = Math.ceil(min / step) * step;
    var count = 0;
    for (var value = first; value <= max + step * 0.001 && count < 140; value += step, count++) {
      var y = Math.round(h - ((value - min) * h / (max - min)));
      var cls = Math.abs(value) < step * 0.001 ? "zeroLine" : "tickLine";
      lines += '<line class="' + cls + '" x1="0" y1="' + y + '" x2="' + w + '" y2="' + y + '"></line>';
    }
    if (min < 0 && max > 0 && first !== 0) {
      var zeroYForced = Math.round(h - ((0 - min) * h / (max - min)));
      lines += '<line class="zeroLine" x1="0" y1="' + zeroYForced + '" x2="' + w + '" y2="' + zeroYForced + '"></line>';
    }
    return lines;
  }
  lines += '<line x1="0" y1="0" x2="' + w + '" y2="0"></line>';
  lines += '<line x1="0" y1="' + Math.round(h / 2) + '" x2="' + w + '" y2="' + Math.round(h / 2) + '"></line>';
  lines += '<line x1="0" y1="' + h + '" x2="' + w + '" y2="' + h + '"></line>';
  if (min < 0 && max > 0) {
    var zeroY = Math.round(h - ((0 - min) * h / (max - min)));
    lines += '<line class="zeroLine" x1="0" y1="' + zeroY + '" x2="' + w + '" y2="' + zeroY + '"></line>';
  }
  return lines;
}

function chartYLabels(min, max, w, h, unit) {
  if (!isFinite(min) || !isFinite(max) || max <= min) return "";
  var step = chartAxisLabelStep(min, max, unit);
  var values = [];
  var first = Math.ceil(min / step) * step;
  var count = 0;
  for (var value = first; value <= max + step * 0.001 && count < 18; value += step, count++) {
    values.push(Math.round(value * 10) / 10);
  }
  if (values.indexOf(Math.round(min * 10) / 10) < 0) values.push(Math.round(min * 10) / 10);
  if (values.indexOf(Math.round(max * 10) / 10) < 0) values.push(Math.round(max * 10) / 10);
  if (min < 0 && max > 0 && values.indexOf(0) < 0) values.push(0);
  values.sort(function (a, b) { return b - a; });
  return '<g class="yAxis">' + values.map(function (value) {
    var y = h - ((value - min) * h / (max - min));
    y = Math.max(0, Math.min(h, y));
    var anchor = y <= 2 ? "start" : (y >= h - 2 ? "end" : "middle");
    var dy = y <= 2 ? "8" : (y >= h - 2 ? "-2" : "3");
    return '<text x="2" y="' + Math.round(y) + '" dy="' + dy + '" dominant-baseline="' + anchor + '">' + esc(scaleLabel(value, unit)) + '</text>';
  }).join("") + '</g>';
}

function chartYLabelsHtml(min, max, unit) {
  if (!isFinite(min) || !isFinite(max) || max <= min) return "";
  var step = chartAxisLabelStep(min, max, unit);
  var values = [];
  var first = Math.ceil(min / step) * step;
  var count = 0;
  for (var value = first; value <= max + step * 0.001 && count < 18; value += step, count++) {
    values.push(Math.round(value * 10) / 10);
  }
  if (values.indexOf(Math.round(min * 10) / 10) < 0) values.push(Math.round(min * 10) / 10);
  if (values.indexOf(Math.round(max * 10) / 10) < 0) values.push(Math.round(max * 10) / 10);
  if (min < 0 && max > 0 && values.indexOf(0) < 0) values.push(0);
  values.sort(function (a, b) { return b - a; });
  return '<div class="axisYLabels">' + values.map(function (value) {
    var y = 100 - ((value - min) * 100 / (max - min));
    y = Math.max(0, Math.min(100, y));
    var cls = value === 0 ? " zeroLabel" : "";
    return '<span class="' + cls + '" style="top:' + y + '%">' + esc(scaleLabel(value, unit)) + '</span>';
  }).join("") + '</div>';
}

function getTimeStepSize(historySeconds) {
  historySeconds = Number(historySeconds) || 60;
  if (historySeconds <= 5) return 1;
  if (historySeconds <= 10) return 2;
  if (historySeconds <= 30) return 5;
  if (historySeconds <= 60) return 10;
  return 60;
}

function relativeTimeSeconds(timestamp, now) {
  return (timestamp - now) / 1000;
}

function graphValue(point, key) {
  if (!point) return null;
  var mapped = {
    tankTopC: "temp1",
    tankMiddleC: "temp2",
    tankBottomC: "temp3"
  }[key] || key;
  if (graphConfig.smoothingEnabled) {
    var smoothKey = mapped + "Smooth";
    if (point[smoothKey] != null && isFinite(point[smoothKey])) return point[smoothKey];
  }
  return point[mapped];
}

function chartHistory() {
  return graphConfig.enabled ? graphData : dashHistory;
}

function chartX(point, now, w) {
  var rel = relativeTimeSeconds(point.t, now);
  return Math.round(w + rel * w / graphConfig.historySeconds);
}

function chartTimeAxis(w, h, offsetX) {
  offsetX = offsetX || 0;
  var step = getTimeStepSize(graphConfig.historySeconds);
  var labels = "";
  for (var s = -graphConfig.historySeconds; s <= 0; s += step) {
    var x = offsetX + Math.round(w + s * w / graphConfig.historySeconds);
    labels += '<text x="' + x + '" y="' + (h + 18) + '">' + s + 's</text>';
  }
  return '<g class="timeAxis">' + labels + '</g>';
}

function chartTimeAxisHtml() {
  var step = getTimeStepSize(graphConfig.historySeconds);
  var labels = "";
  for (var s = -graphConfig.historySeconds; s <= 0; s += step) {
    var x = 100 + s * 100 / graphConfig.historySeconds;
    labels += '<span style="left:' + x + '%">' + s + 's</span>';
  }
  return '<div class="axisXLabels">' + labels + '</div>';
}

function smoothPath(coords) {
  if (!coords.length) return "";
  if (coords.length === 1) return "M" + coords[0].x + " " + coords[0].y;
  var d = "M" + coords[0].x + " " + coords[0].y;
  for (var i = 1; i < coords.length; i++) {
    var prev = coords[i - 1];
    var cur = coords[i];
    var midX = Math.round((prev.x + cur.x) / 2);
    d += " C " + midX + " " + prev.y + ", " + midX + " " + cur.y + ", " + cur.x + " " + cur.y;
  }
  return d;
}

function areaPath(coords, h) {
  if (coords.length < 2) return "";
  var line = smoothPath(coords);
  var first = coords[0];
  var last = coords[coords.length - 1];
  return line + " L " + last.x + " " + h + " L " + first.x + " " + h + " Z";
}

function sparkline(key, cls, minFixed, maxFixed, unit) {
  var history = chartHistory();
  if (!graphConfig.enabled) return '<div class="sparkline muted">graphes desactives</div>';
  if (history.length < 2) return '<div class="sparkline muted">historique en cours...</div>';
  var values = history.map(function (p) { return graphValue(p, key); }).filter(function (v) { return v != null && isFinite(v); });
  if (values.length < 2) return '<div class="sparkline muted">pas de valeur</div>';
  var min = minFixed != null ? minFixed : Math.min.apply(null, values);
  var max = maxFixed != null ? maxFixed : Math.max.apply(null, values);
  if (unit === "W") {
    var marginW = Math.max(50, Math.ceil((max - min) * 0.12 / 50) * 50);
    min = Math.floor((min - marginW) / 50) * 50;
    max = Math.ceil((max + marginW) / 50) * 50;
    if (min > 0) min = 0;
    if (max < 0) max = 0;
  }
  if (unit === "%" && !graphConfig.autoScaleY) { min = 0; max = 100; }
  if (unit === "C") {
    min = 0;
    max = graphConfig.autoScaleY ? Math.min(90, Math.max(25, Math.ceil(max / 5) * 5 + 5)) : 80;
  }
  if (max === min) max = min + 1;
  var w = 360, h = 142;
  var plotX = 0;
  var plotW = w;
  var grid = chartGrid(min, max, w, h, unit);
  var now = Date.now();
  var coords = history.map(function (p) {
    var v = graphValue(p, key);
    if (v == null || !isFinite(v)) return null;
    var x = plotX + chartX(p, now, plotW);
    var y = h - ((v - min) * h / (max - min));
    if (y < 0 || y > h) return null;
    return {x:Math.round(x), y:Math.round(y)};
  }).filter(Boolean);
  if (coords.length < 2) return '<div class="sparkline muted">pas de valeur</div>';
  var last = coords[coords.length - 1];
  return '<div class="chartWithScale" data-chart-key="' + esc(key) + '" data-chart-unit="' + esc(unit || "") + '">' + chartAxis(min, max, unit) + chartYLabelsHtml(min, max, unit) + '<div class="chartPlot"><svg class="sparkline ' + esc(cls || "") + '" viewBox="0 0 ' + w + ' ' + h + '" preserveAspectRatio="none"><g class="grid">' + chartGrid(min, max, plotW, h, unit) + '</g><path class="chartFill" d="' + areaPath(coords, h) + '"></path><path class="chartLine" d="' + smoothPath(coords) + '"></path><circle class="lastPoint" cx="' + last.x + '" cy="' + last.y + '" r="2"></circle></svg>' + chartTimeAxisHtml() + '</div></div>';
}

function multiSparkline(series, unit) {
  var history = chartHistory();
  if (!graphConfig.enabled) return '<div class="sparkline energySpark muted">graphes desactives</div>';
  if (history.length < 2) return '<div class="sparkline energySpark muted">historique en cours...</div>';
  var all = [];
  series.forEach(function (s) {
    if (unit === "C" && s.key === "tempSafety") return;
    history.forEach(function (p) {
      var v = graphValue(p, s.key);
      if (v != null && isFinite(v)) all.push(Number(v));
    });
  });
  if (all.length < 2) return '<div class="sparkline energySpark muted">pas de valeur</div>';
  var min = Math.min.apply(null, all);
  var max = Math.max.apply(null, all);
  if (unit === "W") {
    var marginW = Math.max(50, Math.ceil((max - min) * 0.12 / 50) * 50);
    min = Math.floor((min - marginW) / 50) * 50;
    max = Math.ceil((max + marginW) / 50) * 50;
    if (min > 0) min = 0;
    if (max < 0) max = 0;
  } else if (min > 0 && unit !== "C") min = 0;
  if (unit === "%" && !graphConfig.autoScaleY) { min = 0; max = 100; }
  if (unit === "C") {
    min = 0;
    max = graphConfig.autoScaleY ? Math.min(90, Math.max(25, Math.ceil(max / 5) * 5 + 5)) : 80;
  }
  if (max === min) max = min + 1;
  var w = 360, h = 142;
  var plotX = 0;
  var plotW = w;
  var now = Date.now();
  var lines = series.map(function (s) {
    var coords = history.map(function (p) {
      var v = graphValue(p, s.key);
      if (v == null || !isFinite(v)) return null;
      var x = plotX + chartX(p, now, plotW);
      var y = h - ((v - min) * h / (max - min));
      if (y < 0 || y > h) return null;
      return {x:Math.round(x), y:Math.round(y)};
    }).filter(Boolean);
    if (coords.length < 2) return "";
    var last = coords[coords.length - 1];
    return '<path class="chartLine ' + esc(s.cls) + '" d="' + smoothPath(coords) + '"></path><circle class="lastPoint ' + esc(s.cls) + '" cx="' + last.x + '" cy="' + last.y + '" r="2"></circle>';
  }).join("");
  var keys = series.map(function (s) { return s.key; }).join(",");
  var labels = series.map(function (s) { return s.label; }).join(",");
  var classes = series.map(function (s) { return s.cls || ""; }).join(",");
  return '<div class="chartWithScale energyWithScale" data-chart-series="' + esc(keys) + '" data-chart-labels="' + esc(labels) + '" data-chart-classes="' + esc(classes) + '" data-chart-unit="' + esc(unit || "") + '">' + chartAxis(min, max, unit) + chartYLabelsHtml(min, max, unit) + '<div class="chartPlot"><svg class="sparkline energySpark" viewBox="0 0 ' + w + ' ' + h + '" preserveAspectRatio="none"><g class="grid">' + chartGrid(min, max, plotW, h, unit) + '</g>' + lines + '</svg>' + chartTimeAxisHtml() + '</div></div>';
}

function energyGraphCard() {
  var series = [
    {key:"gridPowerW", label:"Reseau", cls:"consume"},
    {key:"injectionW", label:"Injection", cls:"solar"},
    {key:"surplusW", label:"Surplus", cls:"sun"}
  ];
  return '<section class="energyGraphCard"><div class="energyHead">' +
    dashMetric("Reseau", state.gridPowerW, "W", Number(state.gridPowerW) < 0 ? "solar" : "consume") +
    dashMetric("Injection", state.injectionW, "W", "solar") +
    dashMetric("Surplus", state.surplusW, "W", "sun") +
    '</div>' + multiSparkline(series, "W") +
    '<div class="legend">' + series.map(function (s) { return '<span class="' + esc(s.cls) + '">' + esc(s.label) + '</span>'; }).join("") + '</div>' +
    '<small>Source reseau: ' + esc(state.gridPowerSource || "JSY") + ' - 30 min - 1 point / 5 s</small></section>';
}

function metricCardFor(key) {
  var s = seriesByKey(key);
  return dashMetric(s.label, s.value, s.unit, s.cls);
}

function chartCardFor(key) {
  var s = seriesByKey(key);
  return chartCard(s.label, s.key, s.unit, s.cls, s.min, s.max);
}

function chartCard(label, key, unit, cls, minFixed, maxFixed) {
  var last = dashHistory.length ? dashHistory[dashHistory.length - 1][key] : null;
  return '<div class="chartCard"><div><span>' + esc(label) + '</span><b class="' + esc(cls || "") + '">' + esc(fmt(last)) + ' ' + esc(unit || "") + '</b></div>' + sparkline(key, cls, minFixed, maxFixed, unit) + '<small>30 min - 1 point / 5 s</small></div>';
}

function dashboardTitle(text) {
  return '<div class="dashSectionTitle"><h2>' + esc(text) + '</h2></div>';
}

function dashboardCard(icon, label, value, unit, cls, detail) {
  return '<div class="yasCard ' + esc(cls || "") + '"><span class="roundIcon">' + esc(icon || "i") + '</span><div><b>' + esc(label) + '</b><strong>' + esc(fmt(value)) + (unit ? ' <em>' + esc(unit) + '</em>' : '') + '</strong>' + (detail ? '<small>' + esc(detail) + '</small>' : '') + '</div></div>';
}

function dashboardControl(icon, label, value, cls, detail) {
  return '<div class="controlCard ' + esc(cls || "") + '"><span class="roundIcon">' + esc(icon || "o") + '</span><div><b>' + esc(label) + '</b><strong>' + esc(value) + '</strong>' + (detail ? '<small>' + esc(detail) + '</small>' : '') + '</div></div>';
}

function dashboardBlock(title, subtitle, body, cls) {
  return '<section class="dashBlockPanel ' + esc(cls || "") + '"><div class="dashBlockHead"><div><h2>' + esc(title) + '</h2>' + (subtitle ? '<span>' + esc(subtitle) + '</span>' : '') + '</div></div>' + body + '</section>';
}

function blockMetric(label, value, unit, cls, detail) {
  return '<div class="blockMetric ' + esc(cls || "") + '"><span>' + esc(label) + '</span><b>' + esc(fmt(value)) + (unit ? ' <em>' + esc(unit) + '</em>' : '') + '</b>' + (detail ? '<small>' + esc(detail) + '</small>' : '') + '</div>';
}

function hasValue(value) {
  if (value == null || value === "") return false;
  if (typeof value === "number") return isFinite(value);
  if (typeof value === "string") {
    var text = value.trim();
    return text.length > 0 && text !== "-" && text !== "N/A" && text !== "unknown";
  }
  return true;
}

function blockMetricIf(label, value, unit, cls, detail) {
  return hasValue(value) ? blockMetric(label, value, unit, cls, detail) : "";
}

function networkEnergyMetrics() {
  var source = String(state.gridPowerSource || "JSY").toUpperCase();
  var cfgPf = Number(((cache.system || {}).router || {}).linkyPowerFactorEstimate);
  if (!isFinite(cfgPf) || cfgPf <= 0 || cfgPf > 1) cfgPf = hasValue(state.gridPowerFactor) ? Number(state.gridPowerFactor) : 0.95;
  var linkyVA = Number(state.ticApparentPowerVA);
  var linkyA = Number(state.ticCurrentA);
  var linkyActiveEstimate = isFinite(linkyVA) ? linkyVA * cfgPf : null;
  var linkyVoltageEstimate = null;
  var linkyVoltageDetail = "P / (A x PF)";
  var linkyVoltageClass = "info";
  if (isFinite(linkyActiveEstimate) && isFinite(linkyA) && isFinite(cfgPf) && linkyA > 0.5 && cfgPf > 0.05) {
    linkyVoltageEstimate = linkyActiveEstimate / (linkyA * cfgPf);
  } else if (isFinite(linkyVA) && isFinite(linkyA) && linkyA > 0.5) {
    linkyVoltageEstimate = linkyVA / linkyA;
    linkyVoltageDetail = "VA / A";
  }
  if (isFinite(linkyVoltageEstimate) && (linkyVoltageEstimate < 180 || linkyVoltageEstimate > 260)) {
    linkyVoltageClass = "warn";
    linkyVoltageDetail += " - hors plage";
  }
  var html = "";
  html += blockMetric("Puissance reseau", state.gridPowerW, "W", Number(state.gridPowerW) < 0 ? "solar" : "consume", Number(state.gridPowerW) < 0 ? "injection" : "consommation");
  html += blockMetricIf("Injection", state.injectionW, "W", "solar", "export reseau");
  html += blockMetricIf("Surplus", state.surplusW, "W", "sun", "disponible routeur");

  html += blockMetricIf("Tension", state.gridVoltageV, "V", "info", source.indexOf("JSY") >= 0 ? "JSY" : "");
  html += blockMetricIf("Courant", hasValue(state.gridCurrentA) ? state.gridCurrentA : state.ticCurrentA, "A", "sun", hasValue(state.gridCurrentA) ? "JSY" : "Linky");
  html += blockMetricIf("Frequence", state.gridFrequencyHz, "Hz", "muted", "JSY");
  html += blockMetricIf("Facteur puissance", state.gridPowerFactor, "", "info", "JSY");
  html += blockMetricIf("Direction", state.gridEnergyDirection, "", "muted", "JSY");

  html += blockMetricIf("P. apparente", state.ticApparentPowerVA, "VA", "info", "Linky");
  html += blockMetricIf("Puissance Linky", state.ticGridPowerW, "W", "info", "source possible");
  html += blockMetricIf("P. active estimee", linkyActiveEstimate, "W", "solar", "VA x PF " + fmt(cfgPf));
  if (isFinite(linkyVoltageEstimate)) html += blockMetric("Tension estimee", linkyVoltageEstimate, "V", linkyVoltageClass, linkyVoltageDetail);
  else if (source.indexOf("TIC") >= 0 || state.ticAvailable) html += blockMetric("Tension estimee", "N/A", "", "warn", "courant ou puissance Linky absent");
  html += blockMetricIf("PF estime", hasValue(state.gridPowerFactor) ? state.gridPowerFactor : cfgPf, "", "muted", hasValue(state.gridPowerFactor) ? "JSY" : "config Linky");
  html += blockMetricIf("Tarif", state.ticTariff, "", "muted", "Linky");
  html += blockMetricIf("Periode", state.ticPeriod, "", "muted", "Linky");
  html += blockMetricIf("Index", Number(state.ticEnergyWh) > 0 ? state.ticEnergyWh : null, "Wh", "muted", "Linky");
  return html;
}

function pidCalculationMetrics() {
  var measured = hasValue(state.pidMeasuredW) ? state.pidMeasuredW : (hasValue(state.gridPowerFilteredW) ? state.gridPowerFilteredW : state.gridPowerW);
  var setpoint = hasValue(state.gridSetpointW) ? state.gridSetpointW : 0;
  var error = hasValue(state.pidErrorW) ? state.pidErrorW : Number(setpoint) - Number(measured || 0);
  var deadband = hasValue(state.deadbandW) ? state.deadbandW : 30;
  var output = hasValue(state.pidOutputPercent) ? state.pidOutputPercent : 0;
  var command = hasValue(state.commandPercent) ? state.commandPercent : output;
  var heater = hasValue(state.heaterPowerW) ? state.heaterPowerW : 0;
  var statusCls = state.pidStatus === "INJECTION" ? "solar" : (state.pidStatus === "CONSUMPTION" ? "consume" : (state.pidStatus === "SAFETY" ? "bad" : "info"));
  return blockMetric("Etat PID", state.pidStatus || "IDLE", "", statusCls, state.pidEnabled === false ? "PID desactive" : "PID actif") +
    blockMetric("Mesure filtree", measured, "W", Number(measured) < 0 ? "solar" : "consume", "entree PID") +
    blockMetric("Consigne", setpoint, "W", "info", "objectif reseau") +
    blockMetric("Erreur", error, "W", Number(error) > 0 ? "solar" : "consume", "consigne - mesure") +
    blockMetric("Deadband", deadband, "W", "muted", "zone neutre") +
    blockMetric("Sortie PID", output, "%", Number(output) > 0 ? "ok" : "muted", "calcul") +
    blockMetric("Commande finale", command, "%", Number(command) > 0 ? "ok" : "muted", "apres limites") +
    blockMetric("Puissance estimee", heater, "W", "heat", "chauffe-eau") +
    blockMetric("Kp / Ki / Kd", fmt(state.pidKp) + " / " + fmt(state.pidKi) + " / " + fmt(state.pidKd), "", "muted", "coefficients") +
    blockMetric("Rampe", state.maxOutputRampPercentPerSecond, "%/s", "muted", "limite variation");
}

function blockState(label, value, cls) {
  return '<div class="blockState"><span>' + esc(label) + '</span><b class="' + esc(cls || "") + '">' + esc(value) + '</b></div>';
}

function statusMini(label, value) {
  var cls = stateClass(value);
  return '<div class="statusMini"><span>' + esc(label) + '</span><b class="' + esc(cls) + '">' + esc(value || "N/A") + '</b></div>';
}

function statusBadge(label, value, cls, detail) {
  return '<div class="statusBadge ' + esc(cls || "") + '"><span>' + esc(label) + '</span><b>' + esc(value) + '</b>' + (detail ? '<small>' + esc(detail) + '</small>' : '') + '</div>';
}

function wideChartCard(label, key, unit, cls, minFixed, maxFixed) {
  var last = dashHistory.length ? dashHistory[dashHistory.length - 1][key] : null;
  return '<section class="wideChart"><div class="wideChartHead"><b>' + esc(label) + '</b><span class="' + esc(cls || "") + '">' + esc(fmt(last)) + ' ' + esc(unit || "") + '</span></div>' + sparkline(key, cls, minFixed, maxFixed, unit) + '<small>30 min - 1 point / 5 s</small></section>';
}

function wideMultiChartCard(label, series, unit) {
  return '<section class="wideChart"><div class="wideChartHead"><b>' + esc(label) + '</b><span>' + series.map(function (s) { return esc(s.label); }).join(" / ") + '</span></div>' + multiSparkline(series, unit) + '<small>30 min - 1 point / 5 s</small></section>';
}

function selectedGraphSeries(keys) {
  return keys.map(function (key) {
    var meta = seriesByKeyOrNull(key);
    return meta ? {key:meta.key, label:meta.label, cls:meta.cls, unit:meta.unit, min:meta.min, max:meta.max} : null;
  }).filter(Boolean);
}

function graphCardFromSeries(title, keys, unit) {
  var series = selectedGraphSeries(keys);
  if (!series.length) return "";
  if (series.length === 1) {
    var s = series[0];
    return wideChartCard(title, s.key, s.unit || unit, s.cls, s.min, s.max);
  }
  return wideMultiChartCard(title, series, unit);
}

function dashboardGraphsHtml() {
  if (!graphConfig.enabled) return '<section class="wideChart"><div class="wideChartHead"><b>Graphiques</b><span>desactives</span></div><div class="sparkline muted">active les graphes dans Reglages graphes</div></section>';
  var html = "";
  var networkKeys = dashboardGraphNetwork.slice();
  if (graphConfig.showTarget && networkKeys.indexOf("targetW") < 0) networkKeys.push("targetW");
  if (graphConfig.showDeadband) {
    if (networkKeys.indexOf("deadbandHighW") < 0) networkKeys.push("deadbandHighW");
    if (networkKeys.indexOf("deadbandLowW") < 0) networkKeys.push("deadbandLowW");
  }
  if (graphConfig.showHeaterPower && networkKeys.indexOf("heaterPowerW") < 0) networkKeys.push("heaterPowerW");
  if (graphConfig.showEnergy) html += graphCardFromSeries("Energie reseau - puissance W", networkKeys, "W");
  if (graphConfig.showPid) html += graphCardFromSeries("Regulation / commandes - %", dashboardGraphOutputs.concat(dashboardGraphOutputs.indexOf("pidOutputPercent") < 0 ? ["pidOutputPercent"] : []), "%");
  if (graphConfig.showTemperatures) html += graphCardFromSeries("Temperatures DS18B20 - C", dashboardGraphTemps.concat(["tempSafety"]), "C");
  return html || '<section class="wideChart"><div class="wideChartHead"><b>Graphiques</b><span>aucune courbe active</span></div><div class="sparkline muted">active au moins une courbe dans la personnalisation</div></section>';
}

function configuredDsCards() {
  var labels = ["Sonde 1", "Sonde 2", "Sonde 3"];
  var temps = [state.tankTopC, state.tankMiddleC, state.tankBottomC];
  return [0, 1, 2].map(function (index) {
    if (!dsConfigured(index)) return "";
    var live = dsAvailable(index, temps[index]);
    return dashboardCard("T", labels[index], live ? temps[index] : "absent", live ? "C" : "", live ? tempClass(temps[index]) : "bad", live ? "DS18B20 OK" : "non lu");
  }).join("");
}

function configuredDsMetrics() {
  var labels = ["Sonde 1", "Sonde 2", "Sonde 3"];
  var temps = [state.tankTopC, state.tankMiddleC, state.tankBottomC];
  var html = [0, 1, 2].map(function (index) {
    if (!dsConfigured(index)) return "";
    var live = dsAvailable(index, temps[index]);
    return blockMetric(labels[index], live ? temps[index] : "absent", live ? "C" : "", live ? tempClass(temps[index]) : "bad", live ? "DS18B20 OK" : "non lu");
  }).join("");
  return html || blockMetric("DS18B20", "aucune", "", "muted", "pas de sonde configuree");
}

function ensureChartTooltip() {
  var tip = $("chartTooltip");
  if (!tip) {
    tip = document.createElement("div");
    tip.id = "chartTooltip";
    tip.className = "chartTooltip";
    document.body.appendChild(tip);
  }
  return tip;
}

function historyIndexFromMouse(event, svg) {
  var history = chartHistory();
  if (!history.length) return -1;
  var rect = svg.getBoundingClientRect();
  var x = Math.max(0, Math.min(rect.width, event.clientX - rect.left));
  var ratio = rect.width ? x / rect.width : 0;
  var targetRel = -graphConfig.historySeconds + ratio * graphConfig.historySeconds;
  var now = Date.now();
  var best = 0;
  var bestDistance = Infinity;
  history.forEach(function (point, index) {
    var d = Math.abs(relativeTimeSeconds(point.t, now) - targetRel);
    if (d < bestDistance) {
      bestDistance = d;
      best = index;
    }
  });
  return best;
}

function historyTimeLabel(point) {
  if (!point || !point.t) return "";
  var rel = relativeTimeSeconds(point.t, Date.now());
  if (graphConfig.enabled) return (Math.round(rel * 10) / 10) + " s";
  var d = new Date(point.t);
  return d.toLocaleTimeString([], {hour:"2-digit", minute:"2-digit", second:"2-digit"});
}

function tooltipLine(label, value, unit, cls) {
  return '<span class="tipLine"><i class="' + esc(cls || "muted") + '"></i><em>' + esc(label) + '</em><strong>' + esc(fmt(value)) + ' ' + esc(unit || "") + '</strong></span>';
}

function ensureSvgLine(svg, className) {
  var line = svg.querySelector("line." + className);
  if (line) return line;
  line = document.createElementNS("http://www.w3.org/2000/svg", "line");
  line.setAttribute("class", className + " chartHoverGuide");
  svg.appendChild(line);
  return line;
}

function updateChartGuide(event, svg, index) {
  var rect = svg.getBoundingClientRect();
  var history = chartHistory();
  if (!rect.width || !rect.height || !history.length) return;
  var viewBox = (svg.getAttribute("viewBox") || "0 0 360 166").split(/\s+/).map(Number);
  var w = viewBox[2] || 320;
  var h = Math.max(1, (viewBox[3] || 166) - 24);
  var x = chartX(history[index], Date.now(), w);
  var y = Math.max(0, Math.min(h, (event.clientY - rect.top) * h / rect.height));
  var vertical = ensureSvgLine(svg, "chartHoverX");
  var horizontal = ensureSvgLine(svg, "chartHoverY");
  vertical.setAttribute("x1", x);
  vertical.setAttribute("x2", x);
  vertical.setAttribute("y1", 0);
  vertical.setAttribute("y2", h);
  horizontal.setAttribute("x1", 0);
  horizontal.setAttribute("x2", w);
  horizontal.setAttribute("y1", y);
  horizontal.setAttribute("y2", y);
}

function showChartTooltip(event) {
  var box = event.target.closest ? event.target.closest(".chartWithScale") : null;
  var history = chartHistory();
  if (!box || !history.length) {
    hideChartTooltip();
    return;
  }
  var svg = box.querySelector("svg");
  if (!svg) return;
  var index = historyIndexFromMouse(event, svg);
  var point = history[index];
  if (!point) return;
  updateChartGuide(event, svg, index);
  var unit = box.getAttribute("data-chart-unit") || "";
  var html = '<b>' + esc(historyTimeLabel(point)) + '</b>';
  var series = box.getAttribute("data-chart-series");
  if (series) {
    var keys = series.split(",");
    var labels = (box.getAttribute("data-chart-labels") || series).split(",");
    var classes = (box.getAttribute("data-chart-classes") || "").split(",");
    html += keys.map(function (key, i) {
      return tooltipLine(labels[i] || key, graphValue(point, key), unit, classes[i] || "");
    }).join("");
  } else {
    var key = box.getAttribute("data-chart-key");
    var meta = seriesByKey(key);
    html += tooltipLine(meta.label, graphValue(point, key), unit || meta.unit || "", meta.cls);
  }
  var tip = ensureChartTooltip();
  tip.innerHTML = html;
  tip.style.left = Math.min(window.innerWidth - 170, event.clientX + 14) + "px";
  tip.style.top = Math.max(8, event.clientY - 18) + "px";
  tip.classList.add("visible");
}

function hideChartTooltip() {
  var tip = $("chartTooltip");
  if (tip) tip.classList.remove("visible");
  document.querySelectorAll(".chartHoverGuide").forEach(function (line) {
    line.remove();
  });
}

function updateNavStatus() {
  var box = $("navStatus");
  if (!box) return;
  var cls = state.safetyTripped ? "bad" : (state.simulationMode ? "warn" : (state.wifiConnected || state.networkMode ? "ok" : "muted"));
  var label = state.safetyTripped ? "Safety CRITICAL" : (state.simulationMode ? "Simulation" : (state.wifiConnected ? "Connected" : (state.networkMode || "Local")));
  box.className = "navStatus " + cls;
  box.innerHTML = "<i></i><span>" + esc(label) + "</span>";
}

async function loadStatus() {
  state = await api(page === "diagnostic" ? "/api/diagnostic" : "/api/status-lite");
}

async function refresh() {
  try {
    await loadStatus();
    if ((page === "dashboard" || page === "diagnostic") && !cache.sensors) {
      try {
        cache.sensors = await api("/api/sensors");
      } catch (sensorError) {
        // Le Dashboard doit rester utilisable meme si la config capteurs est
        // temporairement indisponible ou trop lente a charger.
        cache.sensors = {sensors:[], ds18b20:[]};
        state.lastWebWarning = sensorError.message || "Capteurs indisponibles";
      }
    }
    recordDashboardHistory();
    render();
  } catch (error) {
    $("app").innerHTML = '<h1>Erreur API</h1><div class="banner">' + esc(error.message) + '</div><p><a href="/">Retour secours</a> <a href="/api/status-lite">Tester status-lite</a> <a href="/lite">Interface Lite embarquee</a></p>';
  }
}

function recordDashboardHistory() {
  var now = Date.now();
  if (dashHistory.length && now - lastHistorySampleMs < historySampleIntervalMs) return;
  lastHistorySampleMs = now;
  dashHistory.push({
    t: now,
    gridPowerW: Number(state.gridPowerW) || 0,
    injectionW: Number(state.injectionW) || 0,
    consumptionW: Number(state.consumptionW) || 0,
    surplusW: Number(state.surplusW) || 0,
    ssr1PowerPct: Number(state.ssr1PowerPct) || 0,
    ssr2PowerPct: Number(state.ssr2PowerPct) || 0,
    robotDynPowerPct: Number(state.robotDynPowerPct) || 0,
    tankTopC: dsAvailable(0, state.tankTopC) ? Number(state.tankTopC) : null,
    tankMiddleC: dsAvailable(1, state.tankMiddleC) ? Number(state.tankMiddleC) : null,
    tankBottomC: dsAvailable(2, state.tankBottomC) ? Number(state.tankBottomC) : null,
    heapFree: Number(state.heapFree) || 0
  });
  while (dashHistory.length > historyMaxPoints) dashHistory.shift();
}

function dashboard() {
  if (graphConfig.enabled) startGraphPolling();
  else stopGraphPolling();
  var wifiCls = statusClass(state.wifiConnected, state.networkMode === "AP" || state.networkMode === "AP_STA");
  var safetyCls = state.safetyTripped || state.safetyLevel === "CRITICAL" ? "bad" : (state.safetyLevel === "WARNING" || state.safetyLevel === "DEGRADED" ? "warn" : "ok");
  var ssid = state.wifiSsid || "non renseigne";
  var ip = state.stationIp || state.localIp || "-";
  var moduleName = state.moduleName || "Routeur solaire";

  var overviewBlock = dashboardBlock("Etat general", "module " + (state.role || "-"),
    '<div class="blockStates">' +
      blockState("Safety", state.safetyLevel || "OK", safetyCls) +
      blockState("WiFi", state.wifiConnected ? "connecte" : "AP local", wifiCls) +
      blockState("Simulation", state.simulationMode ? "active" : "off", state.simulationMode ? "warn" : "muted") +
      blockState("IP", ip, "info") +
    '</div>' +
    '<div class="blockNote">' + esc(state.safetyReason || "Aucun defaut logiciel actif.") + '</div>', safetyCls);

  var energyBlock = dashboardBlock("Energie reseau", "source " + (state.gridPowerSource || "JSY"),
    '<div class="blockMetricGrid">' +
      networkEnergyMetrics() +
    '</div>', "energy");

  var routingBlock = dashboardBlock("Routage", "commandes actionneurs",
    '<div class="blockMetricGrid three">' +
      blockMetric("SSR1", state.ssr1PowerPct, "%", Number(state.ssr1PowerPct) > 0 ? "ok" : "muted", "chauffe-eau") +
      blockMetric("SSR2", state.ssr2PowerPct, "%", Number(state.ssr2PowerPct) > 0 ? "ok" : "muted", "auxiliaire") +
      blockMetric("RobotDyn", state.robotDynPowerPct, "%", Number(state.robotDynPowerPct) > 0 ? "warn" : "muted", "triac") +
    '</div>', "routing");

  var pidBlock = dashboardBlock("Calcul PID", "regulation chauffe-eau",
    '<div class="blockMetricGrid">' + pidCalculationMetrics() + '</div>', "pid");

  var sensorBlock = dashboardBlock("Capteurs", "etat des mesures",
    '<div class="blockMetricGrid">' +
      blockMetric("JSY-MK-194T", state.jsyOnline ? "OK" : "absent", "", state.jsyOnline ? "ok" : "bad", jsyChannelName(0) + " (" + jsyChannelRole(0) + ") " + fmt(state.activePowerW1) + " W - " + fmt(state.voltageV1) + " V / " + jsyChannelName(1) + " (" + jsyChannelRole(1) + ") " + fmt(state.activePowerW2) + " W - " + fmt(state.voltageV2) + " V") +
      blockMetric("TIC Linky", state.ticAvailable ? "OK" : "absent", "", state.ticAvailable ? "ok" : "warn", state.ticStatus || "diagnostic") +
      configuredDsMetrics() +
    '</div>', "sensors");

  return banner() +
    '<header class="yasTopbar"><div><span>Dashboard</span><h1>' + esc(moduleName) + '</h1></div><div class="statusBadges">' +
      statusBadge("Role", state.role || "-", "info", "firmware commun") +
      statusBadge("WiFi", state.wifiConnected ? "connecte" : "AP local", wifiCls, ssid) +
      statusBadge("Safety", state.safetyLevel || "OK", safetyCls, state.safetyReason || "aucun defaut") +
      statusBadge("Simulation", state.simulationMode ? "active" : "off", state.simulationMode ? "warn" : "muted", state.simulationMode ? simRemainingText() : "reel") +
    '</div></header>' +
    '<section class="updateStrip"><div>Routeur solaire local - ' + esc(state.gridPowerSource || "JSY") + ' comme source reseau</div><button onclick="refresh()">Actualiser</button></section>' +
    helpBox("dashboard") + dashboardCustomizeBox() +
    '<section class="dashBlockGrid">' + overviewBlock + energyBlock + routingBlock + pidBlock + sensorBlock + '</section>' +
    realtimeControls() +
    dashboardTitle("Graphiques") +
    '<section id="dashboardGraphBox" class="wideCharts">' +
      dashboardGraphsHtml() +
    '</section>';
}

function tempClass(value) {
  value = Number(value);
  if (!isFinite(value)) return "bad";
  if (value >= 70) return "bad";
  if (value >= 60) return "warn";
  return "ok";
}

function actuatorBar(label, value) {
  var pct = Math.max(0, Math.min(100, Number(value) || 0));
  return '<div class="barLine"><div><span>' + esc(label) + '</span><b>' + esc(fmt(pct)) + ' %</b></div><div class="bar"><i style="width:' + pct + '%"></i></div></div>';
}

async function sensorsPage() {
  cache.sensors = await api("/api/sensors");
  drawSensorsPage();
}

function drawSensorsPage() {
  var sensors = cache.sensors.sensors || [];
  var ds = cache.sensors.ds18b20 || [];
  var rows = "";
  sensors.forEach(function (s, i) {
    ensureJsyChannels(s);
    var live = genericSensorState(s);
    rows += '<tr class="' + esc(live.cls) + '"><td>' + esc(s.name || s.id) + '</td><td>' + esc(s.type) + '</td><td>' + esc(sensorRoleText(s)) + '</td><td>' + esc(pinText(s)) + '</td><td><span class="badge ' + esc(live.cls) + '">' + esc(live.text) + '</span> <span class="muted">' + esc(live.detail) + '</span>' + sensorExtraValue(s) + '</td><td class="actions"><button onclick="editSensor(' + i + ')">Modifier</button><button onclick="toggleSensor(' + i + ')">' + (s.enabled !== false ? "Desactiver" : "Activer") + '</button><button class="danger" onclick="deleteSensor(' + i + ')">Supprimer</button></td></tr>';
  });
  ds.forEach(function (s, i) {
    var temp = [state.tankTopC, state.tankMiddleC, state.tankBottomC][i];
    var available = dsAvailable(i, temp);
    var cls = s.enabled === false ? "muted" : (available ? "ok" : (s.critical ? "bad" : "warn"));
    var statusText = s.enabled === false ? "desactive" : (available ? "OK" : (s.critical ? "critique absent" : "absent"));
    rows += '<tr class="' + esc(cls) + '"><td>' + esc(s.name || s.id) + '</td><td>DS18B20</td><td>' + esc(s.role) + '</td><td>GPIO ' + esc((cache.sensors.oneWireBus || {}).gpio || 13) + '</td><td><span class="badge ' + esc(cls) + '">' + esc(statusText) + '</span> ' + dsValue(i, temp) + '</td><td class="actions"><button onclick="editDsSensor(' + i + ')">Modifier</button><button onclick="toggleDsSensor(' + i + ')">' + (s.enabled !== false ? "Desactiver" : "Activer") + '</button><button class="danger" onclick="deleteDsSensor(' + i + ')">Supprimer</button></td></tr>';
  });
  $("app").innerHTML = banner() + '<h1>Capteurs</h1>' + helpBox("sensors") + dirtyNotice("sensors") + '<div class="toolbar"><button onclick="newSensor()">Ajouter capteur</button><button onclick="newJsySensor()">Ajouter JSY 2 pinces</button><button onclick="newDsSensor()">Ajouter DS18B20</button><button onclick="saveSensors()">Sauvegarder</button><button onclick="scanDs()">Scanner DS18B20</button><button onclick="jsonEditor(\'sensors\')">JSON avance</button></div>' + oneWireBusPanel() + '<section class="panel" id="sensorForm">Selectionne un capteur ou ajoute-en un nouveau.</section><table><tr><th>Nom</th><th>Type</th><th>Role</th><th>Bus/GPIO</th><th>Etat/Valeur</th><th>Actions</th></tr>' + rows + '</table><pre id="scan"></pre>';
}

function oneWireBusPanel() {
  var bus = cache.sensors.oneWireBus || {};
  var gpio = bus.gpio == null ? 13 : Number(bus.gpio);
  var gpioChoices = [4, 5, 13, 14, 15, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33];
  if (gpioChoices.indexOf(gpio) < 0) gpioChoices.push(gpio);
  gpioChoices.sort(function (a, b) { return a - b; });
  return '<section class="panel oneWirePanel"><h2>Bus OneWire DS18B20</h2><div class="form">' +
    '<label>GPIO des sondes<select id="oneWireGpio">' + options(gpioChoices.map(String), String(gpio)) + '</select></label>' +
    selectField("oneWireEnabled", "Bus actif", bus.enabled !== false) +
    selectField("oneWireScanBoot", "Scan au demarrage", bus.scanOnBoot !== false) +
    '<label>Intervalle lecture ms<input id="oneWireReadMs" type="number" min="1000" max="60000" value="' + esc(bus.readIntervalMs || 2000) + '"></label>' +
    '</div><p class="fieldHelp">Toutes les sondes DS18B20 partagent ce meme bus. Il faut une resistance de tirage, souvent 4,7 kOhm, entre DATA et 3V3.</p>' +
    '<p><button onclick="applyOneWireBus()">Appliquer bus OneWire</button> <button onclick="scanDs()">Scanner sur ce GPIO</button></p></section>';
}

function applyOneWireBus(redraw) {
  cache.sensors.oneWireBus = cache.sensors.oneWireBus || {};
  cache.sensors.oneWireBus.gpio = Number($("oneWireGpio").value);
  cache.sensors.oneWireBus.enabled = boolField("oneWireEnabled");
  cache.sensors.oneWireBus.scanOnBoot = boolField("oneWireScanBoot");
  cache.sensors.oneWireBus.readIntervalMs = Math.max(1000, Math.min(60000, Number($("oneWireReadMs").value) || 2000));
  markDirty("sensors");
  if (redraw !== false) drawSensorsPage();
}

function defaultJsyChannels() {
  return [
    {id:"clamp1", name:"Pince 1", role:"production", measures:["voltageV1", "currentA1", "activePowerW1", "powerFactor1"]},
    {id:"clamp2", name:"Pince 2", role:"grid", measures:["voltageV2", "currentA2", "activePowerW2", "powerFactor2"]}
  ];
}

function ensureJsyChannels(s) {
  if (!s || ((s.id || "") !== "jsy_grid" && (s.type || "") !== "JSY-MK-194T")) return;
  if (!Array.isArray(s.channels) || s.channels.length < 2) s.channels = defaultJsyChannels();
  var defaults = defaultJsyChannels();
  for (var i = 0; i < 2; i++) {
    s.channels[i] = s.channels[i] || {};
    if (!s.channels[i].id) s.channels[i].id = defaults[i].id;
    if (!s.channels[i].name) s.channels[i].name = defaults[i].name;
    if (!s.channels[i].role) s.channels[i].role = defaults[i].role;
    if (!Array.isArray(s.channels[i].measures) || !s.channels[i].measures.length) s.channels[i].measures = defaults[i].measures.slice();
  }
}

function genericSensorState(s) {
  if (s.enabled === false) return {cls:"muted", text:"desactive", detail:""};
  var id = s.id || "";
  var type = s.type || "";
  if (id === "jsy_grid" || type === "JSY-MK-194T") return state.jsyOnline ? {cls:"ok", text:"OK", detail:"trame valide"} : {cls:"bad", text:"Erreur", detail:"JSY absent ou timeout"};
  if (id === "tic_linky" || type === "TIC Linky") return state.ticAvailable ? {cls:"ok", text:"OK", detail:"trame valide"} : {cls:"warn", text:"Absent", detail:"TIC non lue"};
  return {cls:"ok", text:"actif", detail:"configure"};
}

function sensorRoleText(s) {
  if ((s.id || "") === "jsy_grid" || (s.type || "") === "JSY-MK-194T") {
    var channels = s.channels || [];
    if (channels.length) return channels.map(function (c, i) {
      return (c.name || c.id || ("voie " + (i + 1))) + " : " + (c.role || "non defini");
    }).join(" / ");
  }
  return s.role || "";
}

function sensorExtraValue(s) {
  if ((s.id || "") !== "jsy_grid" && (s.type || "") !== "JSY-MK-194T") return "";
  var channels = Array.isArray(s.channels) ? s.channels : defaultJsyChannels();
  var ch1 = channels[0] || defaultJsyChannels()[0];
  var ch2 = channels[1] || defaultJsyChannels()[1];
  return '<div class="channelGrid">' +
    '<span><b>' + esc(ch1.name || ch1.id || "Pince 1") + '</b> <small>' + esc(ch1.role || "custom") + '</small> ' + esc(fmt(state.activePowerW1)) + ' W / ' + esc(fmt(state.currentA1)) + ' A / ' + esc(fmt(state.voltageV1)) + ' V / PF ' + esc(fmt(state.powerFactor1)) + '</span>' +
    '<span><b>' + esc(ch2.name || ch2.id || "Pince 2") + '</b> <small>' + esc(ch2.role || "custom") + '</small> ' + esc(fmt(state.activePowerW2)) + ' W / ' + esc(fmt(state.currentA2)) + ' A / ' + esc(fmt(state.voltageV2)) + ' V / PF ' + esc(fmt(state.powerFactor2)) + '</span>' +
    '<span><b>Reseau</b> ' + esc(fmt(state.gridPowerW)) + ' W</span>' +
    '<span><b>Surplus</b> ' + esc(fmt(state.surplusW)) + ' W</span>' +
    '</div>';
}

function sensorFormHtml(kind, index, s) {
  s = s || {};
  var isDs = kind === "ds";
  ensureJsyChannels(s);
  var type = isDs ? "DS18B20" : (s.type || "Virtual");
  var isJsy = !isDs && ((s.id || "") === "jsy_grid" || type === "JSY-MK-194T");
  var isTic = !isDs && ((s.id || "") === "tic_linky" || type === "TIC Linky");
  var channels = s.channels || defaultJsyChannels();
  var roleOptions = sensorRolesForType(type);
  var ticMode = s.mode || "historique";
  var ticBaudrate = s.baudrate || (ticMode === "standard" ? 9600 : 1200);
  return '<h2>' + (index >= 0 ? "Modifier" : "Ajouter") + ' ' + (isDs ? "DS18B20" : "capteur") + '</h2><input id="sensorKind" type="hidden" value="' + kind + '"><input id="sensorIndex" type="hidden" value="' + index + '">' +
    '<div class="form">' +
    textField("sensorName", "Nom", s.name || "") +
    (isDs ? '<input id="sensorType" type="hidden" value="DS18B20">' : '<label>Type<select id="sensorType" onchange="updateSensorRoleOptions()">' + options(sensorTypes, type) + '</select></label>') +
    '<label>Role<select id="sensorRole">' + options(roleOptions, s.role || roleOptions[0] || "custom") + '</select></label>' +
    (isDs ? textField("sensorAddress", "Adresse OneWire", s.address || "") + '<p class="fieldHelp">Le GPIO ne se regle pas par sonde: il est commun aux DS18B20 dans le bloc Bus OneWire ci-dessus.</p>' : field("sensorGpio", "GPIO", s.gpio == null ? "" : s.gpio) + field("sensorRx", "RX", s.rx == null ? "" : s.rx) + field("sensorTx", "TX", s.tx == null ? "" : s.tx) + textField("sensorSource", "Source", s.source || "local") + textField("sensorMac", "MAC ESP-NOW", s.mac || "")) +
    '<label><input id="sensorEnabled" class="check" type="checkbox" ' + checked(s.enabled) + '> Actif</label>' +
    (isDs ? '<label><input id="sensorCritical" class="check" type="checkbox" ' + checked(s.critical) + '> Critique securite</label>' : '') +
    (isJsy ? '<div class="subPanel"><h3>Modbus JSY</h3><div class="formGrid">' + field("jsyBaudrate", "Vitesse bauds", s.baudrate || 4800) + field("jsyAddress", "Adresse Modbus", s.modbusAddress || s.address || 1) + field("jsyReadInterval", "Lecture ms", s.readIntervalMs || 500) + field("jsyTimeout", "Timeout ms", s.timeoutMs || 400) + field("jsyRs485Dir", "GPIO DE/RE RS485", s.rs485DirPin == null ? -1 : s.rs485DirPin) + '</div><p class="muted">Laisse DE/RE a -1 avec un adaptateur RS485 auto-direction ou une liaison TTL. Avec un MAX485 classique, indique le GPIO qui pilote DE et RE.</p></div><div class="subPanel"><h3>Voies amperemetriques JSY</h3><div class="formGrid"><label>Pince 1 nom<input id="jsyCh1Name" value="' + esc((channels[0] || {}).name || "Pince 1") + '"></label><label>Pince 1 role<select id="jsyCh1Role">' + options(jsyClampRoles, (channels[0] || {}).role || "production") + '</select></label><label>Pince 2 nom<input id="jsyCh2Name" value="' + esc((channels[1] || {}).name || "Pince 2") + '"></label><label>Pince 2 role<select id="jsyCh2Role">' + options(jsyClampRoles, (channels[1] || {}).role || "grid") + '</select></label></div><p class="muted">grid = arrivee reseau, production = solaire, load = charge dediee, custom = autre usage. Dans Logique, utilise activePowerW1 pour la pince 1 et activePowerW2 pour la pince 2.</p></div>' : '') +
    (isTic ? '<div class="subPanel"><h3>TIC Linky</h3><div class="formGrid"><label>Mode TIC<select id="ticMode" onchange="syncTicBaudrate()">' + options(["historique", "standard"], ticMode) + '</select></label>' + field("ticBaudrate", "Vitesse bauds", ticBaudrate) + field("ticTimeout", "Timeout ms", s.timeoutMs || 5000) + '</div><p class="muted">Standard = 9600 bauds 7E1. Historique = 1200 bauds 7E1.</p></div>' : '') +
    '<details class="advancedField"><summary>Avance</summary>' + textField("sensorId", "ID technique", s.id || "") + '<p class="muted">Laisse vide pour creer automatiquement un ID depuis le nom.</p></details>' +
    '</div><p><button onclick="applySensorForm()">Appliquer</button></p>';
}

function sensorRolesForType(type) {
  return sensorRolesByType[type] || ["custom"];
}

function updateSensorRoleOptions() {
  var type = $("sensorType").value;
  var current = $("sensorRole").value;
  var list = sensorRolesForType(type);
  if (list.indexOf(current) < 0) current = list[0];
  $("sensorRole").innerHTML = options(list, current);
}

function syncTicBaudrate() {
  if (!$("ticMode") || !$("ticBaudrate")) return;
  $("ticBaudrate").value = $("ticMode").value === "standard" ? 9600 : 1200;
}

function newSensor() { $("sensorForm").innerHTML = sensorFormHtml("sensor", -1, {enabled:true, source:"local"}); }
function newJsySensor() { $("sensorForm").innerHTML = sensorFormHtml("sensor", -1, {id:"jsy_grid", name:"JSY reseau", type:"JSY-MK-194T", source:"local", serial:"Serial2", rx:26, tx:27, baudrate:4800, modbusAddress:1, readIntervalMs:500, timeoutMs:400, rs485DirPin:-1, role:"mesure reseau principal", enabled:true, channels:defaultJsyChannels()}); }
function editSensor(index) { $("sensorForm").innerHTML = sensorFormHtml("sensor", index, (cache.sensors.sensors || [])[index]); }
function newDsSensor() { $("sensorForm").innerHTML = sensorFormHtml("ds", -1, {enabled:true, critical:false, unit:"C"}); }
function editDsSensor(index) { $("sensorForm").innerHTML = sensorFormHtml("ds", index, (cache.sensors.ds18b20 || [])[index]); }

function applySensorForm() {
  var kind = $("sensorKind").value;
  var index = Number($("sensorIndex").value);
  var type = $("sensorType").value;
  var name = $("sensorName").value;
  var item = {
    id: $("sensorId").value || uniqueSensorId(name || type, index, kind),
    name: name,
    role: $("sensorRole").value,
    enabled: $("sensorEnabled").checked
  };
  if (kind === "ds") {
    item.address = $("sensorAddress").value;
    item.critical = $("sensorCritical").checked;
    item.unit = "C";
    cache.sensors.ds18b20 = cache.sensors.ds18b20 || [];
    if (index >= 0) cache.sensors.ds18b20[index] = item; else cache.sensors.ds18b20.push(item);
  } else {
    item.type = type;
    item.source = $("sensorSource").value || "local";
    item.gpio = readNumber("sensorGpio", undefined);
    item.rx = readNumber("sensorRx", undefined);
    item.tx = readNumber("sensorTx", undefined);
    item.mac = $("sensorMac").value;
    if (item.type === "JSY-MK-194T" || item.id === "jsy_grid") {
      item.baudrate = readNumber("jsyBaudrate", 4800);
      item.modbusAddress = readNumber("jsyAddress", 1);
      item.readIntervalMs = readNumber("jsyReadInterval", 500);
      item.timeoutMs = readNumber("jsyTimeout", 400);
      item.rs485DirPin = readNumber("jsyRs485Dir", -1);
      item.channels = [
        {id:"clamp1", name:$("jsyCh1Name") ? $("jsyCh1Name").value : "Pince 1", role:$("jsyCh1Role") ? $("jsyCh1Role").value : "production", measures:["voltageV1", "currentA1", "activePowerW1", "powerFactor1"]},
        {id:"clamp2", name:$("jsyCh2Name") ? $("jsyCh2Name").value : "Pince 2", role:$("jsyCh2Role") ? $("jsyCh2Role").value : "grid", measures:["voltageV2", "currentA2", "activePowerW2", "powerFactor2"]}
      ];
    } else if (item.type === "TIC Linky" || item.id === "tic_linky") {
      item.mode = $("ticMode") ? $("ticMode").value : "historique";
      item.baudrate = readNumber("ticBaudrate", item.mode === "standard" ? 9600 : 1200);
      item.timeoutMs = readNumber("ticTimeout", 5000);
    }
    cache.sensors.sensors = cache.sensors.sensors || [];
    if (index >= 0) cache.sensors.sensors[index] = item; else cache.sensors.sensors.push(item);
  }
  markDirty("sensors");
  drawSensorsPage();
}

function toggleSensor(index) {
  cache.sensors.sensors[index].enabled = cache.sensors.sensors[index].enabled === false;
  markDirty("sensors");
  drawSensorsPage();
}

function toggleDsSensor(index) {
  cache.sensors.ds18b20[index].enabled = cache.sensors.ds18b20[index].enabled === false;
  markDirty("sensors");
  drawSensorsPage();
}

function deleteSensor(index) {
  if (confirm("Supprimer ce capteur ?")) {
    cache.sensors.sensors.splice(index, 1);
    markDirty("sensors");
    drawSensorsPage();
  }
}

function deleteDsSensor(index) {
  if (confirm("Supprimer cette sonde DS18B20 ?")) {
    cache.sensors.ds18b20.splice(index, 1);
    markDirty("sensors");
    drawSensorsPage();
  }
}

async function saveSensors() {
  var response = await postJson("/api/sensors", cache.sensors);
  if (response.ok) {
    clearDirty("sensors");
    cache.sensors = await api("/api/sensors");
  }
  alert(response.ok ? "Capteurs sauvegardes et relus depuis LittleFS" : "Sauvegarde refusee: " + await response.text());
  drawSensorsPage();
}

function pinText(item) {
  var parts = [];
  if (item.gpio != null) parts.push("GPIO " + item.gpio);
  if (item.rx != null) parts.push("RX " + item.rx);
  if (item.tx != null) parts.push("TX " + item.tx);
  return parts.join(" ");
}

async function scanDs() {
  var scanBox = $("scan");
  if (scanBox) scanBox.innerHTML = '<div class="warnBox">Scan DS18B20 en cours...</div>';
  if ($("oneWireGpio")) {
    applyOneWireBus(false);
    var saveResponse = await postJson("/api/sensors", cache.sensors);
    if (!saveResponse.ok) {
      if ($("scan")) $("scan").innerHTML = '<div class="warnBox">Impossible de sauvegarder le GPIO OneWire avant scan: ' + esc(await saveResponse.text()) + '</div>';
      return;
    }
    clearDirty("sensors");
    cache.sensors = await api("/api/sensors");
    drawSensorsPage();
  }
  var addresses = await api("/api/ds18b20");
  var list = Array.isArray(addresses) ? addresses : [];
  var gpio = ((cache.sensors.oneWireBus || {}).gpio == null ? 13 : (cache.sensors.oneWireBus || {}).gpio);
  if (!list.length) {
    if ($("scan")) $("scan").innerHTML = '<div class="warnBox">Aucune sonde detectee sur le bus OneWire GPIO' + esc(gpio) + '.</div><pre>' + esc(JSON.stringify(addresses, null, 2)) + '</pre>';
    return;
  }
  var targets = (cache.sensors.ds18b20 || []).map(function (sensor, index) {
    return {
      id: sensor.id || ("sonde" + (index + 1)),
      label: sensor.id || ("sonde" + (index + 1))
    };
  });
  if (!targets.length) targets = [{id:"sonde1", label:"sonde1"}, {id:"sonde2", label:"sonde2"}, {id:"sonde3", label:"sonde3"}];
  if ($("scan")) $("scan").innerHTML = '<div class="scanList"><h3>Sondes detectees</h3>' + list.map(function (address) {
    return '<div class="scanItem"><code>' + esc(address) + '</code><span>' +
      targets.map(function (target) {
        return '<button onclick="assignDsAddress(\'' + esc(target.id) + '\', \'' + esc(address) + '\')">' + esc(target.label) + '</button>';
      }).join("") +
      '<button onclick="fillCurrentDsAddress(\'' + esc(address) + '\')">remplir le formulaire</button>' +
    '</span></div>';
  }).join("") + '</div>';
}

function fillCurrentDsAddress(address) {
  if (!$("sensorAddress")) return alert("Ouvre d'abord Modifier sur sonde1, sonde2 ou sonde3.");
  $("sensorAddress").value = address;
  markDirty("sensors");
}

async function assignDsAddress(sensorId, address) {
  if (!confirm("Affecter cette adresse a " + sensorId + " ?")) return;
  var response = await fetch("/api/ds18b20/assign", {
    method:"POST",
    headers:{"Content-Type":"application/x-www-form-urlencoded"},
    body:new URLSearchParams({sensorId:sensorId, address:address})
  });
  if (!response.ok) return alert("Affectation refusee: " + await response.text());
  cache.sensors = await api("/api/sensors");
  clearDirty("sensors");
  drawSensorsPage();
  if ($("scan")) $("scan").innerHTML = '<div class="pendingBox">Adresse affectee a ' + esc(sensorId) + '. Redemarre si la lecture ne se deplace pas immediatement.</div>';
}

async function actuatorsPage() {
  cache.actuators = await api("/api/actuators");
  drawActuatorsPage();
}

function drawActuatorsPage() {
  var rows = "";
  (cache.actuators.actuators || []).forEach(function (a, i) {
    rows += '<tr><td>' + esc(a.name || a.id) + '</td><td>' + esc(a.type) + '</td><td>' + esc(pinText({gpio:a.gpio, rx:a.zeroCross, tx:a.control})) + '</td><td>' + esc(a.mode) + '</td><td>' + esc(commandFor(a.id)) + '</td><td><span class="badge ' + (a.enabled !== false ? "ok" : "muted") + '">' + esc(a.enabled !== false ? "actif" : "off") + '</span></td><td class="actions"><button onclick="editActuator(' + i + ')">Modifier</button><button onclick="toggleActuator(' + i + ')">' + (a.enabled !== false ? "Desactiver" : "Activer") + '</button><button onclick="forceOff(\'' + esc(a.id) + '\')">OFF</button><button class="danger" onclick="deleteActuator(' + i + ')">Supprimer</button></td></tr>';
  });
  $("app").innerHTML = banner() + '<h1>Actionneurs</h1>' + helpBox("actuators") + dirtyNotice("actuators") + '<div class="toolbar"><button onclick="newActuator()">Ajouter actionneur</button><button onclick="saveActuators()">Sauvegarder</button><button onclick="jsonEditor(\'actuators\')">JSON avance</button></div><section class="panel" id="actuatorForm">Selectionne un actionneur ou ajoute-en un nouveau.</section><table><tr><th>Nom</th><th>Type</th><th>GPIO</th><th>Mode</th><th>Commande</th><th>Etat</th><th>Actions</th></tr>' + rows + '</table>';
}

function actuatorFormHtml(index, a) {
  a = a || {};
  var type = a.type || "SSR";
  var modes = actuatorModesForType(type);
  var mode = modes.indexOf(a.mode || "") >= 0 ? a.mode : modes[0];
  return '<h2>' + (index >= 0 ? "Modifier" : "Ajouter") + ' actionneur</h2><input id="actuatorIndex" type="hidden" value="' + index + '">' +
    '<div class="form">' +
    textField("actuatorName", "Nom", a.name || "") +
    '<label>Type<select id="actuatorType" onchange="updateActuatorModeOptions()">' + options(actuatorTypes, type) + '</select></label>' +
    '<label>Mode<select id="actuatorMode" onchange="updateActuatorModeHelp()">' + options(modes, mode) + '</select><span id="actuatorModeHelp" class="fieldHelp">' + esc(actuatorModeHelp[mode] || "") + '</span></label>' +
    field("actuatorGpio", "GPIO sortie", a.gpio == null ? "" : a.gpio) +
    field("actuatorZero", "GPIO zero-cross", a.zeroCross == null ? "" : a.zeroCross) +
    field("actuatorControl", "GPIO controle", a.control == null ? "" : a.control) +
    field("actuatorMaxPower", "Puissance max W", a.maxPowerW == null ? "" : a.maxPowerW) +
    field("actuatorCycle", "Cycle ms", a.cycleMs == null ? "" : a.cycleMs) +
    textField("actuatorSource", "Source", a.source || "local") +
    textField("actuatorMac", "MAC ESP-NOW", a.mac || "") +
    '<label><input id="actuatorEnabled" class="check" type="checkbox" ' + checked(a.enabled) + '> Actif</label>' +
    '<label><input id="actuatorCritical" class="check" type="checkbox" ' + checked(a.critical) + '> Critique</label>' +
    '<details class="advancedField"><summary>Avance</summary>' + textField("actuatorId", "ID technique", a.id || "") + '<p class="muted">Laisse vide pour creer automatiquement un ID depuis le nom.</p></details>' +
    '</div><p><button onclick="applyActuatorForm()">Appliquer</button></p>';
}

function actuatorModesForType(type) {
  return actuatorModeByType[type] || actuatorModes;
}

function updateActuatorModeOptions() {
  var type = $("actuatorType").value;
  var current = $("actuatorMode").value;
  var modes = actuatorModesForType(type);
  if (modes.indexOf(current) < 0) current = modes[0];
  $("actuatorMode").innerHTML = options(modes, current);
  updateActuatorModeHelp();
}

function updateActuatorModeHelp() {
  var mode = $("actuatorMode") ? $("actuatorMode").value : "OFF";
  var help = $("actuatorModeHelp");
  if (help) help.textContent = actuatorModeHelp[mode] || "";
}

function newActuator() { $("actuatorForm").innerHTML = actuatorFormHtml(-1, {enabled:true, critical:false, source:"local"}); }
function editActuator(index) { $("actuatorForm").innerHTML = actuatorFormHtml(index, (cache.actuators.actuators || [])[index]); }

function applyActuatorForm() {
  var index = Number($("actuatorIndex").value);
  var type = $("actuatorType").value;
  var mode = $("actuatorMode").value;
  if (actuatorModesForType(type).indexOf(mode) < 0) return alert("Mode incompatible avec ce type d'actionneur.");
  var id = $("actuatorId").value || uniqueActuatorId($("actuatorName").value || type, index);
  var item = {
    id: id,
    name: $("actuatorName").value,
    type: type,
    mode: mode,
    gpio: readNumber("actuatorGpio", undefined),
    zeroCross: readNumber("actuatorZero", undefined),
    control: readNumber("actuatorControl", undefined),
    maxPowerW: readNumber("actuatorMaxPower", undefined),
    cycleMs: readNumber("actuatorCycle", undefined),
    source: $("actuatorSource").value || "local",
    mac: $("actuatorMac").value,
    enabled: $("actuatorEnabled").checked,
    critical: $("actuatorCritical").checked
  };
  cache.actuators.actuators = cache.actuators.actuators || [];
  if (index >= 0) cache.actuators.actuators[index] = item; else cache.actuators.actuators.push(item);
  markDirty("actuators");
  drawActuatorsPage();
}

function toggleActuator(index) {
  cache.actuators.actuators[index].enabled = cache.actuators.actuators[index].enabled === false;
  markDirty("actuators");
  drawActuatorsPage();
}

function deleteActuator(index) {
  if (confirm("Supprimer cet actionneur ?")) {
    cache.actuators.actuators.splice(index, 1);
    markDirty("actuators");
    drawActuatorsPage();
  }
}

async function saveActuators() {
  var response = await postJson("/api/actuators", cache.actuators);
  if (response.ok) clearDirty("actuators");
  alert(response.ok ? "Actionneurs sauvegardes" : "Sauvegarde refusee: " + await response.text());
  drawActuatorsPage();
}

function commandFor(id) {
  if (id === "ssr1_water_heater") return fmt(state.ssr1PowerPct) + " %";
  if (id === "ssr2_aux") return fmt(state.ssr2PowerPct) + " %";
  if (id === "robotdyn_triac") return fmt(state.robotDynPowerPct) + " %";
  return "-";
}

async function forceOff(id) {
  await fetch("/api/actuator/command", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:new URLSearchParams({id:id, command:"stop", value:"0"})});
  await refresh();
}

async function logicPage() {
  cache.rules = await api("/api/rules");
  cache.actuators = await api("/api/actuators");
  drawLogicPage();
}

function drawLogicPage() {
  var rows = "";
  (cache.rules.rules || []).forEach(function (r, i) {
    rows += '<tr><td><span class="badge ' + (r.enabled !== false ? "ok" : "muted") + '">' + esc(r.enabled !== false ? "ON" : "OFF") + '</span></td><td>' + esc(r.name || r.id) + '</td><td>' + esc(r.priority) + '</td><td>' + esc(r.logic || "AND") + '</td><td>' + esc((r.conditions || []).length) + '</td><td>' + esc((r.actions || []).length) + '</td><td class="actions"><button onclick="editRule(' + i + ')">Modifier</button><button onclick="copyRule(' + i + ')">Copier</button><button onclick="toggleRule(' + i + ')">' + (r.enabled !== false ? "Desactiver" : "Activer") + '</button><button class="danger" onclick="deleteRule(' + i + ')">Supprimer</button></td></tr>';
  });
  $("app").innerHTML = banner() + '<h1>Logique</h1>' + helpBox("logic") + dirtyNotice("rules") + '<div class="toolbar"><button onclick="newRule()">Ajouter regle</button><button onclick="saveRules()">Sauvegarder</button><button onclick="validateRules()">Valider</button><button onclick="jsonEditor(\'rules\')">JSON avance</button></div><div id="validation"></div><section class="panel" id="ruleForm">Selectionne une regle ou ajoute-en une nouvelle.</section><table><tr><th>Etat</th><th>Regle</th><th>Priorite</th><th>Logique</th><th>Conditions</th><th>Actions</th><th>Commandes</th></tr>' + rows + '</table>';
}

async function validateRules() {
  $("validation").textContent = JSON.stringify(await api("/api/rules/validate"), null, 2);
}

function ruleFormHtml(index, r) {
  r = r || {};
  return '<h2>' + (index >= 0 ? "Modifier" : "Ajouter") + ' regle</h2><input id="ruleIndex" type="hidden" value="' + index + '">' +
    '<div class="form">' +
    textField("ruleId", "ID", r.id || uid("rule")) +
    textField("ruleName", "Nom", r.name || "") +
    field("rulePriority", "Priorite", r.priority == null ? 10 : r.priority) +
    '<label>Logique<select id="ruleLogic">' + options(["AND", "OR"], r.logic || "AND") + '</select></label>' +
    '<label><input id="ruleEnabled" class="check" type="checkbox" ' + checked(r.enabled) + '> Active</label>' +
    '</div><h2>SI</h2><div class="ruleRows" id="condRows">' +
    (r.conditions || []).map(function (c, k) { return conditionFormRow(c, k); }).join("") +
    '</div><p><button onclick="addConditionRow()">Ajouter une condition</button></p>' +
    '<h2>ALORS</h2><div class="ruleRows" id="actionRows">' +
    (r.actions || []).map(function (a, k) { return actionFormRow(a, k); }).join("") +
    '</div><p><button onclick="addActionRow()">Ajouter une action</button></p>' +
    '<p><button onclick="applyRuleForm()">Appliquer</button> <button onclick="newRule()">Vider</button></p>';
}

function newRule() {
  $("ruleForm").innerHTML = ruleFormHtml(-1, {enabled:true, priority:10, logic:"AND", conditions:[], actions:[]});
}

function editRule(index) {
  $("ruleForm").innerHTML = ruleFormHtml(index, (cache.rules.rules || [])[index]);
}

function readJsonArray(id) {
  var data = JSON.parse($(id).value || "[]");
  if (!Array.isArray(data)) throw new Error(id + " doit etre un tableau JSON");
  return data;
}

function applyRuleForm() {
  try {
    var index = Number($("ruleIndex").value);
    var item = readRuleForm();
    if (!item.id) return alert("ID obligatoire");
    cache.rules.rules = cache.rules.rules || [];
    if (index >= 0) cache.rules.rules[index] = item; else cache.rules.rules.push(item);
    cache.rules.rules.sort(function (a, b) { return Number(b.priority || 0) - Number(a.priority || 0); });
    markDirty("rules");
    drawLogicPage();
  } catch (error) {
    alert("JSON invalide: " + error.message);
  }
}

function sourceDef(source) {
  for (var i = 0; i < ruleSources.length; i++) if (ruleSources[i].id === source) return ruleSources[i];
  return ruleSources[0];
}

function measureDef(source, measure) {
  var sd = sourceDef(source);
  for (var i = 0; i < sd.measures.length; i++) if (sd.measures[i][0] === measure) return sd.measures[i];
  return sd.measures[0];
}

function measureText(key) {
  return measureLabels[key] || key;
}

function opsForType(type) {
  return type === "number" ? [">", ">=", "<", "<=", "==", "!="] : ["==", "!="];
}

function valueControl(md, value) {
  var type = md[1];
  if (type === "boolean") return '<select data-c="value"><option value="true" ' + (value === true || value === "true" ? "selected" : "") + '>true</option><option value="false" ' + (value === false || value === "false" ? "selected" : "") + '>false</option></select>';
  if (type === "enum") return '<select data-c="value">' + options(md[3] || [], value) + '</select>';
  if (type === "number") return '<input data-c="value" type="number" step="any" value="' + esc(value == null ? 0 : value) + '">';
  return '<input data-c="value" value="' + esc(value == null ? "" : value) + '">';
}

function conditionFormRow(c, k) {
  c = c || {};
  var source = c.source || c.sensorId || "JSY-MK-194T";
  var sd = sourceDef(source);
  var measure = c.measure || c.variable || sd.measures[0][0];
  var md = measureDef(source, measure);
  var ops = opsForType(md[1]);
  var op = ops.indexOf(c.operator) >= 0 ? c.operator : ops[0];
  var sourceOptions = ruleSources.map(function (s) {
    return '<option value="' + esc(s.id) + '" ' + (source === s.id ? "selected" : "") + '>' + esc(s.label) + '</option>';
  }).join("");
  var measureOptions = sd.measures.map(function (m) {
    return '<option value="' + esc(m[0]) + '" ' + (measure === m[0] ? "selected" : "") + '>' + esc(measureText(m[0])) + '</option>';
  }).join("");
  return '<div class="ruleRow">' +
    '<label>Source<select data-c="source" onchange="updateConditionRow(this)">' + sourceOptions + '</select></label>' +
    '<label>Mesure<select data-c="measure" onchange="updateConditionRow(this)">' + measureOptions + '</select></label>' +
    '<label>Comparaison<select data-c="operator">' + options(ops, op) + '</select></label>' +
    '<label>Valeur<span class="valueHost">' + valueControl(md, c.value) + '</span></label>' +
    '<span class="unitCell" data-c="unit">' + esc(md[2] || "") + '</span>' +
    '<button class="danger" onclick="removeRuleRow(this)">Supprimer</button>' +
    '<input data-c="type" type="hidden" value="' + esc(md[1]) + '">' +
    '<input data-c="id" type="hidden" value="' + esc(c.id || uid("cond")) + '">' +
    '</div>';
}

function updateConditionRow(el) {
  var row = el.closest(".ruleRow");
  var source = row.querySelector('[data-c="source"]').value;
  var sd = sourceDef(source);
  var measureSel = row.querySelector('[data-c="measure"]');
  var current = measureSel.value;
  if (el.getAttribute("data-c") === "source" || !sd.measures.some(function (m) { return m[0] === current; })) {
    current = sd.measures[0][0];
  }
  measureSel.innerHTML = sd.measures.map(function (m) {
    return '<option value="' + esc(m[0]) + '" ' + (current === m[0] ? "selected" : "") + '>' + esc(measureText(m[0])) + '</option>';
  }).join("");
  var md = measureDef(source, current);
  row.querySelector('[data-c="operator"]').innerHTML = options(opsForType(md[1]), opsForType(md[1])[0]);
  row.querySelector(".valueHost").innerHTML = valueControl(md, md[1] === "boolean" ? true : ((md[3] || [0])[0]));
  row.querySelector('[data-c="unit"]').textContent = md[2] || "";
  row.querySelector('[data-c="type"]').value = md[1];
  markDirty("rules");
}

function actuatorOptions(selected) {
  var arr = (cache.actuators && cache.actuators.actuators) || [];
  if (!arr.length) return '<option value="">Aucun actionneur</option>';
  return arr.map(function (a) {
    return '<option value="' + esc(a.id) + '" ' + (selected === a.id ? "selected" : "") + '>' + esc((a.name || a.id) + " (" + a.id + ")") + '</option>';
  }).join("");
}

function commandOptions(selected) {
  return ruleCommands.map(function (cmd) {
    return '<option value="' + esc(cmd) + '" ' + (cmd === (selected || "setActuatorPercent") ? "selected" : "") + '>' + esc(commandLabels[cmd] || cmd) + '</option>';
  }).join("");
}

function actionUsesValue(command) {
  return ["setActuatorPercent", "setPower", "setPowerWatts"].indexOf(command) >= 0;
}

function actionValueControl(a) {
  a = a || {};
  var command = a.command || "setActuatorPercent";
  if (command === "setMode") return '<select data-a="mode">' + options(actuatorModes, a.mode || a.value || "OFF") + '</select><input data-a="value" type="hidden" value="' + esc(a.value || "") + '">';
  if (command === "logEvent" || command === "setSafetyWarning") return '<input data-a="message" value="' + esc(a.message || "Evenement regle") + '"><input data-a="value" type="hidden" value="0">';
  if (command === "setPowerFromSurplus") return '<input data-a="maxHeaterPowerW" type="number" value="' + esc(a.maxHeaterPowerW || 1500) + '" placeholder="max W"><input data-a="value" type="hidden" value="0"><span class="muted">surplus proportionnel</span>';
  if (["stop","off","on","toggle","safetyShutdown"].indexOf(command) >= 0) return '<input data-a="value" type="hidden" value="0"><span class="muted">auto</span>';
  return '<input data-a="value" type="number" step="any" value="' + esc(a.value == null ? 0 : a.value) + '" placeholder="' + (command === "setPowerWatts" ? "W" : "%") + '">';
}

function followMeasureOptions(selected) {
  return ["surplusW", "injectionW", "gridPowerW", "activePowerW1", "activePowerW2"].map(function (key) {
    return '<option value="' + esc(key) + '" ' + (key === selected ? "selected" : "") + '>' + esc(measureText(key)) + '</option>';
  }).join("");
}

function actionCommandValueBlock(a) {
  a = a || {};
  var command = a.command || "setActuatorPercent";
  var follows = !!(a.sourceVariable || a.sourceMeasure);
  if (!actionUsesValue(command)) {
    return '<div class="actionValueHost">' + actionValueControl(a) + '</div><input data-a="valueMode" type="hidden" value="fixed">';
  }
  return '<label>Valeur de commande<select data-a="valueMode" onchange="updateActionValueMode(this)">' +
    '<option value="fixed" ' + (!follows ? "selected" : "") + '>Valeur fixe</option>' +
    '<option value="measure" ' + (follows ? "selected" : "") + '>Suivre une mesure</option>' +
    '</select></label>' +
    '<span class="actionValueHost">' + (follows ? actionMeasureControl(a) : actionValueControl(a)) + '</span>';
}

function actionMeasureControl(a) {
  var selected = a.sourceVariable || a.sourceMeasure || "surplusW";
  return '<select data-a="sourceVariable">' + followMeasureOptions(selected) + '</select><input data-a="sourceSensorId" type="hidden" value="jsy_grid"><input data-a="value" type="hidden" value="0"><span class="muted">la commande suivra cette mesure</span>';
}

function actionFormRow(a, k) {
  a = a || {};
  var actuatorId = a.actuatorId || (((cache.actuators || {}).actuators || [])[0] || {}).id || "";
  var command = a.command || "setActuatorPercent";
  return '<div class="ruleActionRow">' +
    '<label>Actionneur<select data-a="actuatorId">' + actuatorOptions(actuatorId) + '</select></label>' +
    '<label>Commande<select data-a="command" onchange="updateActionRow(this)">' + commandOptions(command) + '</select></label>' +
    actionCommandValueBlock(a) +
    '<button class="danger" onclick="removeRuleRow(this)">Supprimer</button>' +
    '<div class="ruleHint">' + esc(commandHelps[command] || "") + '</div>' +
    '</div>';
}

function updateActionRow(el) {
  var row = el.closest(".ruleActionRow");
  var actuatorId = row.querySelector('[data-a="actuatorId"]').value;
  var command = row.querySelector('[data-a="command"]').value;
  var valueBlock = actionCommandValueBlock({command:command, value:0});
  var button = row.querySelector("button.danger").outerHTML;
  row.innerHTML = '<label>Actionneur<select data-a="actuatorId">' + actuatorOptions(actuatorId) + '</select></label>' +
    '<label>Commande<select data-a="command" onchange="updateActionRow(this)">' + commandOptions(command) + '</select></label>' +
    valueBlock + button + '<div class="ruleHint">' + esc(commandHelps[command] || "") + '</div>';
  markDirty("rules");
}

function updateActionValueMode(el) {
  var row = el.closest(".ruleActionRow");
  var command = row.querySelector('[data-a="command"]').value;
  row.querySelector(".actionValueHost").innerHTML = el.value === "measure" ? actionMeasureControl({command:command}) : actionValueControl({command:command, value:0});
  markDirty("rules");
}

function removeRuleRow(button) {
  button.parentElement.remove();
  markDirty("rules");
}

function addConditionRow() {
  $("condRows").insertAdjacentHTML("beforeend", conditionFormRow());
  markDirty("rules");
}

function addActionRow() {
  $("actionRows").insertAdjacentHTML("beforeend", actionFormRow());
  markDirty("rules");
}

function readRuleForm() {
  var item = {
    id: $("ruleId").value,
    name: $("ruleName").value,
    enabled: $("ruleEnabled").checked,
    priority: Number($("rulePriority").value || 0),
    logic: $("ruleLogic").value,
    conditions: [],
    actions: []
  };
  Array.prototype.forEach.call(document.querySelectorAll("#condRows .ruleRow"), function (row) {
    var c = {};
    Array.prototype.forEach.call(row.querySelectorAll("[data-c]"), function (el) { c[el.getAttribute("data-c")] = el.value; });
    var md = measureDef(c.source, c.measure);
    c.type = md[1];
    c.unit = md[2] || "";
    if (c.type === "number") c.value = Number(c.value);
    if (c.type === "boolean") c.value = c.value === "true";
    if (c.source && c.measure) item.conditions.push(c);
  });
  Array.prototype.forEach.call(document.querySelectorAll("#actionRows .ruleActionRow"), function (row) {
    var a = {};
    Array.prototype.forEach.call(row.querySelectorAll("[data-a]"), function (el) { a[el.getAttribute("data-a")] = el.value; });
    var valueMode = a.valueMode || "fixed";
    delete a.valueMode;
    if (a.command === "setMode") a.value = a.mode;
    else if (a.value !== "" && a.value != null) a.value = Number(a.value);
    else delete a.value;
    if (valueMode !== "measure") {
      delete a.sourceSensorId;
      delete a.sourceVariable;
    } else if (!a.sourceSensorId) {
      a.sourceSensorId = "jsy_grid";
    }
    if (!a.sourceVariable) delete a.sourceVariable;
    if (a.actuatorId && a.command) item.actions.push(a);
  });
  return item;
}

function toggleRule(index) {
  cache.rules.rules[index].enabled = cache.rules.rules[index].enabled === false;
  markDirty("rules");
  drawLogicPage();
}

function copyRule(index) {
  var copy = JSON.parse(JSON.stringify(cache.rules.rules[index]));
  copy.id = uid("rule");
  copy.name = (copy.name || copy.id) + " copie";
  cache.rules.rules.push(copy);
  markDirty("rules");
  drawLogicPage();
}

function deleteRule(index) {
  if (confirm("Supprimer cette regle ?")) {
    cache.rules.rules.splice(index, 1);
    markDirty("rules");
    drawLogicPage();
  }
}

async function saveRules() {
  var response = await postJson("/api/rules", cache.rules);
  if (response.ok) clearDirty("rules");
  alert(response.ok ? "Regles sauvegardees" : "Sauvegarde refusee: " + await response.text());
  drawLogicPage();
}

function simulationPage() {
  $("app").innerHTML = banner() +
    '<h1>Simulation</h1><div class="form">' +
    field("grid", "gridPowerW", state.gridPowerW || -800) +
    field("voltage", "Tension V", 231) +
    field("current", "Courant A", 4) +
    field("jsyP1", "JSY puissance active 1 W", state.activePowerW1 || state.gridPowerW || -800) +
    field("jsyC1", "JSY courant 1 A", state.currentA1 || 4) +
    field("jsyP2", "JSY puissance active 2 W", state.activePowerW2 || 0) +
    field("jsyC2", "JSY courant 2 A", state.currentA2 || 0) +
    field("t1", "Sonde 1 C", state.tankTopC || 45) +
    field("t2", "Sonde 2 C", state.tankMiddleC || 42) +
    field("t3", "Sonde 3 C", state.tankBottomC || 38) +
    '</div><p><button onclick="simEnable()">Activer</button> <button onclick="simValues()">Appliquer</button> <button onclick="simRandom()">Aleatoire</button> <button onclick="simScenario(\'tank_overheat\')">Surchauffe</button> <button onclick="simScenario(\'jsy_lost\')">Perte JSY</button> <button onclick="simDisable()">Desactiver</button></p>' +
    dashboard();
}

function field(id, label, value) {
  return '<label>' + esc(label) + '<input id="' + id + '" type="number" step="0.1" value="' + esc(value) + '"></label>';
}

async function simEnable() {
  var mode = $("simMode") ? $("simMode").value : "manual";
  var response = await fetch("/api/simulation/enable", {method:"POST"});
  if (!response.ok) return alert("Activation simulation refusee: " + await response.text());
  if (mode === "random") {
    response = await fetch("/api/simulation/randomize", {method:"POST"});
    if (!response.ok) return alert("Simulation aleatoire refusee: " + await response.text());
  } else if (mode === "scenario") {
    var scenario = $("simScenario") ? $("simScenario").value : "normal";
    response = await fetch("/api/simulation/scenario", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:new URLSearchParams({scenario:scenario})});
    if (!response.ok) return alert("Scenario simulation refuse: " + await response.text());
  } else {
    response = await fetch("/api/simulation/mode", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:new URLSearchParams({mode:"manual"})});
    if (!response.ok) return alert("Mode simulation refuse: " + await response.text());
    await simValues(false);
    return;
  }
  await refresh();
}
async function simDisable() {
  if (!confirm("Desactiver la simulation et revenir au reel ? Les sorties seront forcees OFF.")) return;
  await fetch("/api/simulation/disable", {method:"POST"});
  await refresh();
}
async function simRandom() {
  await fetch("/api/simulation/randomize", {method:"POST"});
  await refresh();
}
async function simScenario(name) {
  await fetch("/api/simulation/scenario", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:new URLSearchParams({scenario:name})});
  await refresh();
}

async function simModeApply() {
  var mode = $("simMode") ? $("simMode").value : "manual";
  await fetch("/api/simulation/mode", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:new URLSearchParams({mode:mode})});
  await refresh();
}

async function simScenarioApply() {
  var scenario = $("simScenario") ? $("simScenario").value : "normal";
  await simScenario(scenario);
}

async function simValues(enableFirst) {
  if (enableFirst !== false) {
    var enableResponse = await fetch("/api/simulation/enable", {method:"POST"});
    if (!enableResponse.ok) return alert("Activation simulation refusee: " + await enableResponse.text());
  }
  var modeResponse = await fetch("/api/simulation/mode", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:new URLSearchParams({mode:"manual"})});
  if (!modeResponse.ok) return alert("Mode simulation refuse: " + await modeResponse.text());
  var body = {
    jsy: {available:true, gridPowerW:Number($("grid").value), voltageV:Number($("voltage").value), currentA:Number($("current").value), activePowerW1:Number($("jsyP1").value), activePowerW2:Number($("jsyP2").value), currentA1:Number($("jsyC1").value), currentA2:Number($("jsyC2").value), powerFactor:0.96, frequencyHz:50},
    tic: {available:true, apparentPowerVA:900, currentA:Number($("current").value), tariff:"BASE"},
    ds18b20: [
      {id:"sonde1", available:true, temperatureC:Number($("t1").value)},
      {id:"sonde2", available:true, temperatureC:Number($("t2").value)},
      {id:"sonde3", available:true, temperatureC:Number($("t3").value)}
    ]
  };
  var response = await postJson("/api/simulation/values", body);
  if (!response.ok) return alert("Simulation refusee: " + await response.text());
  await refresh();
}

async function settingsPage() {
  cache.device = await api("/api/device");
  cache.system = await api("/api/system");
  var d = cache.device;
  var s = cache.system;
  var w = s.wifi || {};
  var r = s.router || {};
  var safe = s.safety || {};
  var auth = s.webAuth || {};
  $("app").innerHTML = banner() + '<h1>Parametres</h1>' + helpBox("settings") + '<div class="settingsGrid">' +
    '<section class="panel"><h2>Module</h2><div class="form">' +
      textField("devName", "Nom module", d.name || d.deviceName || "") +
      '<label>Role<select id="devRole">' + options(["MASTER","BACKUP","NODE_SENSOR","NODE_ACTUATOR","NODE_MIXED"], d.role || "MASTER") + '</select></label>' +
      selectField("devConfigured", "Configuration terminee", d.isConfigured !== false) +
    '</div></section>' +
    '<section class="panel"><h2>WiFi</h2><div class="form">' +
      textField("wifiSsid", "SSID WiFi", w.ssid || s.wifiSsid || "") +
      passwordField("wifiPassword", "Mot de passe WiFi") +
      selectField("keepAp", "AP local toujours actif", w.keepFallbackApAlwaysOn !== false) +
    '</div>' + miniState("IP box", state.stationIp || state.localIp || "-", "info") + miniState("IP AP", state.apIp || "-", "info") + miniState("RSSI", state.rssi == null ? "-" : state.rssi + " dBm", state.wifiConnected ? "ok" : "warn") + '</section>' +
    '<section class="panel"><h2>Acces Web</h2><div class="form">' +
      selectField("webAuthEnabled", "Login active", auth.enabled !== false) +
      textField("webAuthUser", "Utilisateur", auth.username || "admin") +
      passwordField("webAuthPassword", "Mot de passe Web") +
    '</div><div class="warnBox">Par defaut: admin / routeur1234. Laisse le mot de passe vide pour conserver l actuel.</div></section>' +
    '<section class="panel"><h2>Securite</h2><div class="form">' +
      '<label>Mode Safety<select id="safetyMode">' + options(["strict", "warning_only", "missing_sensors_off", "off"], safetyModeFromConfig(safe)) + '</select></label>' +
      field("tankMax", "Temp max ballon C", r.tankMaxC || 65) +
      field("tankSafety", "Temp securite C", r.tempSafetyMaxC || r.tankSafetyC || 70) +
    '</div><div class="warnBox">Le mode Safety OFF desactive les coupures automatiques logiciel. A reserver aux tests sans charge 230 V.</div></section>' +
    '<section class="panel solarSettingsPanel"><h2>Routeur solaire</h2><div class="solarSettingsGrid">' +
      '<div class="subPanel"><h3>Source reseau</h3><div class="formGrid">' +
        '<label>Source puissance reseau<select id="gridPowerSource">' + options(["JSY", "TIC", "AUTO"], r.gridPowerSource || "JSY") + '</select></label>' +
        '<label>Mode routeur<select id="routerMode">' + options(["OFF", "AUTO", "FORCED"], r.mode || "AUTO") + '</select></label>' +
        field("gridSetpointW", "Consigne reseau W", r.gridSetpointW || 0) +
        field("deadbandW", "Deadband W", r.deadbandW || 30) +
      '</div><p class="muted">JSY = mesure rapide via pince. TIC = Linky si la trame donne une puissance exploitable. AUTO = TIC si disponible, sinon JSY.</p></div>' +
      '<div class="subPanel"><h3>Seuils injection</h3><div class="formGrid">' +
        field("minInjection", "Seuil demarrage injection W", r.minInjectionStartW || 200) +
        field("stopInjection", "Seuil arret injection W", r.stopBelowInjectionW || 80) +
        field("hysteresis", "Hysteresis W", r.hysteresisW || 50) +
        field("maxRamp", "Rampe max %/s", r.maxOutputRampPercentPerSecond || 15) +
      '</div></div>' +
      '<div class="subPanel pidPanel"><h3>Regulation PID</h3><div class="pidControls">' +
        selectField("pidEnabled", "PID actif", r.pidEnabled !== false) +
        sliderField("pidKp", "Kp", r.kp || r.pidKp || 0.08, 0, 1, 0.01) +
        sliderField("pidKi", "Ki", r.ki || r.pidKi || 0.01, 0, 0.2, 0.001) +
        sliderField("pidKd", "Kd", r.kd || r.pidKd || 0, 0, 1, 0.01) +
      '</div></div>' +
      '<div class="subPanel"><h3>Mesure et puissance</h3><div class="formGrid">' +
        field("linkyPf", "Facteur puissance Linky estime", r.linkyPowerFactorEstimate || 0.95) +
        field("alphaFilter", "Alpha filtre", r.alphaFilter || 0.25) +
        field("heaterMax", "Puissance chauffe-eau W", r.heaterMaxPowerW || r.ssr1MaxW || 1500) +
        field("jsyReadMs", "Cadence JSY ms", r.jsyReadIntervalMs || 100) +
      '</div></div>' +
    '</div></section>' +
    '</div><p><button onclick="saveSettings()">Sauvegarder</button> <button onclick="restartEsp()">Redemarrer ESP32</button> <button onclick="jsonEditor(\'system\')">JSON system</button> <button onclick="jsonEditor(\'device\')">JSON module</button></p>';
}

function safetyModeFromConfig(safe) {
  if (safe.enabled === false) return "off";
  if (safe.warningOnlyOnMissingSensors) return "warning_only";
  if (safe.blockOnMissingDs18b20 === false && safe.blockOnMissingTopSensor === false && safe.blockOnMissingJsy === false && safe.blockOnMissingJsyAndTic === false) return "missing_sensors_off";
  return "strict";
}

function applySafetyModeConfig(safe, mode) {
  safe.enabled = mode !== "off";
  safe.blockOnMissingDs18b20 = mode === "strict";
  safe.blockOnMissingTopSensor = mode === "strict";
  safe.blockOnMissingJsy = mode === "strict";
  safe.blockOnMissingJsyAndTic = mode === "strict";
  safe.warningOnlyOnMissingSensors = mode === "warning_only" || mode === "off";
}

function textField(id, label, value) {
  return '<label>' + esc(label) + '<input id="' + id + '" value="' + esc(value) + '"></label>';
}

function passwordField(id, label) {
  return '<label>' + esc(label) + '<input id="' + id + '" type="password" placeholder="laisser vide pour conserver"></label>';
}

function selectField(id, label, value) {
  return '<label class="toggleField"><span>' + esc(label) + '</span><input id="' + id + '" type="checkbox" ' + checked(value) + '><i></i><b data-on="Actif" data-off="Inactif"></b></label>';
}

function boolField(id) {
  var el = $(id);
  return el ? !!el.checked : false;
}

function sliderField(id, label, value, min, max, step) {
  return '<label class="sliderField"><span>' + esc(label) + '</span><div class="sliderRow"><input id="' + id + 'Range" type="range" min="' + esc(min) + '" max="' + esc(max) + '" step="' + esc(step) + '" value="' + esc(value) + '" oninput="syncSliderField(\'' + id + '\', true)"><input id="' + id + '" type="number" min="' + esc(min) + '" max="' + esc(max) + '" step="' + esc(step) + '" value="' + esc(value) + '" oninput="syncSliderField(\'' + id + '\', false)"></div></label>';
}

function syncSliderField(id, fromRange) {
  var range = $(id + "Range");
  var input = $(id);
  if (!range || !input) return;
  if (fromRange) input.value = range.value;
  else range.value = input.value;
}

async function saveSettings() {
  var s = cache.system;
  var d = cache.device;
  s.wifi = s.wifi || {};
  s.router = s.router || {};
  s.safety = s.safety || {};
  s.simulation = s.simulation || {};
  s.webAuth = s.webAuth || {};
  d.name = $("devName").value;
  d.deviceName = d.name;
  d.role = $("devRole").value;
  d.isConfigured = boolField("devConfigured");
  s.wifi.ssid = $("wifiSsid").value;
  s.wifiSsid = s.wifi.ssid;
  if ($("wifiPassword").value) {
    s.wifi.password = $("wifiPassword").value;
    s.wifiPassword = s.wifi.password;
  }
  s.wifi.keepFallbackApAlwaysOn = boolField("keepAp");
  s.webAuth.enabled = boolField("webAuthEnabled");
  s.webAuth.username = $("webAuthUser").value || "admin";
  if ($("webAuthPassword").value) s.webAuth.password = $("webAuthPassword").value;
  s.router.gridPowerSource = $("gridPowerSource").value;
  s.router.mode = $("routerMode").value;
  s.router.pidEnabled = boolField("pidEnabled");
  s.router.gridSetpointW = Number($("gridSetpointW").value);
  s.router.deadbandW = Number($("deadbandW").value);
  s.router.linkyPowerFactorEstimate = Number($("linkyPf").value);
  s.router.alphaFilter = Number($("alphaFilter").value);
  s.router.maxOutputRampPercentPerSecond = Number($("maxRamp").value);
  s.router.heaterMaxPowerW = Number($("heaterMax").value);
  s.router.ssr1MaxW = s.router.heaterMaxPowerW;
  s.router.jsyReadIntervalMs = Number($("jsyReadMs").value);
  s.router.minInjectionStartW = Number($("minInjection").value);
  s.router.stopBelowInjectionW = Number($("stopInjection").value);
  s.router.hysteresisW = Number($("hysteresis").value);
  s.router.kp = Number($("pidKp").value);
  s.router.ki = Number($("pidKi").value);
  s.router.kd = Number($("pidKd").value);
  s.router.pidKp = s.router.kp;
  s.router.pidKi = s.router.ki;
  s.router.pidKd = s.router.kd;
  s.router.tankMaxC = Number($("tankMax").value);
  s.router.tempSafetyMaxC = Number($("tankSafety").value);
  s.router.tankSafetyC = s.router.tempSafetyMaxC;
  applySafetyModeConfig(s.safety, $("safetyMode").value);
  var rd = await postJson("/api/device", d);
  if (!rd.ok) return alert("Sauvegarde module refusee: " + await rd.text());
  var response = await postJson("/api/system", s);
  if (!response.ok) return alert("Sauvegarde systeme refusee: " + await response.text());
  cache.device = d;
  cache.system = s;
  state.moduleName = d.deviceName || d.name || state.moduleName;
  state.role = d.role || state.role;
  state.gridPowerSource = s.router.gridPowerSource || state.gridPowerSource;
  alert("Parametres sauvegardes");
  await refresh();
}

async function restartEsp() {
  if (!confirm("Redemarrer ESP32 maintenant ?")) return;
  await fetch("/api/system/reboot", {method:"POST"});
  $("app").innerHTML = '<h1>Redemarrage...</h1><div class="panel">Attends quelques secondes puis recharge /app.</div>';
}

async function mqttPage() {
  cache.system = await api("/api/system");
  var s = cache.system;
  var mqtt = s.mqtt || {};
  var topics = mqtt.topics || {};
  var baseTopic = mqtt.baseTopic || "routeurSolaire";
  $("app").innerHTML = banner() + '<h1>MQTT / Jeedom</h1>' + helpBox("mqtt") +
    '<div class="settingsGrid">' +
      '<section class="panel"><h2>Broker Jeedom</h2><div class="form">' +
        selectField("mqttEnabled", "MQTT active", mqtt.enabled === true) +
        textField("mqttHost", "Adresse broker", mqtt.host || "192.168.0.48") +
        field("mqttPort", "Port", mqtt.port || 1883) +
        textField("mqttClientId", "Client ID", mqtt.clientId || "RouteurSolaireESP32") +
        textField("mqttBaseTopic", "Topic de base", mqtt.baseTopic || "routeurSolaire") +
      '</div></section>' +
      '<section class="panel"><h2>Authentification</h2><div class="form">' +
        textField("mqttUsername", "Utilisateur", mqtt.username || "") +
        passwordField("mqttPassword", "Mot de passe MQTT") +
        selectField("mqttRetain", "Retain", mqtt.retain === true) +
        selectField("mqttIndividual", "Topics individuels Jeedom", mqtt.publishIndividualTopics !== false) +
        field("mqttPublishInterval", "Publication toutes les ms", mqtt.publishIntervalMs || 5000) +
      '</div><div class="warnBox">Ne mets pas de mot de passe si ton broker Jeedom n en demande pas. Le champ reste vide volontairement.</div></section>' +
      '<section class="panel"><h2>Topics programmables</h2><div class="form">' +
        textField("mqttStateTopic", "JSON etat", topics.state || baseTopic + "/state") +
        textField("mqttCommandTopic", "Commande JSON", topics.command || baseTopic + "/command") +
        textField("mqttActuatorTopic", "Commande actionneur", topics.actuatorSet || baseTopic + "/actuator/+/set") +
        textField("mqttAvailabilityTopic", "Disponibilite", topics.availability || baseTopic + "/availability") +
      '</div></section>' +
      '<section class="panel"><h2>Etat actuel</h2>' +
        miniState("MQTT", state.mqttStatus || (mqtt.enabled ? "active" : "desactive"), state.mqttConnected ? "ok" : (mqtt.enabled ? "warn" : "muted")) +
        miniState("Broker", (mqtt.host || "192.168.0.48") + ":" + (mqtt.port || 1883), "info") +
        miniState("WiFi", state.wifiConnected ? "connecte" : "AP/local", state.wifiConnected ? "ok" : "warn") +
        miniState("Derniere publication", state.lastMqttPublishAgeMs && state.lastMqttPublishAgeMs < 4294967295 ? Math.round(state.lastMqttPublishAgeMs / 1000) + " s" : "-", "info") +
        '<p class="muted">Exemple commande JSON: {"actuatorId":"ssr1_water_heater","command":"setActuatorPercent","value":25}</p>' +
        '<p class="muted">Exemple topic direct: ' + esc(baseTopic) + '/actuator/ssr1_water_heater/set avec payload 25.</p>' +
      '</section>' +
    '</div><p><button onclick="saveMqtt()">Sauvegarder MQTT</button> <button onclick="jsonEditor(\'system\')">JSON system</button></p>';
}

async function saveMqtt() {
  var s = cache.system || await api("/api/system");
  s.mqtt = s.mqtt || {};
  s.mqtt.enabled = boolField("mqttEnabled");
  s.mqtt.host = $("mqttHost").value || "192.168.0.48";
  s.mqtt.port = Number($("mqttPort").value) || 1883;
  s.mqtt.clientId = $("mqttClientId").value || "RouteurSolaireESP32";
  s.mqtt.baseTopic = $("mqttBaseTopic").value || "routeurSolaire";
  s.mqtt.username = $("mqttUsername").value || "";
  s.mqtt.retain = boolField("mqttRetain");
  s.mqtt.publishIndividualTopics = boolField("mqttIndividual");
  s.mqtt.publishIntervalMs = Number($("mqttPublishInterval").value) || 5000;
  s.mqtt.topics = s.mqtt.topics || {};
  s.mqtt.topics.state = $("mqttStateTopic").value || s.mqtt.baseTopic + "/state";
  s.mqtt.topics.command = $("mqttCommandTopic").value || s.mqtt.baseTopic + "/command";
  s.mqtt.topics.actuatorSet = $("mqttActuatorTopic").value || s.mqtt.baseTopic + "/actuator/+/set";
  s.mqtt.topics.availability = $("mqttAvailabilityTopic").value || s.mqtt.baseTopic + "/availability";
  if ($("mqttPassword").value) s.mqtt.password = $("mqttPassword").value;
  var response = await postJson("/api/system", s);
  if (!response.ok) return alert("Sauvegarde MQTT refusee: " + await response.text());
  cache.system = s;
  alert("Configuration MQTT sauvegardee");
  await refresh();
}

function tipValueClass(value) {
  var text = String(value == null ? "" : value).toUpperCase();
  if (text === "OK" || text === "TRUE" || text === "ACTIF") return "ok";
  if (text.indexOf("ABSENT") >= 0 || text.indexOf("TIMEOUT") >= 0 || text.indexOf("ERREUR") >= 0) return "bad";
  if (text === "N/A" || text === "-" || text.indexOf("NON LU") >= 0) return "warn";
  return "info";
}

function tipLine(label, value, unit, cls) {
  return '<div><span>' + esc(label) + '</span><b class="' + esc(cls || tipValueClass(value)) + '">' + esc(fmt(value)) + (unit ? ' <em>' + esc(unit) + '</em>' : '') + '</b></div>';
}

function sensorHoverTip(kind, index) {
  if (kind === "jsy") {
    return tipLine("Etat", state.jsyOnline ? "OK" : "Absent", "", state.jsyOnline ? "ok" : "bad") +
      tipLine("Source routeur", state.gridPowerSource || "N/A", "") +
      tipLine("Puissance reseau", state.jsyGridPowerW, "W") +
      tipLine(jsyChannelName(0), state.activePowerW1, "W") +
      tipLine("Voie 1 courant", state.currentA1, "A") +
      tipLine("Voie 1 tension", state.voltageV1, "V") +
      tipLine(jsyChannelName(1), state.activePowerW2, "W") +
      tipLine("Voie 2 courant", state.currentA2, "A") +
      tipLine("Voie 2 tension", state.voltageV2, "V") +
      tipLine("Frequence", state.gridFrequencyHz, "Hz");
  }
  if (kind === "tic") {
    return tipLine("Etat", state.ticStatus || "N/A", "", state.ticAvailable ? "ok" : "warn") +
      tipLine("Puissance", state.ticGridPowerW, "W") +
      tipLine("Apparente", state.ticApparentPowerVA, "VA") +
      tipLine("Courant", state.ticCurrentA, "A") +
      tipLine("Index", state.ticEnergyWh, "Wh") +
      tipLine("Tarif", state.ticTariff || "N/A", "") +
      tipLine("Periode", state.ticPeriod || "N/A", "") +
      tipLine("Derniere lecture", state.lastTicReadMs || "N/A", state.lastTicReadMs ? "ms" : "") +
      tipLine("Erreurs", state.ticErrorCount, "");
  }
  var labels = ["Sonde 1", "Sonde 2", "Sonde 3"];
  var roles = ["ballon haut", "ballon milieu", "ballon bas"];
  var temps = [state.tankTopC, state.tankMiddleC, state.tankBottomC];
  var available = dsAvailable(index, temps[index]);
  return tipLine("Etat", available ? "OK" : "Absent / non lu", "", available ? "ok" : "bad") +
    tipLine("Role", roles[index] || "DS18B20", "") +
    tipLine("Temperature", available ? temps[index] : "N/A", available ? "C" : "") +
    tipLine("Nom", labels[index] || "Sonde", "");
}

function diagnosticPage() {
  var events = state.events || [];
  var dsStates = [
    dsConfigured(0) ? miniState("Sonde 1", dsAvailable(0, state.tankTopC) ? fmt(state.tankTopC) + " C" : "Absent / non lu", dsAvailable(0, state.tankTopC) ? tempClass(state.tankTopC) : "bad", sensorHoverTip("ds", 0)) : "",
    dsConfigured(1) ? miniState("Sonde 2", dsAvailable(1, state.tankMiddleC) ? fmt(state.tankMiddleC) + " C" : "Absent / non lu", dsAvailable(1, state.tankMiddleC) ? tempClass(state.tankMiddleC) : "bad", sensorHoverTip("ds", 1)) : "",
    dsConfigured(2) ? miniState("Sonde 3", dsAvailable(2, state.tankBottomC) ? fmt(state.tankBottomC) + " C" : "Absent / non lu", dsAvailable(2, state.tankBottomC) ? tempClass(state.tankBottomC) : "bad", sensorHoverTip("ds", 2)) : ""
  ].join("") || miniState("DS18B20", "Aucune sonde configuree", "muted");
  $("app").innerHTML = banner() + '<h1>Diagnostic & Simulation</h1>' + helpBox("diagnostic") + '<div class="settingsGrid">' +
    '<section class="panel"><h2>Diagnostic systeme</h2>' +
      miniState("Securite", state.safetyLevel || "-", state.safetyTripped ? "bad" : "ok") +
      miniState("Raison", state.safetyReason || "aucune", state.safetyTripped ? "bad" : "muted") +
      miniState("WiFi", state.wifiConnected ? "connecte" : "AP/local", state.wifiConnected ? "ok" : "warn") +
      miniState("Mode reseau", state.networkMode || "-", "info") +
      miniState("Heap libre", state.heapFree ? Math.round(state.heapFree / 1024) + " Ko" : "-", "ok") +
      '<p><a href="/fs">Voir LittleFS</a> <a href="/api/diagnostic">JSON diagnostic</a></p>' +
    '</section>' +
    '<section class="panel"><h2>Capteurs</h2>' +
      miniState("JSY-MK-194T", state.jsyOnline ? "OK" : "Absent", state.jsyOnline ? "ok" : "bad", sensorHoverTip("jsy")) +
      miniState("TIC Linky", state.ticAvailable ? "OK" : "Absent", state.ticAvailable ? "ok" : "warn", sensorHoverTip("tic")) +
      dsStates +
    '</section>' +
    '<section class="panel"><h2>Simulation</h2>' + simulationControlsHtml() + '</section>' +
    '<section class="panel"><h2>Sorties calculees</h2>' +
      actuatorBar("SSR1", state.ssr1PowerPct) +
      actuatorBar("SSR2", state.ssr2PowerPct) +
      actuatorBar("RobotDyn", state.robotDynPowerPct) +
      '<p class="muted">En simulation, les sorties 230 V restent forcees OFF.</p>' +
    '</section>' +
    '</div><h2>Evenements</h2><p><button onclick="clearLogs()">Reset logs</button></p><table><tr><th>ms</th><th>Niveau</th><th>Code</th><th>Message</th></tr>' +
    events.map(function (event) {
      return '<tr><td>' + esc(event.timestampMs) + '</td><td>' + esc(event.level) + '</td><td>' + esc(event.code) + '</td><td>' + esc(event.message) + '</td></tr>';
    }).join("") + '</table>';
}

function gpioRowsFromConfig() {
  var rows = [];
  function push(owner, field, gpio, detail) {
    if (gpio == null || gpio === "" || Number(gpio) < 0) return;
    rows.push({gpio:Number(gpio), owner:owner, field:field, detail:detail || ""});
  }
  var sensors = (cache.sensors && cache.sensors.sensors) || [];
  sensors.forEach(function (s) {
    var name = s.name || s.id || "capteur";
    push(name, "GPIO", s.gpio, s.type || "");
    push(name, "RX", s.rx, s.type || "");
    push(name, "TX", s.tx, s.type || "");
  });
  var bus = (cache.sensors && cache.sensors.oneWireBus) || {};
  if ((cache.sensors && cache.sensors.ds18b20 || []).length || bus.gpio != null) push("Bus OneWire", "GPIO", bus.gpio == null ? 13 : bus.gpio, "DS18B20");
  var actuators = (cache.actuators && cache.actuators.actuators) || [];
  actuators.forEach(function (a) {
    var name = a.name || a.id || "actionneur";
    push(name, "GPIO", a.gpio, a.type || "");
    push(name, "Zero-cross", a.zeroCross, a.type || "");
    push(name, "Control", a.control, a.type || "");
    push(name, "GPIO zero-cross", a.gpioZeroCross, a.type || "");
    push(name, "GPIO control", a.gpioControl, a.type || "");
  });
  rows.sort(function (a, b) { return a.gpio - b.gpio || a.owner.localeCompare(b.owner); });
  return rows;
}

async function systemPage() {
  var sys = await api("/api/system-info");
  if (!cache.sensors) cache.sensors = await api("/api/sensors");
  if (!cache.actuators) cache.actuators = await api("/api/actuators");
  var gpioRows = gpioRowsFromConfig();
  var storage = sys.storage || {};
  var services = sys.services || {};
  var solar = sys.solarRouter || {};
  var ota = sys.ota || {};
  function otaSlotCard(title, slot) {
    slot = slot || {};
    var version = slot.routeurVersion || slot.version || "N/A";
    return '<div class="miniCard ' + (slot.running ? 'okCard' : '') + '">' +
      '<div class="miniTitle">' + esc(title) + (slot.running ? ' <span class="badge ok">ACTIVE</span>' : '') + '</div>' +
      '<div class="miniValue">' + esc(version) + '</div>' +
      '<div class="miniSub">' + esc(slot.label || "N/A") + ' - ' + (slot.valid ? 'image valide' : 'vide / invalide') + '</div>' +
      '<div class="miniSub">Taille slot: ' + bytesHuman(slot.size) + '</div>' +
      '<div class="miniSub">Adresse: ' + (slot.address == null ? 'N/A' : '0x' + Number(slot.address).toString(16)) + '</div>' +
    '</div>';
  }
  $("app").innerHTML = banner() + '<h1>Systeme</h1><div class="toolbar"><button onclick="refresh()">Actualiser</button><button class="danger" onclick="restartSystemPage()">Redemarrer ESP32</button><a href="/api/system-info">JSON systeme</a></div><div class="settingsGrid">' +
    dashboardBlock("Module ESP", "carte, firmware et uptime",
      '<div class="blockMetricGrid">' +
        blockMetric("Nom", sys.deviceName || "N/A", "", "info", sys.role || "N/A") +
        blockMetric("Firmware", sys.firmwareVersion || "N/A", "", "muted", "") +
        blockMetric("Version build", sys.buildVersion || "N/A", "", "info", "format YYYYMMDD-NN") +
        blockMetric("Compilation", sys.buildDate || "N/A", "", "muted", "") +
        blockMetric("Build timestamp", sys.buildTimestamp || "N/A", "", "muted", "") +
        blockMetric("Uptime", uptimeHuman(sys.uptime), "", "ok", "") +
      '</div>', "identity") +
    dashboardBlock("Reseau WiFi", "connectivite locale",
      '<div class="blockMetricGrid">' +
        blockMetric("SSID", sys.ssid || "N/A", "", sys.ssid && sys.ssid !== "N/A" ? "ok" : "muted", "") +
        blockMetric("RSSI", sys.rssi == null ? "N/A" : sys.rssi, sys.rssi == null ? "" : "dBm", stateClass(sys.wifiQuality), sys.wifiQuality || "N/A") +
        blockMetric("Qualite WiFi", sys.wifiQuality || "N/A", "", stateClass(sys.wifiQuality), "") +
        blockMetric("Mode", sys.networkMode || "N/A", "", "info", "") +
        blockMetric("IP active", sys.ip || "N/A", "", "info", "") +
        blockMetric("IP station", sys.stationIp || "N/A", "", "info", "") +
        blockMetric("IP AP", sys.apIp || "N/A", "", "info", "") +
        blockMetric("NTP", sys.localDateTime || "N/A", "", sys.ntpSynced ? "ok" : "warn", sys.ntpStatus || "N/A") +
        blockMetric("MAC WiFi", sys.mac || "N/A", "", "muted", "") +
      '</div>', "network") +
    dashboardBlock("OTA firmware", "app active et prochain slot",
      '<div class="blockMetricGrid">' +
        blockMetric("Lancee depuis", ota.runningSlot || "N/A", "", "ok", ota.runningLabel || "") +
        blockMetric("Boot configure", ota.bootSlot || "N/A", "", "info", ota.bootLabel || "") +
        blockMetric("Prochaine MAJ", ota.nextUpdateSlot || "N/A", "", "warn", ota.nextUpdateLabel || "") +
        blockMetric("Taille slot", bytesHuman(ota.runningSlotSize), "", "info", "partition app") +
        blockMetric("Firmware utilise", bytesHuman(ota.sketchSize), "", "muted", "") +
        blockMetric("Reste disponible", bytesHuman(ota.runningRemainingBytes), "", ota.runningRemainingBytes > 250000 ? "ok" : "warn", "marge firmware") +
      '</div><div class="miniGrid">' +
        otaSlotCard("APP1 / OTA_0", ota.app0) +
        otaSlotCard("APP2 / OTA_1", ota.app1) +
      '</div><p><button class="danger" onclick="rollbackFirmware()">Rollback firmware</button></p><p class="muted">Redemarre sur l autre partition OTA si elle contient une image valide.</p>', "firmware") +
    dashboardBlock("Partition LittleFS", "stockage interface et configuration",
      '<div class="blockMetricGrid">' +
        blockMetric("Type", storage.type || "N/A", "", "info", "") +
        blockMetric("Version", storage.version || "N/A", "", "info", "LittleFS") +
        blockMetric("Etat", storage.status || "N/A", "", stateClass(storage.status), "") +
        blockMetric("Utilise", bytesHuman(storage.used), "", "info", "") +
        blockMetric("Total", bytesHuman(storage.total), "", "ok", "") +
      '</div><p class="muted"><a href="/fs">Voir les fichiers LittleFS</a></p>', "storage") +
    dashboardBlock("Memoire / CPU", "ressources ESP32",
      '<div class="blockMetricGrid">' +
        blockMetric("RAM libre", bytesHuman(sys.freeHeap), "", "ok", "") +
        blockMetric("RAM mini observee", bytesHuman(sys.minFreeHeap), "", "warn", "") +
        blockMetric("CPU", sys.cpuFreqMHz == null ? "N/A" : sys.cpuFreqMHz, sys.cpuFreqMHz == null ? "" : "MHz", "info", "") +
      '</div>', "memory") +
    dashboardBlock("Redemarrage / stabilite", "diagnostic boot",
      '<div class="blockMetricGrid">' +
        blockMetric("Cause reset", sys.resetReason || "N/A", "", stateClass(sys.resetReason === "Power on" ? "OK" : "Attention"), "") +
        blockMetric("Uptime ms", sys.uptime || "N/A", "", "muted", "") +
        blockMetric("Safety", sys.safetyLevel || "N/A", "", stateClass(sys.safetyLevel), sys.safetyReason || "N/A") +
      '</div>', "stability") +
    dashboardBlock("Services", "etat general",
      '<div class="statusMiniGrid">' +
        statusMini("WiFi", services.wifi) +
        statusMini("NTP", services.ntp) +
        statusMini("MQTT", services.mqtt) +
        statusMini("Capteurs", services.sensors) +
        statusMini("ESP-NOW", services.espnow) +
        statusMini("Safety", services.safety) +
      '</div>', "services") +
    dashboardBlock("Routeur solaire", "mesures principales",
      '<div class="blockMetricGrid">' +
        blockMetric("Mode", solar.mode || "N/A", "", "info", solar.gridPowerSource || "") +
        blockMetric("Puissance", solar.power == null ? "N/A" : solar.power, solar.power == null ? "" : "W", Number(solar.power) < 0 ? "solar" : "consume", "") +
        blockMetric("Sortie", solar.outputPercent == null ? "N/A" : solar.outputPercent, solar.outputPercent == null ? "" : "%", Number(solar.outputPercent) > 0 ? "ok" : "muted", "SSR / triac") +
        blockMetric("Temperature", solar.temperature == null ? "N/A" : solar.temperature, typeof solar.temperature === "number" ? "C" : "", typeof solar.temperature === "number" ? tempClass(solar.temperature) : "muted", "") +
        blockMetric("Derniere mesure", solar.lastMeasureAge == null ? "N/A" : solar.lastMeasureAge, typeof solar.lastMeasureAge === "number" ? "s" : "", "info", "") +
        blockMetric("SSR1 / SSR2 / Triac", fmt(solar.ssr1Percent) + " / " + fmt(solar.ssr2Percent) + " / " + fmt(solar.robotDynPercent), "%", "muted", "") +
      '</div>', "solar") +
    dashboardBlock("Actions systeme", "commandes locales",
      '<p><button class="danger" onclick="restartSystemPage()">Redemarrer ESP32</button></p><p class="muted">Le redemarrage demande une confirmation dans le navigateur. Aucun mot de passe WiFi n est affiche sur cette page.</p>', "actions") +
    '</div><h2>GPIO utilises</h2><table><tr><th>GPIO</th><th>Usage</th><th>Champ</th><th>Detail</th></tr>' +
      (gpioRows.length ? gpioRows.map(function (row) {
        return '<tr><td>GPIO' + esc(row.gpio) + '</td><td>' + esc(row.owner) + '</td><td>' + esc(row.field) + '</td><td>' + esc(row.detail) + '</td></tr>';
      }).join("") : '<tr><td colspan="4">Aucun GPIO configure.</td></tr>') +
    '</table>';
}

async function restartSystemPage() {
  if (!confirm("Redemarrer ESP32 maintenant ?")) return;
  await fetch("/api/restart", {method:"POST"});
  $("app").innerHTML = '<h1>Redemarrage...</h1><div class="panel">Attends quelques secondes puis recharge /app.</div>';
}

async function rollbackFirmware() {
  if (!confirm("Redemarrer sur l autre partition firmware ?")) return;
  var response = await fetch("/api/ota/rollback", {method:"POST"});
  if (!response.ok) return alert("Rollback refuse: " + await response.text());
  $("app").innerHTML = '<h1>Rollback firmware...</h1><div class="panel">L ESP32 redemarre sur l autre partition OTA. Attends quelques secondes puis recharge /app.</div>';
}

function simulationControlsHtml() {
  var scenarios = [
    ["normal", "Retour normal"],
    ["production_low", "Production faible"],
    ["injection_medium", "Injection moyenne"],
    ["injection_high", "Forte injection"],
    ["tank_almost_hot", "Ballon presque chaud"],
    ["tank_overheat", "Surchauffe ballon"],
    ["critical_sensor_lost", "Perte capteur critique"],
    ["jsy_lost", "Perte JSY"]
  ];
  return '<div class="blockStates">' +
      blockState("Etat", state.simulationMode ? "active" : "inactive", state.simulationMode ? "warn" : "muted") +
      blockState("Temps restant", state.simulationMode ? simRemainingText() : "-", state.simulationMode ? "warn" : "muted") +
      blockState("Mode", state.simulationType || "manual", "info") +
      blockState("Scenario", state.simulationScenario || "normal", "info") +
    '</div>' +
    '<div class="form simForm">' +
    '<label>Mode<select id="simMode">' + options(["manual", "random", "scenario"], state.simulationType || "manual") + '</select></label>' +
    '<label>Scenario<select id="simScenario">' + scenarios.map(function (item) { return '<option value="' + esc(item[0]) + '" ' + ((state.simulationScenario || "normal") === item[0] ? "selected" : "") + '>' + esc(item[1]) + '</option>'; }).join("") + '</select></label>' +
    field("grid", "gridPowerW", state.gridPowerW || -800) +
    field("voltage", "Tension V", 231) +
    field("current", "Courant A", 4) +
    field("jsyP1", "JSY puissance active 1 W", state.activePowerW1 || state.gridPowerW || -800) +
    field("jsyC1", "JSY courant 1 A", state.currentA1 || 4) +
    field("jsyP2", "JSY puissance active 2 W", state.activePowerW2 || 0) +
    field("jsyC2", "JSY courant 2 A", state.currentA2 || 0) +
    field("t1", "Sonde 1 C", state.tankTopC || 45) +
    field("t2", "Sonde 2 C", state.tankMiddleC || 42) +
    field("t3", "Sonde 3 C", state.tankBottomC || 38) +
    '</div><div class="toolbar">' +
      '<button onclick="simEnable()">Activer</button>' +
      '<button onclick="simModeApply()">Appliquer mode</button>' +
      '<button onclick="simScenarioApply()">Appliquer scenario</button>' +
      '<button onclick="simValues()">Appliquer manuel</button>' +
      '<button onclick="simRandom()">Aleatoire</button>' +
      '<button onclick="simScenario(\'tank_overheat\')">Surchauffe</button>' +
      '<button onclick="simScenario(\'jsy_lost\')">Perte JSY</button>' +
      '<button class="danger" onclick="simDisable()">Desactiver</button>' +
    '</div><p class="muted">La simulation se coupe automatiquement apres 5 minutes et ne reste pas active apres reboot. Les sorties 230 V restent OFF.</p>';
}

async function clearLogs() {
  await fetch("/api/logs/clear", {method:"POST"});
  await refresh();
}

async function jsonEditor(name) {
  var data = await api("/api/" + name);
  $("app").innerHTML = '<h1>JSON ' + esc(name) + '</h1><p><button onclick="saveJson(\'' + esc(name) + '\')">Sauvegarder</button> <button onclick="refresh()">Retour</button></p><textarea id="jsonText">' + esc(JSON.stringify(data, null, 2)) + '</textarea>';
}

async function saveJson(name) {
  var text = $("jsonText").value;
  var response = await fetch("/api/" + name, {method:"POST", body:text});
  alert(response.ok ? "Sauvegarde OK" : "Sauvegarde refusee: " + await response.text());
}

async function render() {
  updateNavStatus();
  if (page === "sensors") return sensorsPage();
  if (page === "actuators") return actuatorsPage();
  if (page === "logic") return logicPage();
  if (page === "settings") return settingsPage();
  if (page === "diagnostic") return diagnosticPage();
  if (page === "system") return systemPage();
  if (page === "mqtt") return mqttPage();
  $("app").innerHTML = dashboard();
  refreshLabelPatch();
}

Array.prototype.forEach.call(document.querySelectorAll("button[data-page]"), function (button) {
  button.addEventListener("click", function () {
    page = button.getAttribute("data-page");
    if (page !== "dashboard") stopGraphPolling();
    Array.prototype.forEach.call(document.querySelectorAll("button[data-page]"), function (item) {
      item.classList.toggle("active", item === button);
    });
    refresh();
    scheduleDashboardRefresh();
  });
});

document.addEventListener("mousemove", function (event) {
  if (event.target.closest && event.target.closest(".chartWithScale")) showChartTooltip(event);
  else hideChartTooltip();
});
document.addEventListener("mouseleave", hideChartTooltip);
document.addEventListener("click", function (event) {
  if (event.target.closest) flashButton(event.target.closest("button"));
});

var firstButton = document.querySelector('button[data-page="dashboard"]');
if (firstButton) firstButton.classList.add("active");

function scheduleDashboardRefresh() {
  if (dashboardRefreshTimer) clearInterval(dashboardRefreshTimer);
  dashboardRefreshTimer = null;
  if (dashboardRefreshMs > 0) {
    dashboardRefreshTimer = setInterval(function () {
      if (page === "dashboard" || page === "system") refresh();
    }, dashboardRefreshMs);
  }
  refreshLabelPatch();
}

render();
scheduleDashboardRefresh();
refresh();
