var page = "dashboard";
var dashboardRefreshMs = Number(localStorage.getItem("dashboardRefreshMs") || 5000);
var dashboardRefreshTimer = null;
var dashHistory = [];
var lastHistorySampleMs = 0;
var historySampleIntervalMs = 5000;
var historyMaxPoints = 360;
var dashboardMetricKeys = loadDashboardList("dashboardMetrics", ["gridPowerW", "injectionW", "surplusW", "ssr1PowerPct"]);
var dashboardChartKeys = loadDashboardList("dashboardCharts", ["injectionW", "ssr1PowerPct", "tankTopC"]);
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
  gridPowerSource: "JSY",
  jsyGridPowerW: null,
  ticGridPowerW: null,
  activePowerW1: 0,
  activePowerW2: 0,
  currentA1: 0,
  currentA2: 0,
  injectionW: 0,
  surplusW: 0,
  tankTopC: null,
  tankMiddleC: null,
  tankBottomC: null,
  ssr1PowerPct: 0,
  ssr2PowerPct: 0,
  robotDynPowerPct: 0,
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
  {id:"JSY-MK-194T", label:"JSY-MK-194T", measures:[["gridPowerW","number","W"],["injectionW","number","W"],["consumptionW","number","W"],["surplusW","number","W"],["voltageV","number","V"],["currentA","number","A"],["activePowerW","number","W"],["activePowerW1","number","W"],["activePowerW2","number","W"],["powerFactor","number",""],["frequencyHz","number","Hz"],["available","boolean",""]]},
  {id:"TIC Linky", label:"TIC Linky", measures:[["gridPowerW","number","W"],["apparentPowerVA","number","VA"],["currentA","number","A"],["tariff","text",""],["available","boolean",""],["lastValidReadAgeMs","number","ms"]]},
  {id:"DS18B20_TOP", label:"sonde1", measures:[["temperatureC","number","C"],["available","boolean",""],["lastValidReadAgeMs","number","ms"]]},
  {id:"DS18B20_MIDDLE", label:"sonde2", measures:[["temperatureC","number","C"],["available","boolean",""],["lastValidReadAgeMs","number","ms"]]},
  {id:"DS18B20_BOTTOM", label:"sonde3", measures:[["temperatureC","number","C"],["available","boolean",""],["lastValidReadAgeMs","number","ms"]]},
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

function miniState(label, value, cls) {
  return '<div class="miniState"><span>' + esc(label) + '</span><b class="' + esc(cls || "") + '">' + esc(value) + '</b></div>';
}

function bytesHuman(value) {
  value = Number(value) || 0;
  if (value >= 1048576) return Math.round(value / 104857.6) / 10 + " Mo";
  if (value >= 1024) return Math.round(value / 102.4) / 10 + " Ko";
  return value + " o";
}

function valueMissing(value) {
  return value == null || value === "" || (typeof value === "number" && !isFinite(value));
}

function sensorBadge(available, enabled) {
  if (enabled === false) return '<span class="badge muted">desactive</span>';
  return available ? '<span class="badge ok">OK</span>' : '<span class="badge bad">Erreur</span>';
}

function dsAvailable(index, temp) {
  if (Array.isArray(state.ds18b20Available)) return state.ds18b20Available[index] === true;
  return !valueMissing(temp);
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
  return [
    {key:"gridPowerW", label:"Puissance reseau", unit:"W", cls:Number(state.gridPowerW) < 0 ? "solar" : "consume", value:state.gridPowerW},
    {key:"injectionW", label:"Injection", unit:"W", cls:"solar", value:state.injectionW, min:0},
    {key:"surplusW", label:"Surplus", unit:"W", cls:"sun", value:state.surplusW, min:0},
    {key:"ssr1PowerPct", label:"SSR1", unit:"%", cls:"heat", value:state.ssr1PowerPct, min:0, max:100},
    {key:"ssr2PowerPct", label:"SSR2", unit:"%", cls:"info", value:state.ssr2PowerPct, min:0, max:100},
    {key:"robotDynPowerPct", label:"RobotDyn", unit:"%", cls:"warn", value:state.robotDynPowerPct, min:0, max:100},
    {key:"tankTopC", label:"Sonde 1", unit:"C", cls:tempClass(state.tankTopC), value:dsAvailable(0, state.tankTopC) ? state.tankTopC : null, min:0, max:80},
    {key:"tankMiddleC", label:"Sonde 2", unit:"C", cls:tempClass(state.tankMiddleC), value:dsAvailable(1, state.tankMiddleC) ? state.tankMiddleC : null, min:0, max:80},
    {key:"tankBottomC", label:"Sonde 3", unit:"C", cls:tempClass(state.tankBottomC), value:dsAvailable(2, state.tankBottomC) ? state.tankBottomC : null, min:0, max:80},
    {key:"heapFree", label:"Heap libre", unit:"o", cls:"ok", value:state.heapFree, min:0}
  ];
}

function seriesByKey(key) {
  var list = dashboardSeries();
  for (var i = 0; i < list.length; i++) if (list[i].key === key) return list[i];
  return list[0];
}

function seriesOptions(selected) {
  return dashboardSeries().map(function (item) {
    return '<option value="' + esc(item.key) + '" ' + (item.key === selected ? "selected" : "") + '>' + esc(item.label) + '</option>';
  }).join("");
}

function dashboardCustomizeBox() {
  var metricControls = [0,1,2,3].map(function (i) {
    return '<label>Bloc ' + (i + 1) + '<select id="metric' + i + '">' + seriesOptions(dashboardMetricKeys[i]) + '</select></label>';
  }).join("");
  var chartControls = [0,1,2].map(function (i) {
    return '<label>Courbe ' + (i + 1) + '<select id="chart' + i + '">' + seriesOptions(dashboardChartKeys[i]) + '</select></label>';
  }).join("");
  return '<details class="panel dashCustom"><summary>Personnaliser les blocs et courbes</summary>' +
    '<div class="customSection"><h2>Blocs valeur</h2><div class="customGrid">' + metricControls + '</div></div>' +
    '<div class="customSection"><h2>Courbes</h2><div class="customGrid">' + chartControls + '</div></div>' +
    '<p class="customActions"><button onclick="saveDashboardDisplay()">Appliquer</button> <button onclick="resetDashboardDisplay()">Par defaut</button></p></details>';
}

function saveDashboardDisplay() {
  dashboardMetricKeys = [0,1,2,3].map(function (i) { return $("metric" + i).value; });
  dashboardChartKeys = [0,1,2].map(function (i) { return $("chart" + i).value; });
  localStorage.setItem("dashboardMetrics", JSON.stringify(dashboardMetricKeys));
  localStorage.setItem("dashboardCharts", JSON.stringify(dashboardChartKeys));
  render();
}

function resetDashboardDisplay() {
  dashboardMetricKeys = ["gridPowerW", "injectionW", "surplusW", "ssr1PowerPct"];
  dashboardChartKeys = ["injectionW", "ssr1PowerPct", "tankTopC"];
  localStorage.setItem("dashboardMetrics", JSON.stringify(dashboardMetricKeys));
  localStorage.setItem("dashboardCharts", JSON.stringify(dashboardChartKeys));
  render();
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
  var middle = min < 0 && max > 0 ? 0 : (min + max) / 2;
  return '<div class="chartScale"><span>' + esc(scaleLabel(max, unit)) + '</span><span>' + esc(scaleLabel(middle, unit)) + '</span><span>' + esc(scaleLabel(min, unit)) + '</span></div>';
}

function sparkline(key, cls, minFixed, maxFixed, unit) {
  if (dashHistory.length < 2) return '<div class="sparkline muted">historique en cours...</div>';
  var values = dashHistory.map(function (p) { return p[key]; }).filter(function (v) { return v != null && isFinite(v); });
  if (values.length < 2) return '<div class="sparkline muted">pas de valeur</div>';
  var min = minFixed != null ? minFixed : Math.min.apply(null, values);
  var max = maxFixed != null ? maxFixed : Math.max.apply(null, values);
  if (max === min) max = min + 1;
  var w = 180, h = 52;
  var zeroY = min < 0 && max > 0 ? Math.round(h - ((0 - min) * h / (max - min))) : -1;
  var grid = '<line x1="0" y1="0" x2="' + w + '" y2="0"></line><line x1="0" y1="' + Math.round(h / 2) + '" x2="' + w + '" y2="' + Math.round(h / 2) + '"></line><line x1="0" y1="' + h + '" x2="' + w + '" y2="' + h + '"></line>';
  if (zeroY >= 0) grid += '<line class="zeroLine" x1="0" y1="' + zeroY + '" x2="' + w + '" y2="' + zeroY + '"></line>';
  var points = values.map(function (v, i) {
    var x = values.length === 1 ? 0 : (i * w / (values.length - 1));
    var y = h - ((v - min) * h / (max - min));
    return Math.round(x) + "," + Math.round(y);
  }).join(" ");
  return '<div class="chartWithScale" data-chart-key="' + esc(key) + '" data-chart-unit="' + esc(unit || "") + '">' + chartAxis(min, max, unit) + '<svg class="sparkline ' + esc(cls || "") + '" viewBox="0 0 ' + w + ' ' + h + '" preserveAspectRatio="none"><g class="grid">' + grid + '</g><polyline points="' + points + '"></polyline></svg></div>';
}

function multiSparkline(series, unit) {
  if (dashHistory.length < 2) return '<div class="sparkline energySpark muted">historique en cours...</div>';
  var all = [];
  series.forEach(function (s) {
    dashHistory.forEach(function (p) {
      var v = p[s.key];
      if (v != null && isFinite(v)) all.push(Number(v));
    });
  });
  if (all.length < 2) return '<div class="sparkline energySpark muted">pas de valeur</div>';
  var min = Math.min.apply(null, all);
  var max = Math.max.apply(null, all);
  if (min > 0) min = 0;
  if (max === min) max = min + 1;
  var w = 320, h = 96;
  var zeroY = min < 0 && max > 0 ? Math.round(h - ((0 - min) * h / (max - min))) : -1;
  var grid = '<line x1="0" y1="0" x2="' + w + '" y2="0"></line><line x1="0" y1="' + Math.round(h / 2) + '" x2="' + w + '" y2="' + Math.round(h / 2) + '"></line><line x1="0" y1="' + h + '" x2="' + w + '" y2="' + h + '"></line>';
  if (zeroY >= 0) grid += '<line class="zeroLine" x1="0" y1="' + zeroY + '" x2="' + w + '" y2="' + zeroY + '"></line>';
  var lines = series.map(function (s) {
    var values = dashHistory.map(function (p) { return p[s.key]; }).filter(function (v) { return v != null && isFinite(v); });
    if (values.length < 2) return "";
    var points = values.map(function (v, i) {
      var x = values.length === 1 ? 0 : (i * w / (values.length - 1));
      var y = h - ((v - min) * h / (max - min));
      return Math.round(x) + "," + Math.round(y);
    }).join(" ");
    return '<polyline class="' + esc(s.cls) + '" points="' + points + '"></polyline>';
  }).join("");
  var keys = series.map(function (s) { return s.key; }).join(",");
  var labels = series.map(function (s) { return s.label; }).join(",");
  return '<div class="chartWithScale energyWithScale" data-chart-series="' + esc(keys) + '" data-chart-labels="' + esc(labels) + '" data-chart-unit="' + esc(unit || "") + '">' + chartAxis(min, max, unit) + '<svg class="sparkline energySpark" viewBox="0 0 ' + w + ' ' + h + '" preserveAspectRatio="none"><g class="grid">' + grid + '</g>' + lines + '</svg></div>';
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
  if (!dashHistory.length) return -1;
  var rect = svg.getBoundingClientRect();
  var x = Math.max(0, Math.min(rect.width, event.clientX - rect.left));
  var ratio = rect.width ? x / rect.width : 0;
  return Math.max(0, Math.min(dashHistory.length - 1, Math.round(ratio * (dashHistory.length - 1))));
}

function historyTimeLabel(point) {
  if (!point || !point.t) return "";
  var d = new Date(point.t);
  return d.toLocaleTimeString([], {hour:"2-digit", minute:"2-digit", second:"2-digit"});
}

function showChartTooltip(event) {
  var box = event.target.closest ? event.target.closest(".chartWithScale") : null;
  if (!box || !dashHistory.length) {
    hideChartTooltip();
    return;
  }
  var svg = box.querySelector("svg");
  if (!svg) return;
  var index = historyIndexFromMouse(event, svg);
  var point = dashHistory[index];
  if (!point) return;
  var unit = box.getAttribute("data-chart-unit") || "";
  var html = '<b>' + esc(historyTimeLabel(point)) + '</b>';
  var series = box.getAttribute("data-chart-series");
  if (series) {
    var keys = series.split(",");
    var labels = (box.getAttribute("data-chart-labels") || series).split(",");
    html += keys.map(function (key, i) {
      return '<span>' + esc(labels[i] || key) + ' : ' + esc(fmt(point[key])) + ' ' + esc(unit) + '</span>';
    }).join("");
  } else {
    var key = box.getAttribute("data-chart-key");
    var meta = seriesByKey(key);
    html += '<span>' + esc(meta.label) + ' : ' + esc(fmt(point[key])) + ' ' + esc(unit || meta.unit || "") + '</span>';
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
}

async function loadStatus() {
  state = await api(page === "diagnostic" ? "/api/diagnostic" : "/api/status-lite");
}

async function refresh() {
  try {
    await loadStatus();
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
  var wifiCls = statusClass(state.wifiConnected, state.networkMode === "AP" || state.networkMode === "AP_STA");
  var safetyCls = state.safetyTripped || state.safetyLevel === "CRITICAL" ? "bad" : (state.safetyLevel === "WARNING" || state.safetyLevel === "DEGRADED" ? "warn" : "ok");
  var ssid = state.wifiSsid || "non renseigne";
  var ip = state.stationIp || state.localIp || "-";
  var moduleName = state.moduleName || "Routeur solaire";
  return banner() + helpBox("dashboard") + refreshControls() + dashboardCustomizeBox() +
    '<section class="dashHero">' +
      '<div><p class="eyebrow">Routeur solaire local</p><h1>' + esc(moduleName) + '</h1><div class="heroPills">' +
        pill("Role", state.role || "-", "info") +
        pill("WiFi", state.wifiConnected ? "connecte" : "AP local", wifiCls) +
        pill("Securite", state.safetyLevel || "OK", safetyCls) +
        pill("Simulation", state.simulationMode ? "active" : "off", state.simulationMode ? "warn" : "muted") +
      '</div></div>' +
      '<div class="heroNet">' +
        miniState("SSID", ssid, "") +
        miniState("IP box", ip, "info") +
        miniState("IP AP", state.apIp || "-", "info") +
        miniState("RSSI", state.rssi == null ? "-" : state.rssi + " dBm", wifiCls) +
      '</div>' +
    '</section>' +
    energyGraphCard() +
    '<section class="energyPanel">' +
      dashboardMetricKeys.filter(function (key) { return ["gridPowerW","injectionW","surplusW"].indexOf(key) < 0; }).map(metricCardFor).join("") +
    '</section>' +
    '<section class="chartPanel">' +
      dashboardChartKeys.map(chartCardFor).join("") +
    '</section>' +
    '<section class="dashBlocks">' +
      '<div class="dashBlock"><h2>Etat ESP</h2>' +
        miniState("Module", moduleName, "info") +
        miniState("Role", state.role || "-", "info") +
        miniState("Firmware", state.firmwareVersion || "-", "ok") +
        miniState("Puce", (state.chipModel || "ESP32") + " rev " + fmt(state.chipRevision), "info") +
        miniState("CPU", fmt(state.cpuMhz) + " MHz", "info") +
        miniState("Core", "Arduino " + (state.arduinoCore || "-"), "muted") +
        miniState("IDF", state.idfVersion || "-", "muted") +
        miniState("Flash", bytesHuman(state.flashBytes), "info") +
        miniState("LittleFS", bytesHuman(state.littleFsUsed) + " / " + bytesHuman(state.littleFsTotal), "info") +
        miniState("MAC", state.deviceId || "-", "muted") +
        miniState("Mode reseau", state.networkMode || "-", "info") +
        miniState("Heap libre", state.heapFree ? Math.round(state.heapFree / 1024) + " Ko" : "-", "ok") +
        miniState("API", state.ok ? "OK" : "Erreur", state.ok ? "ok" : "bad") +
      '</div>' +
      '<div class="dashBlock"><h2>Capteurs</h2>' +
        miniState("JSY-MK-194T", state.jsyOnline ? "OK" : "Absent", state.jsyOnline ? "ok" : "bad") +
        miniState("TIC Linky", state.ticAvailable ? "OK" : "Absent", state.ticAvailable ? "ok" : "warn") +
        miniState("Sonde 1", dsAvailable(0, state.tankTopC) ? fmt(state.tankTopC) + " C" : "Absent / non lu", dsAvailable(0, state.tankTopC) ? tempClass(state.tankTopC) : "bad") +
        miniState("Sonde 2", dsAvailable(1, state.tankMiddleC) ? fmt(state.tankMiddleC) + " C" : "Absent / non lu", dsAvailable(1, state.tankMiddleC) ? tempClass(state.tankMiddleC) : "bad") +
        miniState("Sonde 3", dsAvailable(2, state.tankBottomC) ? fmt(state.tankBottomC) + " C" : "Absent / non lu", dsAvailable(2, state.tankBottomC) ? tempClass(state.tankBottomC) : "bad") +
      '</div>' +
      '<div class="dashBlock"><h2>Actionneurs</h2>' +
        actuatorBar("SSR1", state.ssr1PowerPct) +
        actuatorBar("SSR2", state.ssr2PowerPct) +
        actuatorBar("RobotDyn", state.robotDynPowerPct) +
      '</div>' +
      '<div class="dashBlock"><h2>ESP-NOW / Secu</h2>' +
        miniState("ESP-NOW", state.espNowStatus || "local", "info") +
        miniState("Raison", state.safetyReason || "aucune", safetyCls) +
        miniState("Type simulation", state.simulationType || "off", state.simulationMode ? "warn" : "muted") +
      '</div>' +
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
    rows += '<tr class="' + esc(cls) + '"><td>' + esc(s.name || s.id) + '</td><td>DS18B20</td><td>' + esc(s.role) + '</td><td>GPIO ' + esc((cache.sensors.oneWireBus || {}).gpio || 4) + '</td><td><span class="badge ' + esc(cls) + '">' + esc(statusText) + '</span> ' + dsValue(i, temp) + '</td><td class="actions"><button onclick="editDsSensor(' + i + ')">Modifier</button><button onclick="toggleDsSensor(' + i + ')">' + (s.enabled !== false ? "Desactiver" : "Activer") + '</button><button class="danger" onclick="deleteDsSensor(' + i + ')">Supprimer</button></td></tr>';
  });
  $("app").innerHTML = banner() + '<h1>Capteurs</h1>' + helpBox("sensors") + dirtyNotice("sensors") + '<div class="toolbar"><button onclick="newSensor()">Ajouter capteur</button><button onclick="newJsySensor()">Ajouter JSY 2 pinces</button><button onclick="newDsSensor()">Ajouter DS18B20</button><button onclick="saveSensors()">Sauvegarder</button><button onclick="scanDs()">Scanner DS18B20</button><button onclick="jsonEditor(\'sensors\')">JSON avance</button></div><section class="panel" id="sensorForm">Selectionne un capteur ou ajoute-en un nouveau.</section><table><tr><th>Nom</th><th>Type</th><th>Role</th><th>Bus/GPIO</th><th>Etat/Valeur</th><th>Actions</th></tr>' + rows + '</table><pre id="scan"></pre>';
}

function defaultJsyChannels() {
  return [
    {id:"clamp1", name:"Pince 1", role:"grid", measures:["currentA1", "activePowerW1"]},
    {id:"clamp2", name:"Pince 2", role:"production", measures:["currentA2", "activePowerW2"]}
  ];
}

function ensureJsyChannels(s) {
  if (!s || ((s.id || "") !== "jsy_grid" && (s.type || "") !== "JSY-MK-194T")) return;
  if (!Array.isArray(s.channels) || s.channels.length < 2) s.channels = defaultJsyChannels();
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
    if (channels.length) return channels.map(function (c) { return (c.name || c.id || "voie") + " : " + (c.role || "non defini"); }).join(" / ");
  }
  return s.role || "";
}

function sensorExtraValue(s) {
  if ((s.id || "") !== "jsy_grid" && (s.type || "") !== "JSY-MK-194T") return "";
  return '<div class="channelGrid">' +
    '<span><b>Pince 1</b> ' + esc(fmt(state.activePowerW1)) + ' W / ' + esc(fmt(state.currentA1)) + ' A</span>' +
    '<span><b>Pince 2</b> ' + esc(fmt(state.activePowerW2)) + ' W / ' + esc(fmt(state.currentA2)) + ' A</span>' +
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
  var channels = s.channels || defaultJsyChannels();
  var roleOptions = sensorRolesForType(type);
  return '<h2>' + (index >= 0 ? "Modifier" : "Ajouter") + ' ' + (isDs ? "DS18B20" : "capteur") + '</h2><input id="sensorKind" type="hidden" value="' + kind + '"><input id="sensorIndex" type="hidden" value="' + index + '">' +
    '<div class="form">' +
    textField("sensorName", "Nom", s.name || "") +
    (isDs ? '<input id="sensorType" type="hidden" value="DS18B20">' : '<label>Type<select id="sensorType" onchange="updateSensorRoleOptions()">' + options(sensorTypes, type) + '</select></label>') +
    '<label>Role<select id="sensorRole">' + options(roleOptions, s.role || roleOptions[0] || "custom") + '</select></label>' +
    (isDs ? textField("sensorAddress", "Adresse OneWire", s.address || "") : '') +
    field("sensorGpio", "GPIO", s.gpio == null ? "" : s.gpio) +
    field("sensorRx", "RX", s.rx == null ? "" : s.rx) +
    field("sensorTx", "TX", s.tx == null ? "" : s.tx) +
    textField("sensorSource", "Source", s.source || "local") +
    textField("sensorMac", "MAC ESP-NOW", s.mac || "") +
    '<label><input id="sensorEnabled" class="check" type="checkbox" ' + checked(s.enabled) + '> Actif</label>' +
    (isDs ? '<label><input id="sensorCritical" class="check" type="checkbox" ' + checked(s.critical) + '> Critique securite</label>' : '') +
    (isJsy ? '<div class="subPanel"><h3>Voies amperemetriques JSY</h3><div class="formGrid"><label>Pince 1 nom<input id="jsyCh1Name" value="' + esc((channels[0] || {}).name || "Pince 1") + '"></label><label>Pince 1 role<select id="jsyCh1Role">' + options(jsyClampRoles, (channels[0] || {}).role || "grid") + '</select></label><label>Pince 2 nom<input id="jsyCh2Name" value="' + esc((channels[1] || {}).name || "Pince 2") + '"></label><label>Pince 2 role<select id="jsyCh2Role">' + options(jsyClampRoles, (channels[1] || {}).role || "production") + '</select></label></div><p class="muted">grid = arrivee reseau, production = solaire, load = charge dediee, custom = autre usage. Dans Logique, utilise activePowerW1 pour la pince 1 et activePowerW2 pour la pince 2.</p></div>' : '') +
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

function newSensor() { $("sensorForm").innerHTML = sensorFormHtml("sensor", -1, {enabled:true, source:"local"}); }
function newJsySensor() { $("sensorForm").innerHTML = sensorFormHtml("sensor", -1, {id:"jsy_grid", name:"JSY reseau", type:"JSY-MK-194T", source:"local", serial:"Serial2", rx:16, tx:17, role:"mesure reseau principal", enabled:true, channels:defaultJsyChannels()}); }
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
      item.channels = [
        {id:"clamp1", name:$("jsyCh1Name") ? $("jsyCh1Name").value : "Pince 1", role:$("jsyCh1Role") ? $("jsyCh1Role").value : "grid", measures:["currentA1", "activePowerW1"]},
        {id:"clamp2", name:$("jsyCh2Name") ? $("jsyCh2Name").value : "Pince 2", role:$("jsyCh2Role") ? $("jsyCh2Role").value : "production", measures:["currentA2", "activePowerW2"]}
      ];
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
  if (response.ok) clearDirty("sensors");
  alert(response.ok ? "Capteurs sauvegardes" : "Sauvegarde refusee: " + await response.text());
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
  var addresses = await api("/api/ds18b20");
  var list = Array.isArray(addresses) ? addresses : [];
  if (!list.length) {
    $("scan").innerHTML = '<div class="warnBox">Aucune sonde detectee sur le bus OneWire GPIO4.</div><pre>' + esc(JSON.stringify(addresses, null, 2)) + '</pre>';
    return;
  }
  var targets = (cache.sensors.ds18b20 || []).map(function (sensor, index) {
    return {
      id: sensor.id || ("sonde" + (index + 1)),
      label: sensor.id || ("sonde" + (index + 1))
    };
  });
  if (!targets.length) targets = [{id:"sonde1", label:"sonde1"}, {id:"sonde2", label:"sonde2"}, {id:"sonde3", label:"sonde3"}];
  $("scan").innerHTML = '<div class="scanList"><h3>Sondes detectees</h3>' + list.map(function (address) {
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

async function simEnable() { await fetch("/api/simulation/enable", {method:"POST"}); await refresh(); }
async function simDisable() { await fetch("/api/simulation/disable", {method:"POST"}); await refresh(); }
async function simRandom() { await fetch("/api/simulation/randomize", {method:"POST"}); await refresh(); }
async function simScenario(name) {
  await fetch("/api/simulation/scenario", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:new URLSearchParams({scenario:name})});
  await refresh();
}

async function simValues() {
  var body = {
    jsy: {available:true, gridPowerW:Number($("grid").value), voltageV:Number($("voltage").value), currentA:Number($("current").value), activePowerW1:Number($("jsyP1").value), activePowerW2:Number($("jsyP2").value), currentA1:Number($("jsyC1").value), currentA2:Number($("jsyC2").value), powerFactor:0.96, frequencyHz:50},
    tic: {available:true, apparentPowerVA:900, currentA:Number($("current").value), tariff:"BASE"},
    ds18b20: [
      {id:"sonde1", available:true, temperatureC:Number($("t1").value)},
      {id:"sonde2", available:true, temperatureC:Number($("t2").value)},
      {id:"sonde3", available:true, temperatureC:Number($("t3").value)}
    ]
  };
  await postJson("/api/simulation/values", body);
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
  var sim = s.simulation || {};
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
    '<section class="panel"><h2>Securite</h2><div class="form">' +
      '<label>Mode Safety<select id="safetyMode">' + options(["strict", "warning_only", "missing_sensors_off", "off"], safetyModeFromConfig(safe)) + '</select></label>' +
      field("tankMax", "Temp max ballon C", r.tankMaxC || 65) +
      field("tankSafety", "Temp securite C", r.tempSafetyMaxC || r.tankSafetyC || 70) +
    '</div><div class="warnBox">Le mode Safety OFF desactive les coupures automatiques logiciel. A reserver aux tests sans charge 230 V.</div></section>' +
    '<section class="panel"><h2>Routeur solaire</h2><div class="form">' +
      '<label>Source puissance reseau<select id="gridPowerSource">' + options(["JSY", "TIC", "AUTO"], r.gridPowerSource || "JSY") + '</select></label>' +
      field("minInjection", "Seuil demarrage injection W", r.minInjectionStartW || 200) +
      field("stopInjection", "Seuil arret injection W", r.stopBelowInjectionW || 80) +
      field("hysteresis", "Hysteresis W", r.hysteresisW || 50) +
      field("pidKp", "PID Kp", r.pidKp || 0.2) +
      field("pidKi", "PID Ki", r.pidKi || 0.01) +
      field("pidKd", "PID Kd", r.pidKd || 0) +
    '</div><p class="muted">JSY = mesure rapide via pince. TIC = Linky si la trame donne une puissance exploitable. AUTO = TIC si disponible, sinon JSY.</p></section>' +
    '<section class="panel"><h2>Simulation</h2><div class="form">' +
      '<label>Simulation<select id="simulationEnabled">' + options(["false", "true"], String((sim.enabled || s.simulationMode) ? true : false)) + '</select></label>' +
      '<label>Mode simulation<select id="simulationModeSelect">' + options(["manual", "random", "scenario"], sim.mode || "manual") + '</select></label>' +
    '</div><p class="muted">La simulation est temporaire: elle se coupe au bout de 5 minutes et ne redemarre jamais automatiquement apres un reboot.</p></section>' +
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
  return '<label>' + esc(label) + '<select id="' + id + '"><option value="true" ' + (value ? "selected" : "") + '>oui</option><option value="false" ' + (!value ? "selected" : "") + '>non</option></select></label>';
}

async function saveSettings() {
  var s = cache.system;
  var d = cache.device;
  s.wifi = s.wifi || {};
  s.router = s.router || {};
  s.safety = s.safety || {};
  s.simulation = s.simulation || {};
  d.name = $("devName").value;
  d.deviceName = d.name;
  d.role = $("devRole").value;
  d.isConfigured = $("devConfigured").value === "true";
  s.wifi.ssid = $("wifiSsid").value;
  s.wifiSsid = s.wifi.ssid;
  if ($("wifiPassword").value) {
    s.wifi.password = $("wifiPassword").value;
    s.wifiPassword = s.wifi.password;
  }
  s.wifi.keepFallbackApAlwaysOn = $("keepAp").value === "true";
  s.router.gridPowerSource = $("gridPowerSource").value;
  s.router.minInjectionStartW = Number($("minInjection").value);
  s.router.stopBelowInjectionW = Number($("stopInjection").value);
  s.router.hysteresisW = Number($("hysteresis").value);
  s.router.pidKp = Number($("pidKp").value);
  s.router.pidKi = Number($("pidKi").value);
  s.router.pidKd = Number($("pidKd").value);
  s.router.tankMaxC = Number($("tankMax").value);
  s.router.tempSafetyMaxC = Number($("tankSafety").value);
  s.router.tankSafetyC = s.router.tempSafetyMaxC;
  applySafetyModeConfig(s.safety, $("safetyMode").value);
  s.simulation.enabled = $("simulationEnabled").value === "true";
  s.simulation.mode = $("simulationModeSelect").value;
  s.simulationMode = s.simulation.enabled;
  var rd = await postJson("/api/device", d);
  if (!rd.ok) return alert("Sauvegarde module refusee: " + await rd.text());
  var response = await postJson("/api/system", s);
  if (!response.ok) return alert("Sauvegarde systeme refusee: " + await response.text());
  if (s.simulation.enabled) {
    await fetch("/api/simulation/enable", {method:"POST"});
    await fetch("/api/simulation/mode", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:new URLSearchParams({mode:s.simulation.mode})});
  } else {
    await fetch("/api/simulation/disable", {method:"POST"});
  }
  alert("Parametres sauvegardes");
  await refresh();
}

async function restartEsp() {
  if (!confirm("Redemarrer ESP32 maintenant ?")) return;
  await fetch("/api/system/reboot", {method:"POST"});
  $("app").innerHTML = '<h1>Redemarrage...</h1><div class="panel">Attends quelques secondes puis recharge /app.</div>';
}

function diagnosticPage() {
  var events = state.events || [];
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
      miniState("JSY-MK-194T", state.jsyOnline ? "OK" : "Absent", state.jsyOnline ? "ok" : "bad") +
      miniState("TIC Linky", state.ticAvailable ? "OK" : "Absent", state.ticAvailable ? "ok" : "warn") +
      miniState("Sonde 1", dsAvailable(0, state.tankTopC) ? fmt(state.tankTopC) + " C" : "Absent / non lu", dsAvailable(0, state.tankTopC) ? tempClass(state.tankTopC) : "bad") +
      miniState("Sonde 2", dsAvailable(1, state.tankMiddleC) ? fmt(state.tankMiddleC) + " C" : "Absent / non lu", dsAvailable(1, state.tankMiddleC) ? tempClass(state.tankMiddleC) : "bad") +
      miniState("Sonde 3", dsAvailable(2, state.tankBottomC) ? fmt(state.tankBottomC) + " C" : "Absent / non lu", dsAvailable(2, state.tankBottomC) ? tempClass(state.tankBottomC) : "bad") +
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

function simulationControlsHtml() {
  return '<div class="form">' +
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
    '</div><div class="toolbar"><button onclick="simEnable()">Activer</button><button onclick="simValues()">Appliquer manuel</button><button onclick="simRandom()">Aleatoire</button><button onclick="simScenario(\'tank_overheat\')">Surchauffe</button><button onclick="simScenario(\'jsy_lost\')">Perte JSY</button><button onclick="simDisable()">Desactiver</button></div>' +
    miniState("Etat", state.simulationMode ? "active" : "inactive", state.simulationMode ? "warn" : "muted") +
    miniState("Temps restant", state.simulationMode ? simRemainingText() : "-", state.simulationMode ? "warn" : "muted") +
    miniState("Mode", state.simulationType || "manual", "info");
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
  if (page === "sensors") return sensorsPage();
  if (page === "actuators") return actuatorsPage();
  if (page === "logic") return logicPage();
  if (page === "settings") return settingsPage();
  if (page === "diagnostic") return diagnosticPage();
  $("app").innerHTML = dashboard();
  refreshLabelPatch();
}

Array.prototype.forEach.call(document.querySelectorAll("button[data-page]"), function (button) {
  button.addEventListener("click", function () {
    page = button.getAttribute("data-page");
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
      if (page === "dashboard") refresh();
    }, dashboardRefreshMs);
  }
  refreshLabelPatch();
}

render();
scheduleDashboardRefresh();
refresh();
