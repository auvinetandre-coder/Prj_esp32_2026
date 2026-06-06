var page = "dashboard";
var dashboardRefreshMs = normalizeDashboardRefreshMs(localStorage.getItem("dashboardRefreshMs"));
var dashboardRefreshTimer = null;
var dashboardHistorySchemaVersion = 4;
var dashHistoryStorageKey = "dashboardHistoryData_v4";
var graphDataStorageKey = "dashboardRealtimeGraphData_v4";
var dashboardCleanVersion = "20260524-03-dashboard-clean";
var graphStorageSaveMs = 5000;
var lastGraphStorageSaveMs = 0;
var dashHistory = loadStoredHistory(dashHistoryStorageKey);
var lastHistorySampleMs = 0;
var editingDashboardSettings = false;
var dashboardSettingsEditUntil = 0;
var dashboardFocusedInputId = "";
var dashboardDirtyFields = {};
var dashboardSettingsSaveTimer = null;
var chartScaleCache = {};
var historySampleIntervalMs = 5000;
var historyMaxPoints = 360;
var realtimeGraphEnabled = false;
var realtimeGraphStepMs = Number(localStorage.getItem("realtimeGraphStepMs") || 300);
var realtimeGraphTimer = null;
var realtimeGraphStopTimer = null;
var realtimeGraphStartedAt = 0;
var realtimeHistory = [];
var graphData = loadStoredHistory(graphDataStorageKey);
var graphPollTimer = null;
var lastGraphDrawMs = 0;
var lastSimulationModeSeen = null;
var graphConfig = loadGraphConfig();
var dashboardConfig = defaultDashboardConfig();
var dashboardConfigLoaded = false;
var dashboardMetricKeys = loadDashboardList("dashboardMetrics", ["gridPowerW", "injectionW", "surplusW", "commandPercent"]);
var dashboardChartKeys = loadDashboardList("dashboardCharts", ["injectionW", "commandPercent", "surplusW"]);
var dashboardGraphDefaults = {
  network: ["gridPowerW"],
  routing: ["surplusW", "heaterPowerW"],
  outputs: ["commandPercent", "pidOutputPercent"],
  temps: []
};
var dashboardGraphChoices = {
  network: ["gridPowerW", "gridPowerFilteredW", "injectionW", "consumptionW"],
  routing: ["surplusW", "heaterPowerW"],
  outputs: ["commandPercent", "pidOutputPercent"],
  temps: []
};
var dashboardGraphNetwork = loadDashboardList("dashboardGraphNetwork", dashboardGraphDefaults.network);
var dashboardGraphRouting = loadDashboardList("dashboardGraphRouting", dashboardGraphDefaults.routing);
var dashboardGraphOutputs = loadDashboardList("dashboardGraphOutputs", dashboardGraphDefaults.outputs);
var dashboardGraphTemps = loadDashboardList("dashboardGraphTemps", dashboardGraphDefaults.temps);
var dashboardBlockDefaults = ["energy", "routing", "temps", "safety", "mode"];
var dashboardBlockChoices = [
  {key:"energy", label:"Equilibre reseau"},
  {key:"routing", label:"Routage routeur"},
  {key:"temps", label:"Temperatures"},
  {key:"safety", label:"Etat securite"},
  {key:"mode", label:"Mode routeur"},
  {key:"overview", label:"Etat general"},
  {key:"pid", label:"Calcul PID"},
  {key:"sensors", label:"Sources puissance"}
];
var dashboardBlocksVisible = loadDashboardList("dashboardBlocksVisible", dashboardBlockDefaults);
var sensorWizardState = null;
normalizeDashboardGraphDefaults();
normalizeDashboardBlocks();
normalizeDashboardCleanVersion();
cleanupGraphData();
while (dashHistory.length > historyMaxPoints) dashHistory.shift();
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
  productionW: 0,
  surplusW: 0,
  batteryVoltageV: null,
  batteryCurrentA: null,
  batteryPowerW: null,
  batterySocPct: null,
  batteryOnline: false,
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

var roles = ["custom", "depart_eau_chaude", "retour_eau_froide", "ambiance", "autre"];
var sensorTypes = ["JSY-MK-194T", "TIC Linky", "DS18B20", "Battery", "Solar", "Analog", "Digital", "Virtual"];
var sensorRolesByType = {
  "JSY-MK-194T": ["mesure_reseau_principal", "mesure_production", "mesure_charge", "diagnostic", "custom"],
  "TIC Linky": ["compteur_officiel", "diagnostic", "coherence_energie", "custom"],
  "DS18B20": roles,
  "Battery": ["stockage_principal", "diagnostic", "custom"],
  "Solar": ["production", "diagnostic", "custom"],
  "Analog": ["mesure_analogique", "niveau", "pression", "luminosite", "custom"],
  "Digital": ["etat_contact", "presence", "alarme", "custom"],
  "Virtual": ["surplus", "production", "consumption", "custom"]
};
var actuatorTypes = ["SSR", "RobotDyn Triac", "Relay", "PWM", "Digital Output", "Virtual", "REMOTE"];
var actuatorModes = ["OFF", "ON_OFF", "BURST_FIRE", "TRAIN_ONDES_ENTIERES", "ZERO_CROSS_BURST", "LOW_FREQ_PWM", "PHASE_ANGLE", "MANUAL_SAFE"];
var actuatorModeByType = {
  "SSR": ["OFF", "ON_OFF", "BURST_FIRE", "TRAIN_ONDES_ENTIERES", "ZERO_CROSS_BURST", "LOW_FREQ_PWM", "MANUAL_SAFE"],
  "RobotDyn Triac": ["OFF", "PHASE_ANGLE", "BURST_FIRE", "ZERO_CROSS_BURST", "MANUAL_SAFE"],
  "TRIAC": ["OFF", "PHASE_ANGLE", "BURST_FIRE", "ZERO_CROSS_BURST", "MANUAL_SAFE"],
  "Relay": ["OFF", "ON_OFF", "MANUAL_SAFE"],
  "RELAY": ["OFF", "ON_OFF", "MANUAL_SAFE"],
  "PWM": ["OFF", "LOW_FREQ_PWM", "MANUAL_SAFE"],
  "Digital Output": ["OFF", "ON_OFF", "MANUAL_SAFE"],
  "Virtual": ["OFF", "ON_OFF", "BURST_FIRE", "LOW_FREQ_PWM", "MANUAL_SAFE"],
  "REMOTE": ["OFF", "ON_OFF", "BURST_FIRE", "LOW_FREQ_PWM", "PHASE_ANGLE", "MANUAL_SAFE"]
};
var actuatorModeHelp = {
  OFF: "Sortie forcee a l'arret. Mode le plus sur pour tester ou neutraliser un actionneur.",
  ON_OFF: "Tout ou rien. Adapte aux relais, contacteurs ou sorties digitales lentes.",
  BURST_FIRE: "Modulation par trains d'impulsions sur une periode lente. Adapte aux SSR zero-cross pour chauffe-eau resistif.",
  TRAIN_ONDES_ENTIERES: "Variante SSR par trains d'ondes completes. Limite les parasites car la commutation reste proche du passage par zero.",
  ZERO_CROSS_BURST: "Commande SSR synchronisee passage par zero. Bon choix pour charges resistives et SSR zero-cross.",
  LOW_FREQ_PWM: "PWM lent avec millis(). Utilisable pour SSR ou sortie basse frequence, a eviter sur relais mecanique rapide.",
  PHASE_ANGLE: "Commande par angle de phase. Reservee au triac RobotDyn avec detection zero-cross.",
  MANUAL_SAFE: "Mode manuel limite par les securites. Les protections temperature et arret critique restent prioritaires."
};
var jsyClampRoles = ["grid", "production", "load", "custom"];
var ruleSources = [
  {id:"JSY-MK-194T", label:"JSY-MK-194T", measures:[["gridPowerW","number","W"],["injectionW","number","W"],["consumptionW","number","W"],["surplusW","number","W"],["voltageV","number","V"],["currentA","number","A"],["activePowerW","number","W"],["voltageV1","number","V"],["currentA1","number","A"],["activePowerW1","number","W"],["powerFactor1","number",""],["voltageV2","number","V"],["currentA2","number","A"],["activePowerW2","number","W"],["powerFactor2","number",""],["powerFactor","number",""],["frequencyHz","number","Hz"],["available","boolean",""]]},
  {id:"TIC Linky", label:"TIC Linky", measures:[["gridPowerW","number","W"],["apparentPowerVA","number","VA"],["currentA","number","A"],["tariff","text",""],["available","boolean",""],["lastValidReadAgeMs","number","ms"]]},
  {id:"sonde1", label:"sonde1", measures:[["temperatureC","number","C"],["available","boolean",""],["lastValidReadAgeMs","number","ms"]]},
  {id:"sonde2", label:"sonde2", measures:[["temperatureC","number","C"],["available","boolean",""],["lastValidReadAgeMs","number","ms"]]},
  {id:"sonde3", label:"sonde3", measures:[["temperatureC","number","C"],["available","boolean",""],["lastValidReadAgeMs","number","ms"]]},
  {id:"battery", label:"Batterie", measures:[["voltageV","number","V"],["currentA","number","A"],["powerW","number","W"],["socPct","number","%"],["available","boolean",""]]},
  {id:"solar", label:"Solaire", measures:[["powerW","number","W"],["available","boolean",""]]},
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
  setPowerFromSurplus:"Route le surplus solaire via la regulation choisie, PID par defaut.",
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
    ["CRITICAL", "Defaut critique. Les sorties SSR1 et SSR2 sont coupees."],
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
    ["BURST_FIRE", "Mode recommande pour SSR zero-cross sur charge resistive."]
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
  ],
  espnow: [
    ["Decouverte", "Chaque ESP compatible annonce son identite en broadcast ESP-NOW et ecoute les annonces des autres."],
    ["Peer autorise", "Un ESP detecte n'est pas forcement autorise. Ajoute-le comme peer avant d'accepter ses donnees."],
    ["Source et destination", "Un meme ESP peut produire des valeurs, en recevoir, ou faire les deux selon ses roles."],
    ["Mapping", "L'association des valeurs GRID, TEMP ou JSY vers les variables du routeur sera ajoutee dans une etape separee."]
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

function normalizeDashboardRefreshMs(value) {
  var n = Number(value);
  if (!isFinite(n) || n <= 0) return 1000;
  return Math.min(n, 1000);
}

function loadDashboardList(name, defaults) {
  try {
    var data = JSON.parse(localStorage.getItem(name) || "null");
    return Array.isArray(data) && data.length ? data : defaults.slice();
  } catch (e) {
    return defaults.slice();
  }
}

function normalizeDashboardGraphDefaults() {
  var legacyNetwork = ["gridPowerW", "injectionW", "consumptionW", "surplusW"];
  if (JSON.stringify(dashboardGraphNetwork) === JSON.stringify(legacyNetwork)) {
    dashboardGraphNetwork = dashboardGraphDefaults.network.slice();
    dashboardGraphRouting = dashboardGraphDefaults.routing.slice();
    dashboardGraphOutputs = dashboardGraphDefaults.outputs.slice();
    localStorage.setItem("dashboardGraphNetwork", JSON.stringify(dashboardGraphNetwork));
    localStorage.setItem("dashboardGraphRouting", JSON.stringify(dashboardGraphRouting));
    localStorage.setItem("dashboardGraphOutputs", JSON.stringify(dashboardGraphOutputs));
  }
  dashboardGraphRouting = dashboardGraphRouting.filter(function (key) {
    return dashboardGraphChoices.routing.indexOf(key) >= 0;
  });
  if (!dashboardGraphRouting.length) dashboardGraphRouting = dashboardGraphDefaults.routing.slice();
  localStorage.setItem("dashboardGraphRouting", JSON.stringify(dashboardGraphRouting));
}

function normalizeDashboardBlocks() {
  var valid = dashboardBlockChoices.map(function (item) { return item.key; });
  dashboardBlocksVisible = dashboardBlocksVisible.filter(function (key) {
    return valid.indexOf(key) >= 0;
  });
  if (!dashboardBlocksVisible.length) dashboardBlocksVisible = dashboardBlockDefaults.slice();
  localStorage.setItem("dashboardBlocksVisible", JSON.stringify(dashboardBlocksVisible));
}

function normalizeDashboardCleanVersion() {
  if (localStorage.getItem("dashboardCleanVersion") === dashboardCleanVersion) return;
  dashboardBlocksVisible = dashboardBlockDefaults.slice();
  localStorage.setItem("dashboardBlocksVisible", JSON.stringify(dashboardBlocksVisible));
  graphConfig.showPid = false;
  saveGraphConfig();
  localStorage.setItem("dashboardCleanVersion", dashboardCleanVersion);
}

function loadStoredHistory(name) {
  try {
    var data = JSON.parse(localStorage.getItem(name) || "[]");
    if (!Array.isArray(data)) return [];
    return data.filter(function (point) {
      return point && isFinite(Number(point.t));
    }).map(function (point) {
      point.t = Number(point.t);
      return point;
    });
  } catch (e) {
    return [];
  }
}

function saveStoredHistory(name, data) {
  try {
    localStorage.setItem(name, JSON.stringify(data || []));
  } catch (e) {
    // Historique local facultatif : si le navigateur refuse, les graphes restent actifs.
  }
}

function saveGraphHistorySoon(force) {
  var now = Date.now();
  if (!force && now - lastGraphStorageSaveMs < graphStorageSaveMs) return;
  lastGraphStorageSaveMs = now;
  saveStoredHistory(graphDataStorageKey, graphData);
}

function saveDashboardHistorySoon(force) {
  if (!force && dashHistory.length && Date.now() - lastHistorySampleMs < graphStorageSaveMs) return;
  saveStoredHistory(dashHistoryStorageKey, dashHistory);
}

function activeDashboardDataKeyMap() {
  var keys = {};
  getAvailableDataSources(state).forEach(function (source) {
    (source.values || []).forEach(function (value) {
      keys[dataSourceKey(source.sourceType, source.id || source.sensorId, value.key)] = true;
    });
  });
  return keys;
}

function pruneDashboardHistoryData() {
  var activeKeys = activeDashboardDataKeyMap();
  dashHistory.forEach(function (point) {
    if (!point || !point.data) return;
    Object.keys(point.data).forEach(function (key) {
      if (!activeKeys[key]) delete point.data[key];
    });
  });
}

function resetDashboardGraphBuffers() {
  graphData = [];
  dashHistory = [];
  chartScaleCache = {};
  lastHistorySampleMs = 0;
  saveStoredHistory(graphDataStorageKey, graphData);
  saveStoredHistory(dashHistoryStorageKey, dashHistory);
}

function defaultGraphConfig() {
  return {
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
    showPid: false,
    showTemperatures: true,
    showAreaFill: true,
    areaOpacity: 0.15
  };
}

function loadGraphConfig() {
  var defaults = defaultGraphConfig();
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

function defaultDashboardConfig() {
  return {
    temperatureMainSource: null,
    graphs: [
      {
        id:"graph_energy",
        title:"Equilibre reseau",
        type:"area",
        enabled:true,
        series:[
          {sourceType:"energy", sourceId:"energy", valueKey:"gridPowerW", label:"Reseau", unit:"W", color:"#ef4444"},
          {sourceType:"energy", sourceId:"energy", valueKey:"gridBuyW", label:"Achat", unit:"W", color:"#f87171"},
          {sourceType:"energy", sourceId:"energy", valueKey:"gridInjectionW", label:"Injection", unit:"W", color:"#22c55e"}
        ]
      },
      {
        id:"graph_pid",
        title:"PID / Regulation",
        type:"pid",
        enabled:true,
        series:[
          {sourceType:"pid", sourceId:"pid", valueKey:"errorW", label:"Erreur PID", unit:"W", color:"#f59e0b"},
          {sourceType:"pid", sourceId:"pid", valueKey:"outputPercent", label:"Sortie PID", unit:"%", color:"#38bdf8"},
          {sourceType:"pid", sourceId:"pid", valueKey:"commandPercent", label:"Commande", unit:"%", color:"#f97316"}
        ]
      }
    ]
  };
}

function mergeDashboardConfig(saved) {
  var defaults = defaultDashboardConfig();
  saved = saved && typeof saved === "object" ? saved : {};
  if (saved.temperatureMainSource) defaults.temperatureMainSource = saved.temperatureMainSource;
  if (Array.isArray(saved.graphs) && saved.graphs.length) {
    defaults.graphs = saved.graphs.map(function (graph, index) {
      graph = graph || {};
      return {
        id: graph.id || ("graph_" + index),
        title: graph.title || "Graphe",
        type: graph.type || "line",
        enabled: graph.enabled !== false,
        series: Array.isArray(graph.series) ? graph.series : []
      };
    }).filter(function (graph) {
      return !isRemovedDashboardGraphId(graph.id);
    });
  }
  dashboardGraphDefaultsById().forEach(function (graphDefault) {
    var existing = defaults.graphs.filter(function (graph) { return graph.id === graphDefault.id; })[0];
    if (!existing) defaults.graphs.push(graphDefault);
    else if ((existing.id === "graph_energy" && (!graphHasValue(existing, "gridBuyW") || existing.title === "Energie")) || (existing.id === "graph_pid" && existing.title === "PID")) {
      existing.title = graphDefault.title;
      existing.series = graphDefault.series;
      existing.type = graphDefault.type;
    }
  });
  return defaults;
}

function dashboardGraphDefaultsById() {
  return defaultDashboardConfig().graphs;
}

function isRemovedDashboardGraphId(id) {
  return id === "graph_temperature" || id === "graph_routing";
}

function graphHasValue(graph, valueKey) {
  return !!(graph && Array.isArray(graph.series) && graph.series.some(function (series) { return series && series.valueKey === valueKey; }));
}

async function loadDashboardConfig() {
  try {
    dashboardConfig = mergeDashboardConfig(await api("/api/dashboard/config"));
    dashboardConfigLoaded = true;
  } catch (e) {
    dashboardConfig = mergeDashboardConfig(JSON.parse(localStorage.getItem("dashboardConfigFallback") || "{}"));
    dashboardConfigLoaded = false;
  }
}

async function saveDashboardConfig() {
  localStorage.setItem("dashboardConfigFallback", JSON.stringify(dashboardConfig));
  var response = await fetch("/api/dashboard/config", {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify(dashboardConfig)});
  if (!response.ok) throw new Error(await response.text());
  dashboardConfigLoaded = true;
}

function optionalNumberInput(id) {
  var node = $(id);
  if (!node || node.value === "") return null;
  return finiteOrNull(node.value);
}

function checked(value) {
  return value !== false ? "checked" : "";
}

function readNumber(id, fallback) {
  var node = $(id);
  if (!node) return fallback;
  var value = node.value;
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

function pad2(value) {
  return String(value).padStart(2, "0");
}

function pad3(value) {
  return String(value).padStart(3, "0");
}

function formatDateTimeMs(epochMs) {
  var date = new Date(Number(epochMs));
  if (!isFinite(date.getTime())) return "N/A";
  return pad2(date.getDate()) + "/" + pad2(date.getMonth() + 1) + "/" + date.getFullYear() + " " +
    pad2(date.getHours()) + ":" + pad2(date.getMinutes()) + ":" + pad2(date.getSeconds()) + "." + pad3(date.getMilliseconds());
}

function timeFromUptimeMs(timestampMs) {
  var stamp = Number(timestampMs);
  if (!isFinite(stamp) || stamp <= 0) return "N/A";
  var uptime = Number(state.uptimeMs);
  if (!isFinite(uptime) || uptime <= 0) uptime = Number(state.uptime) * 1000;
  if (!isFinite(uptime) || uptime <= 0) return fmt(stamp) + " ms";
  var age = Math.max(0, uptime - stamp);
  var epoch = Number(state.currentEpochMs);
  var suffix = "";
  if (!isFinite(epoch) || epoch <= 0) {
    epoch = Date.now();
    suffix = " approx.";
  }
  return formatDateTimeMs(epoch - age) + suffix;
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

function dsLastReadMs(index) {
  if (Array.isArray(state.ds18b20) && state.ds18b20[index]) return state.ds18b20[index].lastReadMs;
  if (Array.isArray(state.ds18b20LastReadMs)) return state.ds18b20LastReadMs[index] || 0;
  return 0;
}

function dsTemperature(index) {
  if (Array.isArray(state.ds18b20) && state.ds18b20[index] && hasValue(state.ds18b20[index].temperatureC)) return state.ds18b20[index].temperatureC;
  if (Array.isArray(state.ds18b20Temps) && hasValue(state.ds18b20Temps[index])) return state.ds18b20Temps[index];
  return [state.tankTopC, state.tankMiddleC, state.tankBottomC][index];
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
  return "";
}

function inlineHelp(text) {
  return '<span class="inlineHelp" tabindex="0" aria-label="Aide">?<span>' + esc(text) + '</span></span>';
}

function refreshControls() {
  return '<div class="refreshBox"><span>Rafraichissement Dashboard</span><select id="dashRefresh" onchange="setDashboardRefresh(this.value)">' +
    options(["1000", "5000", "10000", "30000"], String(dashboardRefreshMs)) +
    '</select><button onclick="refresh()">Actualiser maintenant</button></div>';
}

function dashboardSeries() {
  return [
    {key:"gridPowerW", label:"Puissance reseau", unit:"W", cls:"gridCurve", value:state.gridPowerW},
    {key:"gridPowerRawW", label:"Reseau brut", unit:"W", cls:"gridCurve", value:state.gridPowerRawW},
    {key:"gridPowerFilteredW", label:"Reseau filtre", unit:"W", cls:"surplusCurve", value:state.gridPowerFilteredW},
    {key:"injectionW", label:"Injection", unit:"W", cls:"injectionCurve", value:state.injectionW, min:0},
    {key:"consumptionW", label:"Consommation", unit:"W", cls:"consumptionCurve", value:state.consumptionW, min:0},
    {key:"surplusW", label:"Surplus", unit:"W", cls:"surplusCurve", value:state.surplusW, min:0},
    {key:"targetW", label:"Consigne reseau", unit:"W", cls:"info", value:0},
    {key:"deadbandHighW", label:"Deadband haut", unit:"W", cls:"warn", value:null},
    {key:"deadbandLowW", label:"Deadband bas", unit:"W", cls:"warn", value:null},
    {key:"pidOutputPercent", label:"PID", unit:"%", cls:"info", value:state.pidOutputPercent, min:0, max:100},
    {key:"commandPercent", label:"Commande routeur", unit:"%", cls:"heat", value:state.commandPercent, min:0, max:100},
    {key:"heaterPowerW", label:"Puissance routeur", unit:"W", cls:"heat", value:state.heaterPowerW, min:0},
    {key:"tempSafety", label:"Seuil securite", unit:"C", cls:"bad", value:null, min:0, max:80},
    {key:"heapFree", label:"Heap libre", unit:"o", cls:"ok", value:state.heapFree, min:0}
  ].concat(actuatorDashboardSeries()).concat(temperatureDashboardSeries());
}

function actuatorDashboardSeries() {
  return getActuators().map(function (actuator, index) {
    return {
      key:dataSourceKey("actuator", actuator.id || ("actuator" + index), "commandPercent"),
      label:actuator.name || actuator.id || ("Actionneur " + (index + 1)),
      unit:"%",
      cls:actuatorOrigin(actuator) === "ESP-NOW" ? "info" : "heat",
      value:getActuatorCommandPercent(actuator),
      min:0,
      max:100
    };
  });
}

function temperatureDashboardSeries() {
  return temperatureGraphSeries().map(function (series, index) {
    return {
      key:dataSourceKey(series.sourceType, series.sourceId, series.valueKey),
      label:series.label,
      unit:series.unit || "C",
      cls:"tempCurve" + ((index % 3) + 1),
      value:numericSourceValue(series),
      min:0,
      max:80
    };
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
  var sensors = activeSensorsList();
  for (var i = 0; i < sensors.length; i++) {
    var sensor = sensors[i] || {};
    if ((sensor.id || "") === "jsy_grid" || (sensor.type || "") === "JSY-MK-194T") return sensor;
  }
  return null;
}

function configuredLocalSensorByType(type, id) {
  var sensors = activeSensorsList();
  for (var i = 0; i < sensors.length; i++) {
    var sensor = sensors[i] || {};
    if (sensor.disabled || sensor.enabled === false) continue;
    if ((id && (sensor.id || "") === id) || (sensor.type || "") === type) return sensor;
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

function dashboardBlockCheckboxes() {
  return dashboardBlockChoices.map(function (item) {
    var id = "dash_block_" + item.key;
    return '<label class="curveToggle" for="' + esc(id) + '">' +
      '<input id="' + esc(id) + '" type="checkbox" data-dashboard-block="' + esc(item.key) + '" ' + (dashboardBlocksVisible.indexOf(item.key) >= 0 ? "checked" : "") + '>' +
      '<span>' + esc(item.label) + '</span></label>';
  }).join("");
}

function checkedDashboardBlocks() {
  var nodes = document.querySelectorAll("input[data-dashboard-block]");
  var values = Array.prototype.map.call(nodes, function (node) {
    return node.checked ? node.getAttribute("data-dashboard-block") : "";
  }).filter(Boolean);
  return values.length ? values : dashboardBlockDefaults.slice();
}

function saveDashboardGraphsOnly() {
  dashboardGraphNetwork = checkedGraphKeys("network");
  dashboardGraphRouting = checkedGraphKeys("routing");
  dashboardGraphOutputs = checkedGraphKeys("outputs");
  dashboardGraphTemps = checkedGraphKeys("temps");
  localStorage.setItem("dashboardGraphNetwork", JSON.stringify(dashboardGraphNetwork));
  localStorage.setItem("dashboardGraphRouting", JSON.stringify(dashboardGraphRouting));
  localStorage.setItem("dashboardGraphOutputs", JSON.stringify(dashboardGraphOutputs));
  localStorage.setItem("dashboardGraphTemps", JSON.stringify(dashboardGraphTemps));
}

function saveDashboardBlocksOnly() {
  dashboardBlocksVisible = checkedDashboardBlocks();
  localStorage.setItem("dashboardBlocksVisible", JSON.stringify(dashboardBlocksVisible));
}

function saveDashboardGraphs() {
  saveDashboardGraphsOnly();
  render();
}

function resetDashboardGraphsOnly() {
  dashboardGraphNetwork = dashboardGraphDefaults.network.slice();
  dashboardGraphRouting = dashboardGraphDefaults.routing.slice();
  dashboardGraphOutputs = dashboardGraphDefaults.outputs.slice();
  dashboardGraphTemps = dashboardGraphDefaults.temps.slice();
  localStorage.setItem("dashboardGraphNetwork", JSON.stringify(dashboardGraphNetwork));
  localStorage.setItem("dashboardGraphRouting", JSON.stringify(dashboardGraphRouting));
  localStorage.setItem("dashboardGraphOutputs", JSON.stringify(dashboardGraphOutputs));
  localStorage.setItem("dashboardGraphTemps", JSON.stringify(dashboardGraphTemps));
}

function resetDashboardBlocksOnly() {
  dashboardBlocksVisible = dashboardBlockDefaults.slice();
  localStorage.setItem("dashboardBlocksVisible", JSON.stringify(dashboardBlocksVisible));
}

function resetDashboardGraphs() {
  resetDashboardGraphsOnly();
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
      graphToggle("showAreaFill", "Remplissage area chart") +
      graphSelect("areaOpacity", "Opacite zones", [["0.05","0.05"],["0.10","0.10"],["0.15","0.15"],["0.20","0.20"],["0.30","0.30"],["0.50","0.50"]]) +
      graphToggle("showEnergy", "Graphe energie") +
      graphToggle("showTarget", "Consigne") +
      graphToggle("showDeadband", "Deadband") +
      graphToggle("showHeaterPower", "Puissance routeur") +
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
    else if (key === "smoothingAlpha" || key === "areaOpacity") graphConfig[key] = Number(node.value) || (key === "areaOpacity" ? 0.15 : 0.2);
    else graphConfig[key] = Number(node.value) || graphConfig[key];
  });
  graphConfig.areaOpacity = Math.max(0.05, Math.min(0.5, Number(graphConfig.areaOpacity) || 0.15));
  saveGraphConfig();
  cleanupGraphData();
  restartGraphPolling();
  render();
}

function applyDashboardPersonalization() {
  document.querySelectorAll("[data-graph-setting]").forEach(function (node) {
    var key = node.getAttribute("data-graph-setting");
    if (!key) return;
    if (node.type === "checkbox") graphConfig[key] = node.checked;
    else if (key === "smoothingAlpha" || key === "areaOpacity") graphConfig[key] = Number(node.value) || (key === "areaOpacity" ? 0.15 : 0.2);
    else graphConfig[key] = Number(node.value) || graphConfig[key];
  });
  graphConfig.areaOpacity = Math.max(0.05, Math.min(0.5, Number(graphConfig.areaOpacity) || 0.15));
  saveGraphConfig();
  saveDashboardBlocksOnly();
  saveDashboardGraphsOnly();
  clearDashboardEditingState();
  cleanupGraphData();
  restartGraphPolling();
  render();
}

function resetDashboardPersonalization() {
  graphConfig = defaultGraphConfig();
  saveGraphConfig();
  resetDashboardBlocksOnly();
  resetDashboardGraphsOnly();
  clearDashboardEditingState();
  cleanupGraphData();
  restartGraphPolling();
  render();
}

function dashboardEditingContainer(node) {
  return node && node.closest ? node.closest(".graphEditor") : null;
}

function dashboardEditableFieldKey(node) {
  if (!node || !/^(INPUT|SELECT|TEXTAREA)$/.test(node.tagName || "")) return "";
  return node.id || node.name || node.getAttribute("data-graph-setting") || node.getAttribute("data-graph-group") ||
    node.getAttribute("data-dashboard-block") || node.getAttribute("data-series-source") ||
    node.getAttribute("data-series-label") || node.getAttribute("data-series-unit") ||
    node.getAttribute("data-series-color") || "";
}

function clearDashboardEditingState() {
  editingDashboardSettings = false;
  dashboardSettingsEditUntil = 0;
  dashboardFocusedInputId = "";
  dashboardDirtyFields = {};
}

function markDashboardSettingsEditing(target, dirty) {
  editingDashboardSettings = true;
  dashboardSettingsEditUntil = Date.now() + 3500;
  var key = dashboardEditableFieldKey(target);
  if (key) {
    dashboardFocusedInputId = key;
    if (dirty) dashboardDirtyFields[key] = true;
  }
}

function dashboardDirtyFieldCount() {
  return Object.keys(dashboardDirtyFields || {}).length;
}

function isUserEditingSettings() {
  var active = document.activeElement;
  var inPanel = dashboardEditingContainer(active);
  if (inPanel && /^(INPUT|SELECT|TEXTAREA|BUTTON)$/.test(active.tagName || "")) return true;
  if (dashboardDirtyFieldCount() > 0) return true;
  if (dashboardFocusedInputId) return true;
  if (editingDashboardSettings && Date.now() < dashboardSettingsEditUntil) return true;
  if (editingDashboardSettings) clearDashboardEditingState();
  return false;
}

function clearGraphData() {
  resetDashboardGraphBuffers();
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
    gridPowerW: powerOrNull(data.gridPowerFilteredW != null ? data.gridPowerFilteredW : data.gridPowerW),
    gridPowerRawW: powerOrNull(data.gridPowerRawW),
    productionW: powerOrNull(data.productionW),
    injectionW: powerOrNull(data.injectionW),
    consumptionW: powerOrNull(data.consumptionW),
    surplusW: powerOrNull(data.surplusW),
    targetW: finiteOrNull(data.targetW),
    deadbandW: finiteOrNull(data.deadbandW),
    deadbandHighW: finiteOrNull(Number(data.targetW || 0) + Number(data.deadbandW || 0)),
    deadbandLowW: finiteOrNull(Number(data.targetW || 0) - Number(data.deadbandW || 0)),
    heaterPowerW: powerOrNull(data.heaterPowerW),
    pidErrorW: powerOrNull(data.pidErrorW),
    pidOutputPercent: finiteOrNull(data.pidOutputPercent),
    commandPercent: finiteOrNull(data.commandPercent),
    ssr1PowerPct: finiteOrNull(data.ssr1PowerPct),
    ssr2PowerPct: finiteOrNull(data.ssr2PowerPct),
    robotDynPowerPct: finiteOrNull(data.robotDynPowerPct),
    temp1: finiteOrNull(data.temp1),
    temp2: finiteOrNull(data.temp2),
    temp3: finiteOrNull(data.temp3),
    tempSafety: finiteOrNull(data.tempSafety),
    simulationMode: !!data.simulationMode
  };
  appendSmoothedFields(point);
  graphData.push(point);
  cleanupGraphData();
  saveGraphHistorySoon(false);
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

function boundedFiniteOrNull(value, absMax) {
  var n = finiteOrNull(value);
  if (n == null) return null;
  return Math.abs(n) <= absMax ? n : null;
}

function powerOrNull(value) {
  return boundedFiniteOrNull(value, 100000);
}

function smoothValue(previous, current, alpha) {
  if (current == null || !isFinite(current)) return null;
  if (previous == null || !isFinite(previous)) return current;
  return previous * (1 - alpha) + current * alpha;
}

function appendSmoothedFields(point) {
  var previous = graphData.length ? graphData[graphData.length - 1] : null;
  var alpha = Number(graphConfig.smoothingAlpha) || 0.2;
  ["gridPowerW","productionW","heaterPowerW","pidErrorW","pidOutputPercent","commandPercent","temp1","temp2","temp3"].forEach(function (key) {
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
  renderDashboardCanvases();
}

function setDashboardRefresh(value) {
  dashboardRefreshMs = normalizeDashboardRefreshMs(value);
  localStorage.setItem("dashboardRefreshMs", String(dashboardRefreshMs));
  scheduleDashboardRefresh();
}

function refreshLabelPatch() {
  var select = $("dashRefresh");
  if (!select) return;
  Array.prototype.forEach.call(select.options, function (option) {
    if (option.value === "5000") option.textContent = "5 s";
    if (option.value === "1000") option.textContent = "1 s";
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

function chartStepForUnit(min, max, unit) {
  if (unit === "W") return chartAxisLabelStep(min, max, unit);
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
  var step = chartStepForUnit(min, max, unit);
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
  if (point.data && point.data.hasOwnProperty(key)) return point.data[key];
  var mapped = key;
  if (graphConfig.smoothingEnabled) {
    var smoothKey = mapped + "Smooth";
    if (point[smoothKey] != null && isFinite(point[smoothKey])) return point[smoothKey];
  }
  return point[mapped];
}

function chartHistory() {
  return dashHistory;
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
  return areaPathToBaseline(coords, h);
}

function areaPathToBaseline(coords, baselineY) {
  if (coords.length < 2) return "";
  var line = smoothPath(coords);
  var first = coords[0];
  var last = coords[coords.length - 1];
  return line + " L " + last.x + " " + baselineY + " L " + first.x + " " + baselineY + " Z";
}

function chartZeroY(min, max, h) {
  if (min >= 0) return h;
  if (max <= 0) return 0;
  return Math.round(h - ((0 - min) * h / (max - min)));
}

function areaOpacity(value) {
  var opacity = Number(value);
  if (!isFinite(opacity)) opacity = 0.15;
  return Math.max(0.05, Math.min(0.5, opacity));
}

function clampOpacity(value) {
  var opacity = Number(value);
  if (!isFinite(opacity)) opacity = 0.15;
  return Math.max(0, Math.min(1, opacity));
}

function rgbaFromHex(hex, opacity) {
  var text = String(hex || "#60a5fa").replace("#", "");
  if (text.length === 3) text = text.replace(/(.)/g, "$1$1");
  var n = parseInt(text, 16);
  if (!isFinite(n)) n = 0x60a5fa;
  return "rgba(" + ((n >> 16) & 255) + "," + ((n >> 8) & 255) + "," + (n & 255) + "," + clampOpacity(opacity).toFixed(2) + ")";
}

function chartColorForClass(cls) {
  var colors = {
    solar:"#4caf50",
    consume:"#2196f3",
    sun:"#4caf50",
    heat:"#ff9800",
    info:"#2196f3",
    warn:"#ff9800",
    gridCurve:"#f44336",
    injectionCurve:"#8b5cf6",
    consumptionCurve:"#2196f3",
    surplusCurve:"#4caf50",
    tempCurve1:"#a78bfa",
    tempCurve2:"#06b6d4",
    tempCurve3:"#f472b6"
  };
  return colors[String(cls || "").split(/\s+/)[0]] || "#60a5fa";
}

function chartGradientDef(id, cls) {
  var opacity = areaOpacity(graphConfig.areaOpacity);
  var color = chartColorForClass(cls);
  var className = String(cls || "").split(/\s+/)[0];
  var paleSurplus = className === "surplusCurve" || className === "sun";
  var isTemp = className.indexOf("tempCurve") === 0;
  var topOpacity = paleSurplus ? Math.min(0.2, opacity * 1.25) : Math.min(0.3, opacity * 1.45);
  if (isTemp) topOpacity = Math.min(0.16, opacity);
  var bottomOpacity = isTemp ? 0.01 : (paleSurplus ? 0.02 : 0.01);
  return '<linearGradient id="' + esc(id) + '" x1="0" y1="0" x2="0" y2="1">' +
    '<stop offset="0%" stop-color="' + esc(rgbaFromHex(color, topOpacity)) + '"></stop>' +
    '<stop offset="100%" stop-color="' + esc(rgbaFromHex(color, bottomOpacity)) + '"></stop>' +
    '</linearGradient>';
}

function chartFillPath(coords, baselineY, cls, gradientId) {
  if (!graphConfig.showAreaFill) return "";
  return '<path class="chartFill ' + esc(cls || "") + '" fill="url(#' + esc(gradientId) + ')" d="' + areaPathToBaseline(coords, baselineY) + '"></path>';
}

function signedGridFillDefs(id, baselineY, w, h) {
  var opacity = areaOpacity(graphConfig.areaOpacity);
  var consumeTop = Math.min(0.26, opacity * 1.35);
  var injectTop = Math.min(0.22, opacity * 1.25);
  return signedGridLineDefs(id + "Fill", baselineY, w, h) +
    '<linearGradient id="' + esc(id) + 'ConsumeGrad" x1="0" y1="0" x2="0" y2="1">' +
      '<stop offset="0%" stop-color="' + esc(rgbaFromHex("#f44336", consumeTop)) + '"></stop>' +
      '<stop offset="100%" stop-color="rgba(244,67,54,0.01)"></stop>' +
    '</linearGradient>' +
    '<linearGradient id="' + esc(id) + 'InjectGrad" x1="0" y1="0" x2="0" y2="1">' +
      '<stop offset="0%" stop-color="rgba(76,175,80,0.01)"></stop>' +
      '<stop offset="100%" stop-color="' + esc(rgbaFromHex("#4caf50", injectTop)) + '"></stop>' +
    '</linearGradient>';
}

function signedGridFillPaths(id, coords, baselineY) {
  if (!graphConfig.showAreaFill) return "";
  var path = areaPathToBaseline(coords, baselineY);
  return '<path class="chartFill gridConsumeFill" clip-path="url(#' + esc(id) + 'FillConsume)" fill="url(#' + esc(id) + 'ConsumeGrad)" d="' + path + '"></path>' +
    '<path class="chartFill gridInjectFill" clip-path="url(#' + esc(id) + 'FillInject)" fill="url(#' + esc(id) + 'InjectGrad)" d="' + path + '"></path>';
}

function signedGridLineDefs(id, baselineY, w, h) {
  return '<clipPath id="' + esc(id) + 'Consume"><rect x="0" y="0" width="' + w + '" height="' + Math.max(0, baselineY) + '"></rect></clipPath>' +
    '<clipPath id="' + esc(id) + 'Inject"><rect x="0" y="' + Math.max(0, baselineY) + '" width="' + w + '" height="' + Math.max(0, h - baselineY) + '"></rect></clipPath>';
}

function signedGridLinePaths(id, coords) {
  var path = smoothPath(coords);
  return '<path class="chartLine gridConsume" clip-path="url(#' + esc(id) + 'Consume)" d="' + path + '"></path>' +
    '<path class="chartLine gridInject" clip-path="url(#' + esc(id) + 'Inject)" d="' + path + '"></path>';
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
    if (minFixed != null) min = minFixed;
    if (maxFixed != null) max = maxFixed;
    if (min > 0) min = 0;
    if (max < 0) max = 0;
  }
  if (unit === "%" && !graphConfig.autoScaleY) { min = 0; max = 100; }
  if (unit === "C") {
    min = 0;
    max = graphConfig.autoScaleY ? Math.min(90, Math.max(25, Math.ceil(max / 5) * 5 + 5)) : 80;
  }
  if (max === min) max = min + 1;
  var scale = stableChartScale("spark_" + key, min, max, unit);
  min = scale.min;
  max = scale.max;
  var w = 360, h = 142;
  var plotX = 0;
  var plotW = w;
  var grid = chartGrid(min, max, w, h, unit);
  var baselineY = chartZeroY(min, max, h);
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
  var gradientId = "chartGrad_" + key.replace(/[^a-zA-Z0-9_]/g, "") + "_" + Math.round(Date.now() % 100000);
  var clipId = "gridClip_" + Math.round(Date.now() % 100000);
  var defs = key === "gridPowerW"
    ? signedGridFillDefs(clipId, baselineY, w, h) + signedGridLineDefs(clipId, baselineY, w, h)
    : chartGradientDef(gradientId, cls);
  var line = key === "gridPowerW" ? signedGridLinePaths(clipId, coords) : '<path class="chartLine" d="' + smoothPath(coords) + '"></path>';
  var fill = key === "gridPowerW" ? signedGridFillPaths(clipId, coords, baselineY) : chartFillPath(coords, baselineY, cls, gradientId);
  return '<div class="chartWithScale" data-chart-key="' + esc(key) + '" data-chart-unit="' + esc(unit || "") + '">' + chartAxis(min, max, unit) + chartYLabelsHtml(min, max, unit) + '<div class="chartPlot"><svg class="sparkline ' + esc(cls || "") + '" viewBox="0 0 ' + w + ' ' + h + '" preserveAspectRatio="none"><defs>' + defs + '</defs><g class="grid">' + grid + '</g>' + fill + line + '<circle class="lastPoint" cx="' + last.x + '" cy="' + last.y + '" r="2"></circle></svg>' + chartTimeAxisHtml() + '</div></div>';
}

function multiSparkline(series, unit) {
  var history = chartHistory();
  if (!graphConfig.enabled) return '<div class="sparkline energySpark muted">graphes desactives</div>';
  if (history.length < 2) return '<div class="sparkline energySpark muted">historique en cours...</div>';
  var all = [];
  var minFixed = null;
  var maxFixed = null;
  series.forEach(function (s) {
    if (s.min != null) minFixed = minFixed == null ? s.min : Math.min(minFixed, s.min);
    if (s.max != null) maxFixed = maxFixed == null ? s.max : Math.max(maxFixed, s.max);
    if (unit === "C" && s.key === "tempSafety") return;
    history.forEach(function (p) {
      var v = graphValue(p, s.key);
      if (v != null && isFinite(v)) all.push(Number(v));
    });
  });
  if (all.length < 2) return '<div class="sparkline energySpark muted">pas de valeur</div>';
  var min = minFixed != null ? minFixed : Math.min.apply(null, all);
  var max = maxFixed != null ? maxFixed : Math.max.apply(null, all);
  if (unit === "W") {
    var marginW = Math.max(50, Math.ceil((max - min) * 0.12 / 50) * 50);
    min = Math.floor((min - marginW) / 50) * 50;
    max = Math.ceil((max + marginW) / 50) * 50;
    if (minFixed != null) min = minFixed;
    if (maxFixed != null) max = maxFixed;
    if (min > 0) min = 0;
    if (max < 0) max = 0;
  } else if (min > 0 && unit !== "C") min = 0;
  if (unit === "%" && !graphConfig.autoScaleY) { min = 0; max = 100; }
  if (unit === "C") {
    min = 0;
    max = graphConfig.autoScaleY ? Math.min(90, Math.max(25, Math.ceil(max / 5) * 5 + 5)) : 80;
  }
  if (max === min) max = min + 1;
  var scale = stableChartScale("multi_" + series.map(function (s) { return s.key; }).join("_"), min, max, unit);
  min = scale.min;
  max = scale.max;
  var w = 360, h = 142;
  var plotX = 0;
  var plotW = w;
  var now = Date.now();
  var baselineY = chartZeroY(min, max, h);
  var defs = "";
  var fills = "";
  var lines = series.map(function (s, index) {
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
    var gradientId = "chartGrad_" + String(s.key || index).replace(/[^a-zA-Z0-9_]/g, "") + "_" + index + "_" + Math.round(Date.now() % 100000);
    if (s.key === "gridPowerW") {
      defs += signedGridFillDefs(gradientId + "Clip", baselineY, w, h) + signedGridLineDefs(gradientId + "Clip", baselineY, w, h);
      fills += signedGridFillPaths(gradientId + "Clip", coords, baselineY);
    } else {
      defs += chartGradientDef(gradientId, s.cls);
      fills += chartFillPath(coords, baselineY, s.cls, gradientId);
    }
    var line = s.key === "gridPowerW"
      ? signedGridLinePaths(gradientId + "Clip", coords)
      : '<path class="chartLine ' + esc(s.cls) + '" d="' + smoothPath(coords) + '"></path>';
    return line + '<circle class="lastPoint ' + esc(s.cls) + '" cx="' + last.x + '" cy="' + last.y + '" r="2"></circle>';
  }).join("");
  var keys = series.map(function (s) { return s.key; }).join(",");
  var labels = series.map(function (s) { return s.label; }).join(",");
  var classes = series.map(function (s) { return s.cls || ""; }).join(",");
  return '<div class="chartWithScale energyWithScale" data-chart-series="' + esc(keys) + '" data-chart-labels="' + esc(labels) + '" data-chart-classes="' + esc(classes) + '" data-chart-unit="' + esc(unit || "") + '">' + chartAxis(min, max, unit) + chartYLabelsHtml(min, max, unit) + '<div class="chartPlot"><svg class="sparkline energySpark" viewBox="0 0 ' + w + ' ' + h + '" preserveAspectRatio="none"><defs>' + defs + '</defs><g class="grid">' + chartGrid(min, max, plotW, h, unit) + '</g>' + fills + lines + '</svg>' + chartTimeAxisHtml() + '</div></div>';
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

function stableChartScale(id, min, max, unit) {
  if (!isFinite(min) || !isFinite(max)) return {min:min, max:max};
  if (unit === "%" || unit === "C") return {min:min, max:max};
  var previous = chartScaleCache[id];
  if (!previous || !isFinite(previous.min) || !isFinite(previous.max)) {
    chartScaleCache[id] = {min:min, max:max};
    return chartScaleCache[id];
  }
  var span = Math.max(1, previous.max - previous.min);
  var changed = min < previous.min - span * 0.18 || max > previous.max + span * 0.18;
  if (changed) {
    chartScaleCache[id] = {
      min: previous.min * 0.75 + min * 0.25,
      max: previous.max * 0.75 + max * 0.25
    };
  }
  return chartScaleCache[id];
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

function blockMetric(label, value, unit, cls, detail, tip) {
  return '<div class="blockMetric ' + esc(cls || "") + (tip ? ' hasTip' : '') + '"' + (tip ? ' tabindex="0"' : '') + '><span>' + esc(label) + (tip ? '<i class="infoMark">i</i>' : '') + '</span><b>' + esc(fmt(value)) + (unit ? ' <em>' + esc(unit) + '</em>' : '') + '</b>' + (detail ? '<small>' + esc(detail) + '</small>' : '') + (tip ? '<div class="hoverTip simpleTip">' + esc(tip) + '</div>' : '') + '</div>';
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
  var html = "";
  var signedPower = Number(state.gridPowerW);
  var filteredPower = Number(state.gridPowerFilteredW);
  var hasSignedPower = isFinite(signedPower);
  var importPower = hasSignedPower ? Math.max(0, signedPower) : null;
  var exportPower = hasSignedPower ? Math.max(0, -signedPower) : null;
  var balanceCls = !hasSignedPower ? "muted" : (signedPower < -20 ? "solar" : (signedPower > 20 ? "consume" : "ok"));
  var balanceState = !hasSignedPower ? "mesure absente" : (signedPower < -20 ? "injection reseau" : (signedPower > 20 ? "achat reseau" : "proche de zero"));
  html += blockMetric("Equilibre reseau", hasSignedPower ? signedPower : "N/A", hasSignedPower ? "W" : "", balanceCls, "positif achat / negatif injection", "+ = achat reseau, - = injection reseau. L'objectif est de rester proche de 0 W.");
  html += blockMetric("Etat", balanceState, "", balanceCls, "objectif routeur: rester pres de 0 W");
  html += blockMetric("Achat reseau", hasSignedPower ? importPower : "N/A", hasSignedPower ? "W" : "", importPower > 0 ? "consume" : "muted", "part positive de la puissance reseau", "Partie positive de la puissance reseau.");
  html += blockMetric("Injection reseau", hasSignedPower ? exportPower : "N/A", hasSignedPower ? "W" : "", exportPower > 0 ? "solar" : "muted", "part negative convertie en valeur positive", "Partie negative de la puissance reseau, affichee en positif.");
  html += blockMetric("Source active", state.gridPowerSource || "JSY", "", "info", sourceStatusDetail());
  html += blockMetric("Mesure filtree", isFinite(filteredPower) ? filteredPower : "N/A", isFinite(filteredPower) ? "W" : "", "muted", "valeur utilisee par la regulation");
  return html;
}

function routingHeaterMetrics() {
  var heaterActuators = configuredActuatorsByUsage("water_heater");
  var homeHeatingActuators = configuredActuatorsByUsage("home_heating");
  var surplus = Math.max(0, Number(state.surplusW) || 0);
  var heater = Math.max(0, Number(state.heaterPowerW) || 0);
  var routed = heaterActuators.concat(homeHeatingActuators);
  var maxPct = routed.reduce(function (max, actuator) {
    return Math.max(max, actuatorCommandPercent(actuator));
  }, 0);
  var command = hasValue(state.commandPercent) ? Math.max(0, Number(state.commandPercent) || 0) : maxPct;
  var cls = state.safetyTripped ? "bad" : (heater > 0 || maxPct > 0 ? "ok" : "muted");
  var status = state.safetyTripped ? "securite" : (heater > 0 || maxPct > 0 ? "actif" : "inactif");
  var html = blockMetric("Surplus disponible", surplus, "W", surplus > 0 ? "sun" : "muted", "disponible pour routage", "Puissance estimee disponible pour le routage.");
  if (heaterActuators.length) {
    html += blockMetric("Puissance routeur", heater, "W", heater > 0 ? "heat" : "muted", "toujours positive", "Puissance reellement envoyee vers la sortie routeur. Toujours affichee en positif.");
    heaterActuators.forEach(function (actuator) {
      var pct = Math.max(0, actuatorCommandPercent(actuator));
      html += blockMetric(actuator.name || actuator.id || "Sortie routeur", pct, "%", pct > 0 ? "ok" : "muted", "routeur", "Commande appliquee a la sortie routeur. 100 % = puissance maximale autorisee.");
    });
  }
  homeHeatingActuators.forEach(function (actuator) {
    var pct = Math.max(0, actuatorCommandPercent(actuator));
    var estimatedPower = actuatorEstimatedPowerW(actuator, pct);
    html += blockMetric(actuator.name || actuator.id || "Sortie auxiliaire", pct, "%", pct > 0 ? "info" : "muted", estimatedPower != null ? fmt(estimatedPower) + " W estime" : "sortie auxiliaire", "Usage secondaire du surplus.");
  });
  if (!routed.length) html += blockMetric("Usage routeur", "non configure", "", "muted", "selectionne un usage dans Actionneurs");
  html += blockMetric("Commande routeur", command, "%", command > 0 ? "info" : "muted", "sortie calculee");
  html += blockMetric("Etat routage", status, "", cls, state.pidStatus || "routeur");
  return html;
}

function configuredHeaterActuator() {
  return configuredActuatorsByUsage("water_heater")[0] || null;
}

function configuredActuatorsByUsage(usage) {
  return getActuators().filter(function (a) {
    return actuatorUsage(a) === usage;
  });
}

function isHeaterActuator(actuator) {
  return actuatorUsage(actuator) === "water_heater";
}

function actuatorUsage(actuator) {
  if (!actuator) return "";
  if (actuator.usage) return String(actuator.usage);
  if (actuator.heater === true || actuator.waterHeater === true || actuator.isWaterHeater === true) return "water_heater";
  return "";
}

function actuatorUsageLabel(value) {
  var labels = {
    "":"aucun",
    water_heater:"routeur",
    home_heating:"chauffage maison",
    auxiliary:"auxiliaire",
    diagnostic:"test / diagnostic"
  };
  return labels[value || ""] || value || "aucun";
}

function actuatorUsageClass(value) {
  if (value === "water_heater") return "warn";
  if (value === "home_heating") return "info";
  if (value === "auxiliary") return "ok";
  if (value === "diagnostic") return "bad";
  return "muted";
}

function actuatorUsageSelect(selected) {
  var items = [["","Aucun usage"],["water_heater","Routeur"],["home_heating","Chauffage maison"],["auxiliary","Auxiliaire"],["diagnostic","Test / diagnostic"]];
  return items.map(function (item) {
    return '<option value="' + esc(item[0]) + '" ' + (String(selected || "") === item[0] ? "selected" : "") + '>' + esc(item[1]) + '</option>';
  }).join("");
}

function actuatorConfigList() {
  return (cache.actuators && Array.isArray(cache.actuators.actuators)) ? cache.actuators.actuators : [];
}

function actuatorRuntimeList() {
  if (state && Array.isArray(state.actuators)) return state.actuators;
  if (cache.actuatorRuntime && Array.isArray(cache.actuatorRuntime.actuators)) return cache.actuatorRuntime.actuators;
  return [];
}

function actuatorOrigin(actuator) {
  var origin = actuator && (actuator.origin || actuator.source);
  origin = String(origin || "LOCAL").toUpperCase();
  return origin === "ESPNOW" || origin === "ESPNOW_REMOTE" ? "ESP-NOW" : origin;
}

function normalizeActuatorFromConfig(cfg) {
  cfg = cfg || {};
  var command = hasValue(cfg.commandPercent) ? Number(cfg.commandPercent) : 0;
  var maxPower = Number(cfg.maxPowerW);
  return Object.assign({}, cfg, {
    origin:actuatorOrigin(cfg),
    enabled:cfg.enabled !== false,
    ok:cfg.enabled !== false,
    commandPercent:command,
    outputPercent:hasValue(cfg.outputPercent) ? Number(cfg.outputPercent) : command,
    outputOn:hasValue(cfg.outputOn) ? !!cfg.outputOn : command > 0,
    estimatedPowerW:hasValue(cfg.estimatedPowerW) ? Number(cfg.estimatedPowerW) : (isFinite(maxPower) && maxPower > 0 ? maxPower * command / 100 : null),
    values:cfg.values && typeof cfg.values === "object" ? cfg.values : {}
  });
}

function getActuators() {
  var byId = {};
  actuatorConfigList().forEach(function (cfg) {
    if (!cfg || !cfg.id) return;
    byId[String(cfg.id)] = normalizeActuatorFromConfig(cfg);
  });
  actuatorRuntimeList().forEach(function (runtime) {
    if (!runtime || !runtime.id) return;
    var id = String(runtime.id);
    byId[id] = Object.assign({}, byId[id] || {}, runtime, {
      origin:actuatorOrigin(runtime),
      values:runtime.values && typeof runtime.values === "object" ? runtime.values : {}
    });
  });
  return Object.keys(byId).map(function (id) { return byId[id]; }).sort(function (a, b) {
    if (!!a.ok !== !!b.ok) return a.ok ? -1 : 1;
    return String(a.name || a.id).localeCompare(String(b.name || b.id));
  });
}

function getActuatorById(id) {
  id = String(id || "");
  return getActuators().filter(function (actuator) { return String(actuator.id || "") === id; })[0] || null;
}

function getActuatorsByType(type) {
  type = String(type || "").toUpperCase();
  return getActuators().filter(function (actuator) { return String(actuator.type || "").toUpperCase() === type; });
}

function getActuatorsByOrigin(origin) {
  origin = String(origin || "").toUpperCase();
  if (origin === "ESPNOW") origin = "ESP-NOW";
  return getActuators().filter(function (actuator) { return actuatorOrigin(actuator) === origin; });
}

function getActuatorCommandPercent(actuator) {
  if (!actuator) return 0;
  var runtime = actuator.id ? getActuatorById(actuator.id) : actuator;
  var source = runtime || actuator;
  if (hasValue(source.commandPercent)) return Math.max(0, Number(source.commandPercent) || 0);
  if (source.values && hasValue(source.values.commandPercent)) return Math.max(0, Number(source.values.commandPercent) || 0);
  return 0;
}

function getActuatorEstimatedPowerW(actuator) {
  if (!actuator) return null;
  var runtime = actuator.id ? getActuatorById(actuator.id) : actuator;
  var source = runtime || actuator;
  if (hasValue(source.estimatedPowerW)) return Number(source.estimatedPowerW);
  if (source.values && hasValue(source.values.estimatedPowerW)) return Number(source.values.estimatedPowerW);
  var maxPower = Number(source.maxPowerW);
  if (!isFinite(maxPower) || maxPower <= 0) return null;
  return maxPower * getActuatorCommandPercent(source) / 100;
}

function actuatorCommandPercent(actuator) {
  return getActuatorCommandPercent(actuator);
}

function actuatorEstimatedPowerW(actuator, pct) {
  if (hasValue(pct)) {
    var maxPower = Number(actuator && actuator.maxPowerW);
    if (!isFinite(maxPower) || maxPower <= 0) return getActuatorEstimatedPowerW(actuator);
    return maxPower * Math.max(0, Number(pct) || 0) / 100;
  }
  return getActuatorEstimatedPowerW(actuator);
}

function dsLabel(index) {
  var ds = cache.sensors && Array.isArray(cache.sensors.ds18b20) ? cache.sensors.ds18b20 : null;
  var item = ds && ds[index] ? ds[index] : null;
  return (item && (item.name || item.label || item.id)) || ("Sonde " + (index + 1));
}

function sourceStatusDetail() {
  if ((state.gridPowerSource || "JSY") === "TIC") return state.ticAvailable ? "Linky OK" : "Linky absent";
  if ((state.gridPowerSource || "JSY") === "AUTO") return state.ticAvailable ? "AUTO via Linky" : (state.jsyOnline ? "AUTO via JSY" : "aucune source");
  return state.jsyOnline ? "JSY OK" : "JSY absent";
}

function sourcePowerMetrics() {
  var html = "";
  var sensors = getPowerSensors();
  sensors.forEach(function (sensor) {
    var values = canonicalSensorValues(sensor);
    Object.keys(values).forEach(function (key) {
      if (!isPowerLikeValueKey(key)) return;
      var value = getSensorValue(sensor, key);
      if (!hasValue(value)) return;
      var cls = Number(value) < 0 ? "solar" : "consume";
      html += blockMetric((sensor.name || sensor.id) + " / " + labelForSensorValueKey(sensor, key), value, unitForValueKey(key), cls, sensor.origin + " - " + (sensor.ok ? "OK" : "perdu"));
    });
  });
  return html || blockMetric("Sources puissance", "aucune", "", "muted", "aucun capteur de puissance dans sensors[]");
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
    blockMetric("Puissance estimee", heater, "W", "heat", "routeur") +
    blockMetric("Kp / Ki / Kd", fmt(state.pidKp) + " / " + fmt(state.pidKi) + " / " + fmt(state.pidKd), "", "muted", "coefficients") +
    blockMetric("Rampe", state.maxOutputRampPercentPerSecond, "%/s", "muted", "limite variation");
}

function safetyDashboardMetrics() {
  var level = state.safetyLevel || (state.safetyTripped ? "CRITICAL" : "OK");
  var cls = state.safetyTripped || level === "CRITICAL" ? "bad" : (level === "WARNING" || level === "DEGRADED" ? "warn" : "ok");
  var reason = state.safetyReason || "aucun defaut";
  var blocked = state.safetyTripped ? "oui" : "non";
  return blockMetric("Securite", level, "", cls, reason, "Indique si une protection bloque ou limite le routage.") +
    blockMetric("Defaut actif", reason, "", cls === "ok" ? "muted" : cls, cls === "ok" ? "aucun blocage" : "a verifier") +
    blockMetric("Blocage routeur", blocked, "", state.safetyTripped ? "bad" : "ok", state.safetyTripped ? "sortie bloquee" : "routage autorise") +
    blockMetric("Mesure puissance", state.gridPowerSource || "JSY", "", hasValue(state.gridPowerW) ? "ok" : "warn", sourceStatusDetail()) +
    blockMetric("Sondes temperature", temperatureSafetySummary(), "", temperatureSafetyClass(), "DS18B20");
}

function temperatureSafetySummary() {
  var sensors = getTemperatureSensors();
  var live = sensors.filter(function (sensor) {
    return sensor.ok && hasValue(sensorTemperatureValue(sensor));
  }).length;
  if (!sensors.length) return "non configurees";
  return live + "/" + sensors.length + " OK";
}

function temperatureSafetyClass() {
  var sensors = getTemperatureSensors();
  var live = 0;
  var warn = false;
  sensors.forEach(function (sensor) {
    var temp = sensorTemperatureValue(sensor);
    if (sensor.ok && hasValue(temp)) {
      live++;
      if (Number(temp) >= 60) warn = true;
    }
  });
  if (!sensors.length) return "muted";
  if (live < sensors.length) return "bad";
  return warn ? "warn" : "ok";
}

function routerModeValue() {
  return state.routerMode || state.mode || state.routingMode || (state.pidEnabled === false ? "OFF" : "AUTO");
}

function routerModeMetrics() {
  var mode = String(routerModeValue() || "AUTO").toUpperCase();
  var cls = mode === "OFF" ? "muted" : (mode === "FORCED" || mode === "FORCE" || mode === "TEST" ? "warn" : "ok");
  var tip = mode === "OFF" ? "Sortie desactivee." : (mode === "FORCED" || mode === "FORCE" ? "Sortie activee manuellement, dans les limites de securite." : "Le routeur ajuste automatiquement la puissance selon le surplus.");
  var regulation = state.pidEnabled === false ? "inactive" : "active";
  var error = hasValue(state.pidErrorW) ? state.pidErrorW : (Number(state.gridSetpointW || 0) - Number(state.gridPowerFilteredW || state.gridPowerW || 0));
  var output = hasValue(state.commandPercent) ? state.commandPercent : state.pidOutputPercent;
  return blockMetric("Mode routeur", mode, "", cls, "pilotage principal", tip) +
    blockMetric("Regulation", regulation, "", state.pidEnabled === false ? "muted" : "ok", state.pidStatus || "PID") +
    blockMetric("Consigne reseau", hasValue(state.gridSetpointW) ? state.gridSetpointW : 0, "W", "info", "objectif reseau") +
    blockMetric("Erreur reseau", error, "W", Math.abs(Number(error) || 0) > Number(state.deadbandW || 30) ? "warn" : "ok", "consigne - mesure") +
    blockMetric("Sortie routeur", Math.max(0, Number(output) || 0), "%", Number(output) > 0 ? "ok" : "muted", "commande finale");
}

function tipWrap(html, tip) {
  return '<span class="tipWrap' + (tip ? ' hasTip' : '') + '"' + (tip ? ' tabindex="0"' : '') + '>' + html + (tip ? '<i class="infoMark">i</i><span class="hoverTip simpleTip">' + esc(tip) + '</span>' : '') + '</span>';
}

function supervisionBadge(text, cls, tip) {
  return tipWrap('<span class="badge ' + esc(cls || "muted") + '">' + esc(text) + '</span>', tip);
}

function supervisionValue(value, unit, cls, tip) {
  return tipWrap('<b class="' + esc(cls || "") + '">' + esc(fmt(value)) + (unit ? ' <em>' + esc(unit) + '</em>' : '') + '</b>', tip);
}

function supervisionRow(zone, stateText, stateCls, valueHtml, detail, trend, tip) {
  return '<div class="supervisionRow">' +
    '<div class="supervisionZone">' + tipWrap('<strong>' + esc(zone) + '</strong>', tip) + '</div>' +
    '<div class="supervisionState">' + supervisionBadge(stateText, stateCls, tip) + '</div>' +
    '<div class="supervisionValue">' + valueHtml + '</div>' +
    '<div class="supervisionDetail">' + esc(detail || "") + '</div>' +
    '<div class="supervisionTrend">' + esc(trend || "") + '</div>' +
  '</div>';
}

function networkSupervisionRow() {
  var signedPower = Number(state.gridPowerW);
  var valid = isFinite(signedPower);
  var importW = valid ? Math.max(0, signedPower) : null;
  var injectionW = valid ? Math.max(0, -signedPower) : null;
  var cls = !valid ? "muted" : (signedPower < -20 ? "solar" : (signedPower > 20 ? "consume" : "ok"));
  var status = !valid ? "mesure absente" : (signedPower < -20 ? "injection" : (signedPower > 20 ? "achat" : "equilibre"));
  var detail = "achat " + fmt(importW) + " W / injection " + fmt(injectionW) + " W";
  var trend = state.gridPowerSource || "JSY";
  return supervisionRow("Reseau", status, cls, supervisionValue(valid ? signedPower : "N/A", valid ? "W" : "", cls, "+ = achat reseau, - = injection reseau."), detail, trend, "Puissance reseau. L'objectif du routeur est de rester proche de 0 W.");
}

function routingSupervisionRows() {
  var rows = "";
  var heaterActuators = configuredActuatorsByUsage("water_heater");
  var homeHeatingActuators = configuredActuatorsByUsage("home_heating");
  var surplus = Math.max(0, Number(state.surplusW) || 0);
  var heater = Math.max(0, Number(state.heaterPowerW) || 0);
  var routed = heaterActuators.concat(homeHeatingActuators);
  var maxPct = routed.reduce(function (max, actuator) { return Math.max(max, actuatorCommandPercent(actuator)); }, 0);
  var cls = state.safetyTripped ? "bad" : (heater > 0 || maxPct > 0 ? "ok" : "muted");
  var status = state.safetyTripped ? "securite" : (heater > 0 || maxPct > 0 ? "actif" : "inactif");
  var detail = routed.length ? routed.map(function (actuator) {
    return (actuator.name || actuator.id || actuatorUsageLabel(actuatorUsage(actuator))) + " " + fmt(actuatorCommandPercent(actuator)) + " %";
  }).join(" / ") : "aucun usage routeur configure";
  rows += supervisionRow("Routage", status, cls, supervisionValue(surplus, "W", surplus > 0 ? "sun" : "muted", "Puissance estimee disponible pour le routage."), detail, "surplus", "Affiche si le surplus est consomme par un usage configure.");
  if (heaterActuators.length) {
    rows += supervisionRow("Routeur", heater > 0 ? "actif" : "pret", heater > 0 ? "ok" : "muted", supervisionValue(heater, "W", heater > 0 ? "heat" : "muted", "Puissance envoyee a la sortie routeur, toujours positive."), heaterActuators.map(function (a) { return (a.name || a.id) + " " + fmt(actuatorCommandPercent(a)) + " %"; }).join(" / "), "sortie", "Usage routeur configure dans Actionneurs.");
  }
  homeHeatingActuators.forEach(function (actuator) {
    var pct = Math.max(0, actuatorCommandPercent(actuator));
    var power = actuatorEstimatedPowerW(actuator, pct);
    rows += supervisionRow("Chauffage maison", pct > 0 ? "actif" : "pret", pct > 0 ? "info" : "muted", supervisionValue(power == null ? pct : power, power == null ? "%" : "W", pct > 0 ? "info" : "muted", "Usage secondaire du surplus, par exemple en hiver."), (actuator.name || actuator.id) + " - " + fmt(pct) + " %", "priorite 2", "Affiche seulement les actionneurs avec l'usage Chauffage maison.");
  });
  return rows;
}

function temperaturesSupervisionRow() {
  var live = getTemperatureSensors().filter(function (sensor) {
    return sensor.ok && hasValue(sensorTemperatureValue(sensor));
  }).map(function (sensor) {
    return {label:sensor.name || sensor.id, value:Number(sensorTemperatureValue(sensor)), origin:sensor.origin};
  });
  var main = live.length ? live[0] : null;
  var cls = temperatureSafetyClass();
  var status = cls === "bad" ? "defaut" : (cls === "warn" ? "attention" : "OK");
  var detail = live.length ? live.map(function (item) { return item.label + " " + fmt(item.value) + " C"; }).join(" / ") : temperatureSafetySummary();
  return supervisionRow("Temperatures", status, cls, supervisionValue(main ? main.value : "N/A", main ? "C" : "", cls, "Temperature issue de sensors[]."), detail, "sensors[]", "Permet de verifier les sondes temperature disponibles.");
}

function safetySupervisionRow() {
  var level = state.safetyLevel || (state.safetyTripped ? "CRITICAL" : "OK");
  var cls = state.safetyTripped || level === "CRITICAL" ? "bad" : (level === "WARNING" || level === "DEGRADED" ? "warn" : "ok");
  var reason = state.safetyReason || "aucun defaut";
  return supervisionRow("Securite", level, cls, supervisionValue(level, "", cls, "Indique si une protection bloque ou limite le routage."), reason, state.safetyTripped ? "bloque" : "autorise", "Ce point doit rester visible immediatement en cas de defaut.");
}

function modeSupervisionRow() {
  var mode = String(routerModeValue() || "AUTO").toUpperCase();
  var cls = mode === "OFF" ? "muted" : (mode === "FORCED" || mode === "FORCE" || mode === "TEST" ? "warn" : "ok");
  var output = hasValue(state.commandPercent) ? state.commandPercent : state.pidOutputPercent;
  var detail = "regulation " + (state.pidEnabled === false ? "inactive" : "active") + " / sortie " + fmt(Math.max(0, Number(output) || 0)) + " %";
  return supervisionRow("Mode routeur", mode, cls, supervisionValue(mode, "", cls, "AUTO ajuste automatiquement la puissance selon le surplus."), detail, state.pidStatus || "PID", "OFF desactive le routage. FORCE/TEST doivent rester sous controle des securites.");
}

function dashboardSupervisionTable() {
  return '<section class="supervisionPanel"><div class="wideChartHead"><b>Supervision</b><span>lecture rapide</span></div>' +
    '<div class="supervisionTable">' +
      '<div class="supervisionHeader"><span>Zone</span><span>Etat</span><span>Valeur</span><span>Detail</span><span>Tendance</span></div>' +
      networkSupervisionRow() +
      routingSupervisionRows() +
      temperaturesSupervisionRow() +
      safetySupervisionRow() +
      modeSupervisionRow() +
    '</div></section>';
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

function selectedGraphSeries(keys, forceMinZero) {
  return keys.map(function (key) {
    var meta = seriesByKeyOrNull(key);
    return meta ? {key:meta.key, label:meta.label, cls:meta.cls, unit:meta.unit, min:forceMinZero ? 0 : meta.min, max:meta.max} : null;
  }).filter(Boolean);
}

function graphCardFromSeries(title, keys, unit, forceMinZero) {
  var series = selectedGraphSeries(keys, forceMinZero);
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
  var routingKeys = dashboardGraphRouting.slice();
  if (graphConfig.showTarget && networkKeys.indexOf("targetW") < 0) networkKeys.push("targetW");
  if (graphConfig.showDeadband) {
    if (networkKeys.indexOf("deadbandHighW") < 0) networkKeys.push("deadbandHighW");
    if (networkKeys.indexOf("deadbandLowW") < 0) networkKeys.push("deadbandLowW");
  }
  if (!graphConfig.showHeaterPower) routingKeys = routingKeys.filter(function (key) { return key !== "heaterPowerW"; });
  if (graphConfig.showEnergy) {
    html += graphCardFromSeries("Equilibre reseau - puissance reseau", networkKeys, "W");
    html += graphCardFromSeries("Routage routeur - W", routingKeys, "W", true);
  }
  if (graphConfig.showTemperatures) html += '<section class="wideChart"><div class="wideChartHead"><b>Temperatures</b><span>fusionne</span></div><div class="sparkline muted">Voir le graphe Temperatures principal, recalcule depuis sensors[].</div></section>';
  return html || '<section class="wideChart"><div class="wideChartHead"><b>Graphiques</b><span>aucune courbe active</span></div><div class="sparkline muted">active au moins une courbe dans la personnalisation</div></section>';
}

function configuredDsCards() {
  return getTemperatureSensors().map(function (sensor) {
    var temp = sensorTemperatureValue(sensor);
    var live = sensor.ok && hasValue(temp);
    return dashboardCard("T", sensor.name || sensor.id, live ? temp : "absent", live ? "C" : "", live ? tempClass(temp) : "bad", live ? sensor.origin + " OK" : "non lu");
  }).join("");
}

function configuredDsMetrics() {
  var html = getTemperatureSensors().map(function (sensor) {
    var temp = sensorTemperatureValue(sensor);
    var live = sensor.ok && hasValue(temp);
    return blockMetric(sensor.name || sensor.id, live ? temp : "absent", live ? "C" : "", live ? tempClass(temp) : "bad", live ? sensor.origin + " OK" : "non lu", "Temperature mesuree depuis sensors[].");
  }).join("");
  return html || blockMetric("Temperature", "aucune", "", "muted", "pas de capteur temperature dans sensors[]");
}

function objectHasOwn(object, key) {
  return !!(object && Object.prototype.hasOwnProperty.call(object, key));
}

function energyPowerValue(energy, key, legacyValue) {
  return powerOrNull(objectHasOwn(energy, key) ? energy[key] : legacyValue);
}

function energyNumberValue(energy, key, legacyValue) {
  return finiteOrNull(objectHasOwn(energy, key) ? energy[key] : legacyValue);
}

function stateEnergy() {
  var e = state.energy || {};
  var temperatureSensors = getTemperatureSensors();
  var primaryTemperatureSensor = temperatureSensors.filter(function (sensor) {
    return sensor.ok && hasValue(sensorTemperatureValue(sensor));
  })[0] || null;
  var primaryTemperature = primaryTemperatureSensor ? sensorTemperatureValue(primaryTemperatureSensor) : null;
  return {
    productionW: energyPowerValue(e, "productionW", state.productionW),
    gridPowerW: energyPowerValue(e, "gridPowerW", state.gridPowerW),
    heaterPowerW: energyPowerValue(e, "heaterPowerW", state.heaterPowerW),
    heaterPercent: energyNumberValue(e, "heaterPercent", state.commandPercent),
    tankTempC: energyNumberValue(e, "tankTempC", primaryTemperature),
    surplusW: energyPowerValue(e, "surplusW", state.surplusW),
    consumptionW: energyPowerValue(e, "consumptionW", state.consumptionW)
  };
}

function pidState() {
  var p = state.pid || {};
  var measured = powerOrNull(hasValue(state.pidMeasuredW) ? state.pidMeasuredW : (hasValue(state.gridPowerFilteredW) ? state.gridPowerFilteredW : state.gridPowerW));
  var target = p.targetGridW != null ? p.targetGridW : (hasValue(state.gridSetpointW) ? state.gridSetpointW : 0);
  var grid = stateEnergy().gridPowerW;
  var computedError = hasValue(grid) && hasValue(target) ? Number(grid) - Number(target) : null;
  var providedError = powerOrNull(p.errorW != null ? p.errorW : (hasValue(state.pidErrorW) ? state.pidErrorW : null));
  var error = powerOrNull(providedError);
  if (!hasValue(error) || (Number(error) === 0 && hasValue(computedError) && Math.abs(Number(computedError)) > 20)) error = computedError;
  return {
    mode: p.mode || routerModeValue(),
    source: p.source || state.gridPowerSource || "AUTO",
    targetGridW: finiteOrNull(target),
    deadbandW: finiteOrNull(p.deadbandW != null ? p.deadbandW : state.deadbandW),
    errorW: powerOrNull(error),
    outputPercent: finiteOrNull(p.outputPercent != null ? p.outputPercent : state.pidOutputPercent),
    commandPercent: finiteOrNull(p.commandPercent != null ? p.commandPercent : state.commandPercent),
    status: p.status || state.pidStatus || "IDLE",
    heaterPowerW: powerOrNull(p.heaterPowerW != null ? p.heaterPowerW : state.heaterPowerW)
  };
}

function routerModeDisplay(p) {
  if (state.safetyTripped || state.safetyLevel === "CRITICAL") return "SECURITE";
  var mode = String((p && p.mode) || routerModeValue() || "AUTO").toUpperCase();
  if (mode === "FORCE" || mode === "FORCED" || mode === "MANUAL" || mode === "MANUEL" || mode === "TEST") return "MANUEL";
  if (mode === "OFF" || state.pidEnabled === false) return "OFF";
  return "AUTO";
}

function activeMeasureSourceLabel(p) {
  var source = String((p && p.source) || state.gridPowerSource || "AUTO");
  var upper = source.toUpperCase();
  var sensors = activeSensorsList();
  var matched = sensors.filter(function (sensor) {
    var text = String(sensor.id || "") + " " + String(sensor.name || "") + " " + String(sensor.type || "");
    text = text.toUpperCase();
    if (upper.indexOf("JSY") >= 0) return text.indexOf("JSY") >= 0;
    if (upper.indexOf("LINKY") >= 0 || upper.indexOf("TIC") >= 0) return text.indexOf("LINKY") >= 0 || text.indexOf("TIC") >= 0;
    return false;
  })[0] || null;
  var origin = matched ? matched.origin : (upper.indexOf("ESPNOW") >= 0 || upper.indexOf("ESP-NOW") >= 0 ? "ESP-NOW" : "LOCAL");
  if (upper.indexOf("JSY") >= 0) return "JSY " + origin;
  if (upper.indexOf("LINKY") >= 0 || upper.indexOf("TIC") >= 0) return "Linky " + origin;
  return source || "--";
}

function pidStatusDisplay(p, command, stable) {
  if (state.safetyTripped || state.safetyLevel === "CRITICAL") return "SECURITY";
  var status = String((p && p.status) || state.pidStatus || "").toUpperCase();
  if (status === "SECURITE") status = "SECURITY";
  var detected = detectedPidState(p, command, stable);
  if (detected !== "IDLE" && detected !== "ACTIVE") return detected;
  if (status && status !== "IDLE") return status;
  if (detected) return detected;
  return status || "IDLE";
}

function pidErrorHistoryStats() {
  var points = dashHistory.slice(-40).map(function (point) {
    return hasValue(point.pidErrorW) ? Number(point.pidErrorW) : null;
  }).filter(function (value) {
    return value != null && isFinite(value);
  });
  if (points.length < 8) return {count:points.length, signChanges:0, amplitude:0};
  var signChanges = 0;
  var previousSign = 0;
  var min = points[0];
  var max = points[0];
  points.forEach(function (value) {
    min = Math.min(min, value);
    max = Math.max(max, value);
    var sign = value > 0 ? 1 : (value < 0 ? -1 : 0);
    if (sign && previousSign && sign !== previousSign) signChanges++;
    if (sign) previousSign = sign;
  });
  return {count:points.length, signChanges:signChanges, amplitude:max - min};
}

function detectedPidState(p, command, stable) {
  if (state.safetyTripped || state.safetyLevel === "CRITICAL") return "SECURITY";
  var error = hasValue(p && p.errorW) ? Math.abs(Number(p.errorW)) : null;
  var deadband = Math.max(1, Math.abs(Number((p && p.deadbandW) || 30)));
  if (hasValue(command) && error != null && error > deadband && (command <= 1 || command >= 99)) return "SATURATION";
  var stats = pidErrorHistoryStats();
  if (stats.count >= 8 && stats.signChanges >= 4 && stats.amplitude > deadband * 2) return "OSCILLATION";
  if (stable) return "STABLE";
  if ((hasValue(command) && command > 1) || (error != null && error > deadband)) return "ACTIVE";
  return "IDLE";
}

function dashboardValue(value, unit) {
  return hasValue(value) ? esc(fmt(Number(value))) + (unit ? ' <em>' + esc(unit) + '</em>' : '') : '--';
}

function signedDashboardValue(value, unit) {
  if (!hasValue(value)) return '--';
  var n = Number(value);
  return esc((n > 0 ? "+" : "") + fmt(n)) + (unit ? ' <em>' + esc(unit) + '</em>' : '');
}

function ageLabel(ms) {
  if (!hasValue(ms) || Number(ms) === 4294967295) return "--";
  var seconds = Math.max(0, Math.round(Number(ms) / 1000));
  if (seconds < 90) return seconds + " s";
  var minutes = Math.round(seconds / 60);
  if (minutes < 90) return minutes + " min";
  return Math.round(minutes / 60) + " h";
}

function roleInfo() {
  var role = String(state.role || "").toUpperCase();
  if (role === "MASTER") return {label:"MASTER", detail:"regulation active possible", pidExecution:"active localement"};
  if (role === "BACKUP") return {label:"BACKUP", detail:"veille / secours", pidExecution:"veille secours"};
  if (role === "NODE_SENSOR") return {label:"NODE_SENSOR", detail:"acquisition seulement", pidExecution:"inactive sur NODE_SENSOR"};
  if (role === "NODE_ACTUATOR") return {label:"NODE_ACTUATOR", detail:"actionneur seulement", pidExecution:"inactive sur NODE_ACTUATOR"};
  if (role === "NODE_MIXED") return {label:"NODE_MIXED", detail:"acquisition + action locale", pidExecution:"locale si active"};
  return {label:role || "-", detail:"role non renseigne", pidExecution:"non determinee"};
}

function gridBalanceInfo(value) {
  if (!hasValue(value)) return {label:"Reseau", value:"--", cls:"muted", detail:"donnee non disponible", flow:"unknown"};
  var grid = Number(value);
  if (grid > 20) return {label:"ACHAT RESEAU", value:signedDashboardValue(grid, "W"), cls:"consume", detail:"appoint depuis reseau", flow:"buy"};
  if (grid < -20) return {label:"INJECTION", value:signedDashboardValue(grid, "W"), cls:"solar", detail:"surplus vers reseau", flow:"inject"};
  return {label:"EQUILIBRE", value:dashboardValue(0, "W"), cls:"ok", detail:"reseau proche de zero", flow:"neutral"};
}

function gridDerived(value) {
  if (!hasValue(value)) return {signed:null, buy:null, injection:null, nearZero:false};
  var grid = Number(value);
  return {
    signed:grid,
    buy:Math.max(grid, 0),
    injection:Math.max(-grid, 0),
    nearZero:Math.abs(grid) <= 20
  };
}

function gridDerivedForGraph(value) {
  var derived = gridDerived(value);
  if (!hasValue(derived.signed)) return {buy:null, injection:null};
  return {
    buy:derived.buy > 20 ? derived.buy : null,
    injection:derived.injection > 20 ? derived.injection : null
  };
}

function productionInfo(value) {
  if (!hasValue(value)) return {value:"--", cls:"muted", detail:"donnee non disponible"};
  var n = Number(value);
  return {value:dashboardValue(n, "W"), cls:n > 0 ? "solar" : "muted", detail:n > 0 ? "PV actif" : "production nulle"};
}

function pidExecutionInfo(p) {
  var role = roleInfo();
  var mode = String(p.mode || "").toUpperCase();
  if (state.safetyTripped || state.safetyLevel === "CRITICAL") return "inactive : securite active";
  if (mode === "OFF") return "inactive : mode OFF";
  if (role.label === "MASTER") return "active localement";
  if (role.label === "NODE_ACTUATOR" && hasValue(p.commandPercent)) return "commande recue / PID non local";
  return role.pidExecution;
}

function pidDiagnosticText(p) {
  var e = stateEnergy();
  var role = roleInfo().label;
  var grid = Number(e.gridPowerW);
  var output = Number(p.commandPercent || p.outputPercent || 0);
  var deadband = Number(p.deadbandW || 30);
  var mode = String(p.mode || "").toUpperCase();
  if (state.safetyTripped || state.safetyLevel === "CRITICAL") return "PID inactif : securite active";
  if (mode === "OFF") return "PID inactif : mode OFF";
  if (role === "NODE_SENSOR") return "PID inactif : role NODE_SENSOR";
  if (role === "NODE_ACTUATOR") return "PID non local : actionneur seulement";
  if (!hasValue(e.gridPowerW)) return "PID inactif : source reseau absente";
  if (grid > deadband && output <= 1) return "Pas de surplus : routeur a 0 %";
  if (grid < -deadband && output > 1) return "PID actif : surplus absorbe";
  if (Math.abs(Number(p.errorW || 0)) <= deadband) return "PID stable dans la deadband";
  return "PID actif : regulation en cours";
}

function isNodeActuatorRole() {
  return roleInfo().label === "NODE_ACTUATOR";
}

function actuatorStateText() {
  var states = [];
  if (state.ssr1OutputOn || state.ssr1PinHigh) states.push("SSR1 actif");
  if (state.ssr2OutputOn || state.ssr2PinHigh) states.push("SSR2 actif");
  if (state.robotDynOutputOn || state.robotDynPinHigh) states.push("RobotDyn actif");
  return states.length ? states.join(" + ") : "sortie inactive";
}

function unitForValueKey(key) {
  key = String(key || "").toUpperCase();
  if (key.indexOf("TEMP") >= 0) return "C";
  if (key.indexOf("PERCENT") >= 0 || key === "SOC" || key === "HUM") return "%";
  if (key.indexOf("VOLT") >= 0 || key === "BATV") return "V";
  if (key.indexOf("CURR") >= 0 || key === "IINST" || key === "BATA") return "A";
  if (key.indexOf("ENERGY") >= 0) return "Wh";
  if (key.indexOf("RSSI") >= 0) return "dBm";
  if (key.indexOf("HEAP") >= 0) return "B";
  if (key === "PAPP") return "VA";
  if (key.indexOf("FREQ") >= 0) return "Hz";
  if (key.indexOf("POWER") >= 0 || key === "GRID" || key.indexOf("W") === key.length - 1) return "W";
  return "";
}

function isPowerLikeValueKey(key) {
  key = String(key || "").toUpperCase();
  var unit = unitForValueKey(key);
  return unit === "W" || unit === "VA" || key.indexOf("SURPLUS") >= 0 || key.indexOf("INJECTION") >= 0 || key.indexOf("CONSUMPTION") >= 0 || key.indexOf("PRODUCTION") >= 0;
}

function isPlausibleSensorValueKey(key, value) {
  if (!hasValue(value)) return false;
  var n = Number(value);
  if (!isFinite(n)) return false;
  key = String(key || "").toUpperCase();
  if (key === "VALUE") return Math.abs(n) <= 100000;
  if (isPowerLikeValueKey(key)) return Math.abs(n) <= 100000;
  if (key.indexOf("TEMP") >= 0) return n >= -30 && n <= 125;
  if (key.indexOf("CURR") >= 0 || key === "IINST") return Math.abs(n) <= 500;
  if (key.indexOf("VOLT") >= 0) return n >= 0 && n <= 300;
  if (key.indexOf("FREQ") >= 0) return n >= 45 && n <= 55;
  if (key === "PF") return Math.abs(n) <= 1.2;
  if (key.indexOf("PERCENT") >= 0 || key === "SOC" || key === "HUM") return n >= 0 && n <= 100;
  return Math.abs(n) <= 1000000000;
}

function labelForValueKey(key) {
  var labels = {
    GRID:"Puissance reseau",
    POWER:"Puissance",
    VOLT:"Tension",
    CURR:"Courant",
    PAPP:"Linky PAPP",
    IINST:"Intensite Linky",
    TEMP:"Temperature",
    temperatureC:"Temperature",
    PF:"Facteur puissance",
    FREQ:"Frequence",
    CH1_CURR:"Pince 1 courant",
    CH1_POWER:"Pince 1 puissance",
    CH1_PF:"Pince 1 PF",
    CH1_ENERGY_POS:"Pince 1 energie +",
    CH1_ENERGY_NEG:"Pince 1 energie -",
    CH1_EPOS:"Pince 1 energie +",
    CH1_ENEG:"Pince 1 energie -",
    CH1_DIR:"Pince 1 sens",
    CH2_CURR:"Pince 2 courant",
    CH2_POWER:"Pince 2 puissance",
    CH2_PF:"Pince 2 PF",
    CH2_ENERGY_POS:"Pince 2 energie +",
    CH2_ENERGY_NEG:"Pince 2 energie -",
    CH2_EPOS:"Pince 2 energie +",
    CH2_ENEG:"Pince 2 energie -",
    CH2_DIR:"Pince 2 sens",
    RSSI:"RSSI",
    ageMs:"Age derniere mesure",
    heapFree:"Heap libre",
    wifiRssi:"RSSI Wi-Fi",
    errorW:"Erreur PID",
    outputPercent:"Sortie PID",
    commandPercent:"Commande",
    heaterPowerW:"Puissance routeur",
    heaterPowerPositiveW:"Puissance routeur",
    gridPowerW:"Reseau",
    gridBuyW:"Achat reseau",
    gridInjectionW:"Injection reseau",
    productionW:"Production",
    surplusW:"Surplus",
    surplusAvailableW:"Surplus disponible"
  };
  return labels[key] || labels[String(key || "").toUpperCase()] || key;
}

function labelForSensorValueKey(sensor, key) {
  if (!sensor || sensor.type !== "JSY-MK-194T") return labelForValueKey(key);
  var upper = String(key || "").toUpperCase();
  var match = upper.match(/^CH([12])_(.+)$/);
  if (!match) return labelForValueKey(key);
  var channelIndex = Number(match[1]) - 1;
  var channels = Array.isArray(sensor.channels) ? sensor.channels : defaultJsyChannels();
  var channel = channels[channelIndex] || defaultJsyChannels()[channelIndex] || {};
  var name = channel.name || channel.id || ("Pince " + match[1]);
  var suffixes = {
    CURR:"courant",
    POWER:"puissance",
    PF:"PF",
    EPOS:"energie +",
    ENEG:"energie -",
    ENERGY_POS:"energie +",
    ENERGY_NEG:"energie -",
    DIR:"sens"
  };
  return name + " " + (suffixes[match[2]] || match[2].toLowerCase());
}

function canonicalSensorValues(sensor) {
  var values = normalizeSensorValues(sensor && sensor.values);
  var out = {};
  Object.keys(values || {}).forEach(function (key) {
    if (!isPlausibleSensorValueKey(key, values[key])) return;
    var value = isPowerLikeValueKey(key) ? powerOrNull(values[key]) : values[key];
    if (hasValue(value)) out[key] = value;
  });
  if (hasValue(values.TEMP) && !hasValue(out.temperatureC)) out.temperatureC = values.TEMP;
  if (hasValue(values.GRID) && !hasValue(out.gridPowerW)) out.gridPowerW = powerOrNull(values.GRID);
  if (hasValue(values.POWER) && !hasValue(out.powerW)) out.powerW = powerOrNull(values.POWER);
  if (hasValue(values.VOLT) && !hasValue(out.voltageV)) out.voltageV = values.VOLT;
  if (hasValue(values.CURR) && !hasValue(out.currentA)) out.currentA = values.CURR;
  return out;
}

function getSensorsByType(type) {
  var expected = String(type || "").toUpperCase();
  return activeSensorsList().filter(function (sensor) {
    return String(sensor.type || "").toUpperCase() === expected;
  });
}

function getSensorsByRole(role) {
  var expected = String(role || "").toLowerCase();
  return activeSensorsList().filter(function (sensor) {
    return String(sensor.role || "").toLowerCase() === expected ||
      String(sensor.usage || "").toLowerCase() === expected ||
      String(sensor.location || "").toLowerCase() === expected;
  });
}

function getSensorValue(sensor, key) {
  if (!sensor || !key) return null;
  var values = canonicalSensorValues(sensor);
  var wanted = String(key).toUpperCase();
  if (hasValue(values[key])) return values[key];
  var keys = Object.keys(values);
  for (var i = 0; i < keys.length; i++) {
    if (String(keys[i]).toUpperCase() === wanted) return values[keys[i]];
  }
  return null;
}

function getTemperatureSensors() {
  return activeSensorsList().filter(isTemperatureSensor);
}

function getPowerSensors() {
  return activeSensorsList().filter(function (sensor) {
    var values = canonicalSensorValues(sensor);
    return Object.keys(values).some(function (key) {
      return isPowerLikeValueKey(key) && hasValue(values[key]);
    });
  });
}

function sourceStatusFromSensor(sensor) {
  if (!sensor) return "not_configured";
  return sensorStatusInfo(sensor).key;
}

function getAvailableDataSources(sourceState) {
  sourceState = sourceState || state;
  var sources = [];
  activeSensorsList().forEach(function (sensor) {
    var values = canonicalSensorValues(sensor);
    var valueList = Object.keys(values).map(function (key) {
      return {key:key, label:labelForSensorValueKey(sensor, key), unit:unitForValueKey(key), currentValue:values[key]};
    });
    if (hasValue(sensor.ageMs)) valueList.push({key:"ageMs", label:"Age derniere mesure", unit:"ms", currentValue:sensor.ageMs});
    sources.push({
      sourceType:"sensor",
      id:String(sensor.id),
      sensorId:String(sensor.id),
      name:sensor.name,
      origin:sensor.origin,
      role:sensor.role,
      usage:sensor.usage,
      location:sensor.location,
      status:sourceStatusFromSensor(sensor),
      values:valueList
    });
  });
  var p = pidState();
  sources.push({sourceType:"pid", id:"pid", name:"PID", origin:"SYSTEM", status:state.pidEnabled === false ? "not_applicable" : "ok", values:[
    {key:"errorW", label:"Erreur PID", unit:"W", currentValue:p.errorW},
    {key:"outputPercent", label:"Sortie PID", unit:"%", currentValue:p.outputPercent},
    {key:"commandPercent", label:"Commande", unit:"%", currentValue:p.commandPercent},
    {key:"targetGridW", label:"Consigne reseau", unit:"W", currentValue:p.targetGridW},
    {key:"deadbandW", label:"Deadband", unit:"W", currentValue:p.deadbandW}
  ]});
  var e = stateEnergy();
  sources.push({sourceType:"energy", id:"energy", name:"Energie calculee", origin:"SYSTEM", status:"ok", values:[
    {key:"productionW", label:"Production", unit:"W", currentValue:e.productionW},
    {key:"gridPowerW", label:"Reseau", unit:"W", currentValue:e.gridPowerW},
    {key:"gridBuyW", label:"Achat reseau", unit:"W", currentValue:gridDerivedForGraph(e.gridPowerW).buy},
    {key:"gridInjectionW", label:"Injection reseau", unit:"W", currentValue:gridDerivedForGraph(e.gridPowerW).injection},
    {key:"heaterPowerW", label:"Puissance routeur", unit:"W", currentValue:e.heaterPowerW},
    {key:"heaterPowerPositiveW", label:"Puissance routeur positive", unit:"W", currentValue:hasValue(e.heaterPowerW) ? Math.max(Number(e.heaterPowerW), 0) : null},
    {key:"heaterPercent", label:"Routeur", unit:"%", currentValue:e.heaterPercent},
    {key:"tankTempC", label:"Temperature principale", unit:"C", currentValue:e.tankTempC},
    {key:"surplusW", label:"Surplus", unit:"W", currentValue:e.surplusW},
    {key:"surplusAvailableW", label:"Surplus disponible", unit:"W", currentValue:hasValue(e.surplusW) ? Math.max(Number(e.surplusW), 0) : null},
    {key:"consumptionW", label:"Consommation", unit:"W", currentValue:e.consumptionW}
  ]});
  var h = sourceState.systemHealth || {};
  sources.push({sourceType:"system", id:"system", name:"Systeme", origin:"LOCAL", status:"ok", values:[
    {key:"heapFree", label:"Heap libre", unit:"B", currentValue:h.heapFree || sourceState.heapFree},
    {key:"wifiRssi", label:"RSSI Wi-Fi", unit:"dBm", currentValue:sourceState.rssi},
    {key:"uptimeMs", label:"Uptime", unit:"ms", currentValue:h.uptimeMs || sourceState.uptimeMs}
  ]});
  var actuators = getActuators();
  actuators.forEach(function (actuator) {
    var id = actuator.id || actuator.name || "actuator";
    var pct = getActuatorCommandPercent(actuator);
    sources.push({sourceType:"actuator", id:String(id), name:actuator.name || id, origin:actuatorOrigin(actuator), status:actuator.enabled === false ? "disabled" : (actuator.ok === false ? "degraded" : "ok"), values:[
      {key:"commandPercent", label:"Commande", unit:"%", currentValue:pct},
      {key:"estimatedPowerW", label:"Puissance estimee", unit:"W", currentValue:getActuatorEstimatedPowerW(actuator)}
    ]});
  });
  return sources;
}

function dataSourceKey(sourceType, sourceId, valueKey) {
  return String(sourceType || "sensor") + ":" + String(sourceId || "") + ":" + String(valueKey || "");
}

function seriesSourceKey(series) {
  if (!series || !series.valueKey) return "";
  return dataSourceKey(series.sourceType || (series.sensorId ? "sensor" : "energy"), series.sourceId || series.sensorId || series.id, series.valueKey);
}

function numericSourceValue(series) {
  var value = resolveDataSourceValue(series);
  if (!hasValue(value)) return null;
  var number = Number(value);
  return isFinite(number) ? number : null;
}

function resolveDataSourceValue(series, point) {
  series = series || {};
  var key = dataSourceKey(series.sourceType || (series.sensorId ? "sensor" : "energy"), series.sourceId || series.sensorId || series.id, series.valueKey);
  if (point && point.data && point.data.hasOwnProperty(key)) return point.data[key];
  var sources = getAvailableDataSources(state);
  for (var i = 0; i < sources.length; i++) {
    var source = sources[i];
    var sourceId = series.sourceId || series.sensorId || series.id;
    if (source.sourceType !== (series.sourceType || (series.sensorId ? "sensor" : source.sourceType))) continue;
    if (String(source.id || source.sensorId) !== String(sourceId)) continue;
    for (var j = 0; j < source.values.length; j++) {
      if (source.values[j].key === series.valueKey) return source.values[j].currentValue;
    }
  }
  return null;
}

function selectedTemperatureSource() {
  var configured = dashboardConfig && dashboardConfig.temperatureMainSource;
  if (configured && configured.valueKey) return configured;
  return null;
}

function sensorById(sensorId) {
  if (!sensorId) return null;
  var id = String(sensorId);
  return activeSensorsList().filter(function (sensor) { return String(sensor.id) === id; })[0] || null;
}

function sensorTemperatureValue(sensor) {
  if (!sensor) return null;
  return getSensorValue(sensor, "temperatureC") != null ? getSensorValue(sensor, "temperatureC") : getSensorValue(sensor, "TEMP");
}

function looksLikeDefaultProbeName(sensor) {
  var text = String((sensor && (sensor.name || sensor.id)) || "").toLowerCase();
  return /^sonde\s*\d+$/.test(text) || text === "sonde temperature" || /^sonde\d+$/.test(text);
}

function isLikelyInventedProbeRole(sensor, role) {
  return false;
}

function getSensorDisplayRole(sensor) {
  if (!sensor) return "";
  var value = sensor.usage || sensor.location || sensor.role || "";
  return isLikelyInventedProbeRole(sensor, value) ? "" : value;
}

function helpIcon(text) {
  return '<button class="helpIcon" type="button" tabindex="0" aria-label="' + esc(text) + '">?</button><span class="helpBubble">' + esc(text) + '</span>';
}

function metricLabel(label, help) {
  return '<span class="metricLabel">' + esc(label) + (help ? helpIcon(help) : '') + '</span>';
}

function energyHeroCard(label, value, unit, cls, detail, rawValue, help) {
  return '<section class="cockpitMetric ' + esc(cls || "muted") + '">' + metricLabel(label, help) + '<strong>' + (rawValue ? value : dashboardValue(value, unit)) + '</strong><small>' + esc(detail || "") + '</small></section>';
}

function gridBalanceCard(e, p) {
  var info = gridBalanceInfo(e.gridPowerW);
  var g = gridDerived(e.gridPowerW);
  var filtered = hasValue(state.pidMeasuredW) ? state.pidMeasuredW : (hasValue(state.gridPowerFilteredW) ? state.gridPowerFilteredW : null);
  var source = p.source || state.gridPowerSource || "--";
  var details = [
    "etat : " + (info.flow === "buy" ? "achat" : (info.flow === "inject" ? "injection" : (info.flow === "neutral" ? "proche de zero" : "inconnu"))),
    "achat " + (hasValue(g.buy) ? fmt(g.buy) + " W" : "--"),
    "injection " + (hasValue(g.injection) ? fmt(g.injection) + " W" : "--"),
    "source " + source
  ];
  if (hasValue(filtered)) details.push("filtre " + fmt(filtered) + " W");
  return '<section class="cockpitMetric ' + esc(info.cls || "muted") + '">' +
    metricLabel("Reseau", "Signe technique: positif = achat, negatif = injection. Achat et injection sont separes pour lire le bilan en un coup d'oeil.") +
    '<strong>' + info.value + '</strong><small>' + esc(details.join(" | ")) + '</small></section>';
}

function energyTopBand() {
  var e = stateEnergy();
  var p = pidState();
  var pv = productionInfo(e.productionW);
  var commandPercent = hasValue(p.commandPercent) ? p.commandPercent : p.outputPercent;
  var routerPowerW = hasValue(e.heaterPowerW) ? e.heaterPowerW : p.heaterPowerW;
  var commandDetail = hasValue(commandPercent) ? "commande appliquee" : "commande absente";
  var powerDetail = "state.energy / state.pid";
  return '<section class="energyTopBand">' +
    energyHeroCard("Production solaire", pv.value, "", pv.cls, pv.detail, true) +
    gridBalanceCard(e, p) +
    energyHeroCard("Commande routeur", commandPercent, "%", Number(commandPercent) > 1 ? "heat" : "muted", commandDetail, false, "Commande appliquee par le PID a la sortie routeur, sans supposer SSR1 ou SSR2.") +
    energyHeroCard("Puissance routeur", routerPowerW, "W", Number(routerPowerW) > 5 ? "heat" : "muted", powerDetail, false, "Puissance instantanee estimee ou mesuree de la sortie routeur.") +
  '</section>';
}

function energyFlowSvg() {
  var e = stateEnergy();
  var grid = Number(e.gridPowerW);
  var p = pidState();
  var heaterPower = hasValue(e.heaterPowerW) ? Number(e.heaterPowerW) : (hasValue(p.heaterPowerW) ? Number(p.heaterPowerW) : null);
  var inject = isFinite(grid) && grid < -20;
  var buy = isFinite(grid) && grid > 20;
  var heaterActive = hasValue(heaterPower) && heaterPower > 5;
  var productionActive = hasValue(e.productionW) && Number(e.productionW) > 1;
  var gridCls = !isFinite(grid) ? "unknown" : (inject ? "inject" : (buy ? "buy" : "neutral"));
  var heatCls = heaterActive ? "heat" : "noHeat";
  var solarCls = productionActive ? "pvActive" : "pvIdle";
  var home = hasValue(e.consumptionW) ? e.consumptionW : null;
  var gridLabel = hasValue(e.gridPowerW) ? (buy ? "+" : "") + fmt(e.gridPowerW) + " W " + (buy ? "achat" : (inject ? "injection" : "equilibre")) : "--";
  var pvLabel = hasValue(e.productionW) ? fmt(e.productionW) + " W production" : "-- production";
  var heaterValueText = hasValue(heaterPower) ? fmt(heaterPower) + " W" : "-- W";
  var heaterCommandText = hasValue(p.commandPercent) ? "Cmd " + fmt(p.commandPercent) + " %" : "";
  var heaterLabel = hasValue(heaterPower) ? fmt(heaterPower) + " W sortie" : "-- W sortie";
  return '<section class="energyFlowPanel">' +
    '<div class="wideChartHead"><b>Schema energetique ' + helpIcon("Les fleches actives montrent uniquement les flux reels. A zero watt, le flux reste discret pour indiquer l'equilibre.") + '</b><span>' + esc(inject ? "surplus vers reseau" : (buy ? "appoint reseau" : "equilibre")) + '</span></div>' +
    '<svg class="energyFlowSvg ' + esc(gridCls) + ' ' + esc(heatCls) + ' ' + esc(solarCls) + '" viewBox="0 0 760 270" role="img" aria-label="Flux energie">' +
      '<defs><marker id="flowArrow" markerWidth="7" markerHeight="7" refX="6" refY="3.5" orient="auto"><path d="M0 0 L7 3.5 L0 7 Z"></path></marker><marker id="flowArrowStart" markerWidth="7" markerHeight="7" refX="1" refY="3.5" orient="auto"><path d="M7 0 L0 3.5 L7 7 Z"></path></marker></defs>' +
      '<g class="flowNode sunNode"><circle cx="96" cy="82" r="34"></circle><text x="96" y="76">Soleil</text><text x="96" y="96">Production</text><text x="96" y="112">' + dashboardValue(e.productionW, "W").replace(/<[^>]+>/g, "") + '</text></g>' +
      '<g class="flowNode homeNode"><rect x="315" y="48" width="130" height="82" rx="8"></rect><text x="380" y="82">Maison</text><text x="380" y="104">' + (home != null ? fmt(home) + " W" : "--") + '</text></g>' +
      '<g class="flowNode gridNode"><rect x="590" y="48" width="124" height="82" rx="8"></rect><text x="652" y="82">Reseau</text><text x="652" y="104">' + (hasValue(e.gridPowerW) ? fmt(e.gridPowerW) + " W" : "--") + '</text></g>' +
      '<g class="flowNode heaterNode"><rect x="318" y="172" width="126" height="80" rx="8"></rect><text x="381" y="195">Sortie SSR</text><text x="381" y="214">' + esc(heaterValueText) + '</text><text x="381" y="233">' + esc(heaterCommandText) + '</text></g>' +
      '<path class="flowLine solarLine" d="M130 82 C190 82 245 84 315 88"></path>' +
      '<path class="flowLine gridLine gridBoth" d="M445 88 C500 88 535 88 590 88"></path>' +
      '<path class="flowLine heaterLine" d="M380 130 C380 146 380 158 380 172"></path>' +
      '<text class="flowValue solarValue" x="205" y="68">' + esc(pvLabel) + '</text>' +
      '<text class="flowValue gridValue" x="508" y="70">' + esc(gridLabel) + '</text>' +
      '<text class="flowValue heaterValue" x="396" y="156">' + esc(heaterLabel) + '</text>' +
    '</svg></section>';
}

function pidCockpitCard() {
  var p = pidState();
  var command = hasValue(p.commandPercent) ? Math.max(0, Math.min(100, Number(p.commandPercent))) : (hasValue(p.outputPercent) ? Math.max(0, Math.min(100, Number(p.outputPercent))) : null);
  var commandBar = hasValue(command) ? command : 0;
  var error = hasValue(p.errorW) ? Number(p.errorW) : null;
  var stable = error != null && Math.abs(error) <= Number(p.deadbandW || 30);
  var status = pidStatusDisplay(p, commandBar, stable);
  var mode = routerModeDisplay(p);
  var cls = (mode === "SECURITE" || status === "SECURITY") ? "bad" : (status === "SATURATION" || commandBar >= 99 ? "warn" : (status === "STABLE" ? "ok" : "info"));
  var actuatorMode = isNodeActuatorRole();
  var title = actuatorMode ? "Commande actionneur / PID amont" : "Routeur solaire / PID";
  var source = activeMeasureSourceLabel(p);
  var metrics =
    blockMetric("Mode routeur", mode, "", mode === "SECURITE" ? "bad" : (mode === "OFF" ? "muted" : (mode === "MANUEL" ? "warn" : "ok")), actuatorMode ? "execution locale actionneur" : "pilotage routeur") +
    blockMetric("Source mesure", source || "--", "", "info", p.source || state.gridPowerSource || "--") +
    blockMetric("Consigne reseau", p.targetGridW, "W", "info", "objectif") +
    blockMetric("Deadband", p.deadbandW, "W", "muted", "zone neutre") +
    blockMetric("Erreur PID", p.errorW, "W", hasValue(p.errorW) ? (Number(p.errorW) > 0 ? "consume" : "solar") : "muted", hasValue(p.errorW) ? (Number(p.errorW) > 0 ? "achat" : "injection") : "absente") +
    blockMetric("Sortie PID", p.outputPercent, "%", hasValue(p.outputPercent) && Number(p.outputPercent) > 0 ? "ok" : "muted", "calcul") +
    blockMetric("Commande appliquee", p.commandPercent, "%", hasValue(command) && command > 0 ? "heat" : "muted", "sortie reelle") +
    blockMetric("Puissance routeur", p.heaterPowerW, "W", hasValue(p.heaterPowerW) && Number(p.heaterPowerW) > 0 ? "heat" : "muted", "estimee") +
    blockMetric("Etat PID", status, "", cls, stable ? "dans la deadband" : pidExecutionInfo(p));
  return '<section class="pidCockpit ' + esc(cls) + '">' +
    '<div class="wideChartHead"><b>' + esc(title) + ' ' + helpIcon(actuatorMode ? "Sur un NODE_ACTUATOR, cette carte suit la commande recue et appliquee. Le PID peut etre execute en amont." : "Resume la regulation: mode, source de mesure, erreur, sortie PID, commande appliquee et etat.") + '</b><span class="' + esc(cls) + '">' + esc(status) + '</span></div>' +
    '<div class="pidModeLine"><strong>' + esc(mode) + '</strong><span>source ' + esc(source || "--") + '</span></div>' +
    '<div class="pidExecutionLine"><span>' + esc(actuatorMode ? "Execution commande" : "Execution PID") + '</span><b>' + esc(pidExecutionInfo(p)) + '</b></div>' +
    '<div class="pidCommandBar"><i style="width:' + commandBar + '%"></i><span>' + (hasValue(command) ? esc(fmt(command)) + ' %' : '--') + '</span></div>' +
    '<div class="pidGrid">' + metrics + '</div><p class="pidDiagnostic">' + esc(pidDiagnosticText(p)) + '</p></section>';
}

function normalizeSensorValues(values) {
  var out = {};
  if (Array.isArray(values)) {
    values.forEach(function (v) {
      if (!v) return;
      var key = String(v.key || valueTypeLabel(v.valueType) || "VALUE").toUpperCase();
      out[key] = v.value;
    });
    return out;
  }
  if (values && typeof values === "object") return values;
  return {};
}

function valueTypeLabel(valueType) {
  var map = {1:"POWER",2:"GRID",3:"VOLT",4:"CURR",5:"PAPP",6:"PF",7:"FREQ",8:"TEMP",9:"HUM",10:"ENERGY",11:"BATV",12:"BATA",13:"SOC",14:"STATE",15:"RSSI"};
  return map[valueType] || "VALUE";
}

function normalizeActiveSensor(sensor) {
  sensor = sensor || {};
  var origin = sensor.origin || sensor.source || (sensor.mac ? "ESP-NOW" : "LOCAL");
  origin = String(origin).toUpperCase() === "ESPNOW" ? "ESP-NOW" : String(origin || "LOCAL").toUpperCase();
  var name = sensor.name || sensor.sensorName || sensor.id || ("sensorId " + (sensor.sensorId == null ? "-" : sensor.sensorId));
  var type = sensor.type || sensor.sensorTypeText || sensor.sensorType || "-";
  return {
    id: sensor.id || sensor.sensorId || name,
    name: name,
    type: type,
    origin: origin,
    ok: sensor.ok !== false && sensor.timedOut !== true && sensor.enabled !== false,
    disabled: sensor.enabled === false,
    ageMs: sensor.ageMs,
    timedOut: sensor.timedOut === true,
    neverSeen: sensor.neverSeen === true,
    lastSeenMs: sensor.lastSeenMs,
    lastUpdateMs: sensor.lastUpdateMs,
    role: sensor.role || sensor.sensorRole || "",
    usage: sensor.usage || sensor.sensorUsage || "",
    location: sensor.location || sensor.sensorLocation || "",
    values: normalizeSensorValues(sensor.values)
  };
}

function configSensorsById() {
  var map = {};
  var cfg = cache.sensors || {};
  (cfg.sensors || []).forEach(function (sensor) {
    if (!sensor) return;
    var id = sensor.id || sensor.sensorId || sensor.name;
    if (!id) return;
    map[String(id)] = sensor;
  });
  (cfg.ds18b20 || []).forEach(function (sensor, index) {
    if (!sensor) return;
    var id = sensor.id || ("sonde" + (index + 1));
    map[String(id)] = sensor;
  });
  return map;
}

function mergeRuntimeSensorsWithConfig(runtimeSensors, configMap) {
  configMap = configMap || configSensorsById();
  return (runtimeSensors || []).map(function (runtime) {
    var sensor = normalizeActiveSensor(runtime);
    var cfg = configMap[String(sensor.id)];
    if (!cfg) return sensor;
    sensor.name = cfg.name || cfg.label || sensor.name;
    sensor.type = cfg.type || sensor.type;
    if (String(sensor.origin || sensor.source || "").toUpperCase().indexOf("ESP") >= 0 && sensor.sensorRole) {
      sensor.role = sensor.sensorRole;
    } else {
      sensor.role = cfg.role || sensor.role || "";
    }
    sensor.usage = cfg.usage || sensor.usage || "";
    sensor.location = cfg.location || sensor.location || "";
    sensor.disabled = cfg.enabled === false || sensor.disabled;
    return sensor;
  });
}

function activeSensorsList() {
  var seen = {};
  var items = [];
  function push(sensor) {
    var s = sensor && sensor.__normalized ? sensor : normalizeActiveSensor(sensor);
    var key = String(s.origin + ":" + s.id + ":" + s.name);
    if (seen[key]) return;
    seen[key] = true;
    items.push(s);
  }
  mergeRuntimeSensorsWithConfig(Array.isArray(state.sensors) ? state.sensors : []).forEach(function (s) {
    s.__normalized = true;
    push(s);
  });
  if (!items.length && cache.sensors) {
    (cache.sensors.sensors || []).forEach(function (s) { push(s); });
    (cache.sensors.ds18b20 || []).forEach(function (s, i) {
      var temp = dsTemperature(i);
      push({id:s.id || ("sonde" + (i + 1)), name:s.name || s.id || ("Sonde " + (i + 1)), type:"DS18B20", origin:"LOCAL", ok:dsAvailable(i, temp), ageMs:dsLastReadMs(i), values:{TEMP:temp}});
    });
  }
  return items.sort(function (a, b) {
    var statusA = sensorStatusRank(a);
    var statusB = sensorStatusRank(b);
    if (statusA !== statusB) return statusA - statusB;
    var rankA = sensorTypeRank(a);
    var rankB = sensorTypeRank(b);
    if (rankA !== rankB) return rankA - rankB;
    return String(a.name).localeCompare(String(b.name));
  });
}

function sensorTypeRank(sensor) {
  var t = String((sensor && sensor.type) || "").toUpperCase() + " " + String((sensor && sensor.role) || "").toUpperCase() + " " + Object.keys((sensor && sensor.values) || {}).join(" ");
  if (t.indexOf("JSY") >= 0 || t.indexOf("LINKY") >= 0 || t.indexOf("TIC") >= 0 || t.indexOf("GRID") >= 0 || t.indexOf("PAPP") >= 0) return 0;
  if (t.indexOf("DS18") >= 0 || t.indexOf("TEMP") >= 0) return 1;
  return 2;
}

function sensorStaleLimitMs(sensor) {
  var text = String((sensor && sensor.type) || "").toUpperCase();
  if (text.indexOf("JSY") >= 0) return 2500;
  if (text.indexOf("TIC") >= 0 || text.indexOf("LINKY") >= 0) return 10000;
  if (text.indexOf("DS18") >= 0 || text.indexOf("TEMP") >= 0) return 30000;
  return 15000;
}

function sensorStatusInfo(sensor) {
  if (!sensor) return {key:"unknown", rank:4, cls:"muted", label:"Inconnu", detail:"etat indisponible", stale:false};
  if (sensor.disabled) return {key:"disabled", rank:4, cls:"muted", label:"Desactive", detail:"capteur desactive", stale:true};
  if (sensor.neverSeen) return {key:"neverSeen", rank:3, cls:"bad", label:"Jamais recu", detail:"aucune mesure recue", stale:true};
  if (sensor.timedOut) return {key:"lost", rank:2, cls:"bad", label:"Perdu", detail:"timeout depuis " + ageLabel(sensor.ageMs), stale:true};
  if (!sensor.ok) return {key:"lost", rank:2, cls:"bad", label:"Perdu", detail:"mesure invalide", stale:true};
  if (hasValue(sensor.ageMs) && Number(sensor.ageMs) > sensorStaleLimitMs(sensor)) {
    return {key:"stale", rank:1, cls:"warn", label:"Ancien", detail:"age " + ageLabel(sensor.ageMs), stale:true};
  }
  return {key:"ok", rank:0, cls:"ok", label:"OK", detail:"mesure recente", stale:false};
}

function sensorStatusRank(sensor) {
  return sensorStatusInfo(sensor).rank;
}

function sensorValuesLine(values, stale) {
  var order = ["GRID","POWER","VOLT","CURR","PAPP","IINST","TEMP","PF","FREQ","HUM","SOC","BATV"];
  var units = {GRID:"W",POWER:"W",VOLT:"V",CURR:"A",PAPP:"VA",IINST:"A",TEMP:"C",PF:"",FREQ:"Hz",HUM:"%",SOC:"%",BATV:"V"};
  var keys = order.filter(function (key) { return hasValue(values[key]); });
  Object.keys(values || {}).forEach(function (key) {
    if (key === "VALUE" && keys.length) return;
    if (keys.indexOf(key) < 0 && isPlausibleSensorValueKey(key, values[key])) keys.push(key);
  });
  if (!keys.length) return '<span class="muted">--</span>';
  return keys.slice(0, 6).map(function (key) {
    return '<span class="' + (stale ? 'staleValue' : '') + '"><b>' + esc(key) + '</b> ' + esc(fmt(values[key])) + (units[key] ? ' ' + esc(units[key]) : '') + '</span>';
  }).join("");
}

function activeSensorsPanel() {
  var sensors = activeSensorsList();
  var okCount = sensors.filter(function (s) { return sensorStatusInfo(s).key === "ok"; }).length;
  var cards = sensors.map(function (s) {
    var live = sensorStatusInfo(s);
    var cls = live.cls;
    var status = live.label + " - " + live.detail;
    var valueCaption = live.key === "ok" ? "mesure actuelle" : (live.key === "neverSeen" ? "aucune mesure recue" : "derniere mesure connue");
    var usage = getSensorDisplayRole(s) || "usage non configure";
    return '<article class="activeSensorCard ' + esc(cls) + '"><div><strong>' + esc(s.name) + '</strong><span class="badge ' + esc(cls) + '">' + esc(live.label) + '</span><span class="badge muted">' + esc(s.origin) + '</span></div><small>' + esc(s.type) + ' / ' + esc(usage) + '</small><div class="sensorStatusLine"><span>' + esc(status) + '</span><span>source : ' + esc(s.origin) + '</span></div><p>' + sensorValuesLine(s.values, live.stale) + '</p><small>' + esc(valueCaption) + '</small></article>';
  }).join("");
  return '<section class="activeSensorsPanel"><div class="wideChartHead"><b>Capteurs actifs ' + helpIcon("Liste les capteurs locaux et ESP-NOW connus par SensorManager. Les roles ne sont affiches que s'ils sont configures.") + '</b><span class="sensorOkCounter">' + okCount + '/' + sensors.length + ' OK</span></div><div class="activeSensorGrid">' + (cards || '<div class="mutedBox">Aucun capteur actif visible.</div>') + '</div></section>';
}

function systemHealthPanel() {
  var h = state.systemHealth || {};
  var sensors = activeSensorsList();
  var okCount = sensors.filter(function (s) { return sensorStatusInfo(s).key === "ok"; }).length;
  var staleCount = sensors.filter(function (s) { return sensorStatusInfo(s).key === "stale"; }).length;
  var lostSensors = sensors.filter(function (s) {
    var key = sensorStatusInfo(s).key;
    return key === "lost" || key === "neverSeen";
  });
  var lostCount = lostSensors.length;
  var firstLost = lostSensors[0];
  return '<section class="systemHealthPanel"><div class="wideChartHead"><b>Sante systeme ' + helpIcon("Resume Wi-Fi, ESP-NOW, securite, uptime, memoire et etat capteurs.") + '</b><span>diagnostic</span></div><div class="healthGrid">' +
    statusMini("Wi-Fi", h.wifi || (state.wifiConnected ? "OK" : state.networkMode)) +
    statusMini("ESP-NOW", h.espnow || (state.espNowReady ? "OK" : "N/A")) +
    statusMini("MQTT", h.mqtt || (state.mqttEnabled ? state.mqttStatus : "N/A")) +
    statusMini("Securite", h.safety || state.safetyLevel || "OK") +
    blockMetric("Role", roleInfo().label, "", "info", roleInfo().detail) +
    blockMetric("Simulation", state.simulationMode ? "active" : "OFF", "", state.simulationMode ? "warn" : "muted", state.simulationMode ? simRemainingText() : "mesures reelles") +
    blockMetric("Uptime", formatDuration(h.uptimeMs || state.uptimeMs), "", "muted", "depuis boot") +
    blockMetric("Heap", h.heapFree || state.heapFree, "B", "info", "memoire libre") +
    blockMetric("Capteurs OK", okCount, "", okCount ? "ok" : "warn", okCount + "/" + sensors.length + " actifs") +
    blockMetric("Capteurs anciens", staleCount, "", staleCount ? "warn" : "ok", staleCount ? "a surveiller" : "aucun") +
    blockMetric("Capteurs perdus", lostCount, "", lostCount ? "warn" : "ok", firstLost ? firstLost.name : "aucun timeout") +
    blockMetric("RSSI Wi-Fi", state.rssi, "dBm", hasValue(state.rssi) ? "info" : "muted", "signal") +
    blockMetric("Dernier defaut", h.lastFault || state.safetyReason || "aucun", "", state.safetyTripped ? "bad" : "muted", "") +
  '</div></section>';
}

function formatDuration(ms) {
  var s = Math.floor(Number(ms || 0) / 1000);
  if (!s) return "--";
  var h = Math.floor(s / 3600);
  var m = Math.floor((s % 3600) / 60);
  if (h) return h + " h " + m + " min";
  return m + " min";
}

function routerMoodPanel(extraClass) {
  var e = stateEnergy();
  var p = pidState();
  var grid = Number(e.gridPowerW);
  var output = Number(p.commandPercent || p.outputPercent || 0);
  var sensors = activeSensorsList();
  var sensorOk = sensors.some(function (s) { return s.ok; });
  var sensorLost = sensors.some(function (s) { return !s.ok && !s.disabled; });
  var text = "Equilibre / attente";
  var cls = "muted";
  var icon = "😴";
  if (state.safetyTripped || state.safetyLevel === "CRITICAL") { icon = "🛑"; text = "Securite active"; cls = "bad"; }
  else if (state.simulationMode) { icon = "🧪"; text = "Simulation"; cls = "warn"; }
  else if (!sensorOk || sensorLost) { icon = "⚠️"; text = "Capteurs degrades"; cls = "warn"; }
  else if (String(p.status || "").toUpperCase().indexOf("OSC") >= 0) { icon = "⚠️"; text = "Oscillation PID"; cls = "warn"; }
  else if (grid > 50) { icon = "⚡"; text = "Achat reseau"; cls = "consume"; }
  else if (output > 1) { icon = "🔥"; text = "Routeur actif"; cls = "heat"; }
  else if (grid < -50 || Number(e.surplusW) > 50) { icon = "😎"; text = "Surplus disponible"; cls = "solar"; }
  return '<section class="routerMood ' + esc(cls) + (extraClass ? ' ' + esc(extraClass) : '') + '"><span>' + icon + '</span><div><b>Humeur du routeur ' + helpIcon("Synthese rapide calculee avec securite, capteurs, reseau et commande routeur.") + '</b><strong>' + esc(text) + '</strong><small>basee sur reseau, PID, securite et capteurs</small></div></section>';
}

function canvasChartCard(id, title, subtitle) {
  return '<section class="canvasChartCard"><div class="wideChartHead"><b>' + esc(title) + '</b><span>' + esc(subtitle || "") + '</span></div><canvas id="' + esc(id) + '" height="180"></canvas></section>';
}

function configuredCanvasChartCard(graph, index) {
  var effective = effectiveDashboardGraph(graph);
  var status = effective.enabled === false ? "desactive" : ((effective.series || []).length + " serie(s)");
  return '<section class="canvasChartCard" data-graph-index="' + index + '"><div class="wideChartHead"><b>' + esc(effective.title || "Graphe") + '</b><span>' + esc(status) + '</span><button class="miniButton" onclick="openDashboardGraphConfig(' + index + ')">Configurer</button></div><canvas id="dashGraph_' + index + '" height="180"></canvas></section>';
}

function dashboardVisibleConfiguredGraphs() {
  return (dashboardConfig.graphs || []).map(function (graph, index) {
    return {graph:graph, index:index, effective:effectiveDashboardGraph(graph)};
  }).filter(function (item) {
    return item.effective.id !== "graph_pid" && !isRemovedDashboardGraphId(item.effective.id);
  });
}

function dedupeDashboardSeries(series) {
  var seen = {};
  return (series || []).filter(function (item) {
    var key = seriesSourceKey(item);
    if (!key || seen[key]) return false;
    seen[key] = true;
    return true;
  });
}

function effectiveDashboardGraph(graph) {
  graph = graph || {};
  if (graph.id === "graph_pid" && isNodeActuatorRole()) {
    return {
      id:graph.id,
      title:"Commande actionneur",
      type:"area",
      enabled:graph.enabled !== false,
      series:[
        {sourceType:"pid", sourceId:"pid", valueKey:"outputPercent", label:"Commande recue", unit:"%", color:"#38bdf8"},
        {sourceType:"pid", sourceId:"pid", valueKey:"commandPercent", label:"Commande appliquee", unit:"%", color:"#f97316"},
        {sourceType:"energy", sourceId:"energy", valueKey:"heaterPowerPositiveW", label:"Puissance estimee routeur", unit:"W", color:"#f59e0b"}
      ]
    };
  }
  if (graph.id === "graph_pid") {
    graph.title = graph.title === "PID" ? "PID / Regulation" : graph.title;
  }
  return {
    id:graph.id,
    title:graph.title,
    type:graph.type,
    enabled:graph.enabled,
    series:dedupeDashboardSeries(graph.series)
  };
}

function temperatureGraphSeries() {
  var colors = ["#38bdf8", "#22c55e", "#f97316", "#a78bfa", "#f472b6", "#f59e0b"];
  return getTemperatureSensors().filter(function (sensor) {
    return hasValue(sensorTemperatureValue(sensor));
  }).map(function (sensor, index) {
    var values = canonicalSensorValues(sensor);
    var key = hasValue(values.temperatureC) ? "temperatureC" : "TEMP";
    var labelBits = [];
    labelBits.push(sensor.name);
    if (getSensorDisplayRole(sensor)) labelBits.push(getSensorDisplayRole(sensor));
    else labelBits.push("usage non configure");
    return {
      sourceType:"sensor",
      sourceId:String(sensor.id),
      sensorId:String(sensor.id),
      valueKey:key,
      label:labelBits.join(" / "),
      unit:"C",
      color:colors[index % colors.length]
    };
  });
}

function modernDashboardGraphs() {
  var graphs = dashboardVisibleConfiguredGraphs().map(function (item) {
    return configuredCanvasChartCard(item.graph, item.index);
  }).join("");
  return '<section class="cockpitCharts">' +
    canvasChartCard("pidErrorDedicated", "Erreur PID", "W - zero et deadband") +
    canvasChartCard("pidOutputDedicated", "Sortie PID", "PID / commande appliquee") +
    graphs +
    '</section><section id="dashboardGraphEditor"></section>';
}

function renderDashboardCanvases() {
  if (page !== "dashboard") return;
  drawPidErrorChart("pidErrorDedicated");
  drawPidOutputChart("pidOutputDedicated");
  dashboardVisibleConfiguredGraphs().forEach(function (item) {
    drawConfiguredGraph("dashGraph_" + item.index, item.effective);
  });
}

function canvasHistory() {
  return dashHistory.slice(-180);
}

function drawLineChart(canvasId, series, options) {
  var canvas = $(canvasId);
  if (!canvas) return;
  var rect = canvas.getBoundingClientRect();
  var dpr = window.devicePixelRatio || 1;
  var width = Math.max(280, Math.floor(rect.width || canvas.parentNode.clientWidth || 320));
  var height = Math.max(160, Number(canvas.getAttribute("height")) || 180);
  canvas.width = Math.floor(width * dpr);
  canvas.height = Math.floor(height * dpr);
  var ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#101115";
  ctx.fillRect(0, 0, width, height);
  var history = canvasHistory();
  if (!series || !series.length) {
    ctx.fillStyle = "#9ca3af";
    ctx.fillText("aucune serie configuree", 14, 24);
    return;
  }
  if (history.length < 2) {
    ctx.fillStyle = "#9ca3af";
    ctx.fillText("historique en cours...", 14, 24);
    return;
  }
  var pad = {l:44, r:12, t:12, b:26};
  var plotW = width - pad.l - pad.r;
  var plotH = height - pad.t - pad.b;
  var values = [];
  series.forEach(function (s) { history.forEach(function (p) { var v = graphValue(p, s.key); if (v != null && isFinite(v)) values.push(v); }); });
  var min = options && options.min != null ? options.min : Math.min.apply(null, values);
  var max = options && options.max != null ? options.max : Math.max.apply(null, values);
  if (!values.length || !isFinite(min) || !isFinite(max)) {
    ctx.fillStyle = "#9ca3af";
    ctx.fillText("pas de valeur exploitable", 14, 24);
    return;
  }
  if (options && options.includeZero !== false) { min = Math.min(min, 0); max = Math.max(max, 0); }
  if (max === min) max = min + 1;
  var span = max - min;
  min -= span * 0.12;
  max += span * 0.12;
  function xAt(i) { return pad.l + (history.length <= 1 ? 0 : i * plotW / (history.length - 1)); }
  function yAt(v) { return pad.t + plotH - ((v - min) * plotH / (max - min)); }
  ctx.strokeStyle = "#2a2b30";
  ctx.lineWidth = 1;
  for (var g = 0; g <= 4; g++) {
    var y = pad.t + g * plotH / 4;
    ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(width - pad.r, y); ctx.stroke();
  }
  if (options && options.deadband != null) {
    var d = Math.abs(Number(options.deadband) || 0);
    ctx.fillStyle = "rgba(148,163,184,.12)";
    ctx.fillRect(pad.l, yAt(d), plotW, Math.max(1, yAt(-d) - yAt(d)));
    ctx.strokeStyle = "rgba(148,163,184,.35)";
    ctx.setLineDash([4, 5]);
    ctx.beginPath(); ctx.moveTo(pad.l, yAt(d)); ctx.lineTo(width - pad.r, yAt(d)); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(pad.l, yAt(-d)); ctx.lineTo(width - pad.r, yAt(-d)); ctx.stroke();
    ctx.setLineDash([]);
  }
  if (min < 0 && max > 0) {
    var zy = yAt(0);
    ctx.strokeStyle = "#e5e7eb";
    ctx.lineWidth = options && options.pidError ? 2 : 1;
    ctx.beginPath(); ctx.moveTo(pad.l, zy); ctx.lineTo(width - pad.r, zy); ctx.stroke();
  }
  series.forEach(function (s) {
    var coords = [];
    history.forEach(function (p, i) {
      var v = graphValue(p, s.key);
      if (v == null || !isFinite(v)) return;
      coords.push({x:xAt(i), y:yAt(v)});
    });
    if (options && options.area && coords.length > 1) {
      var baseY = yAt(0);
      ctx.beginPath();
      ctx.moveTo(coords[0].x, baseY);
      coords.forEach(function (pt) { ctx.lineTo(pt.x, pt.y); });
      ctx.lineTo(coords[coords.length - 1].x, baseY);
      ctx.closePath();
      var grad = ctx.createLinearGradient(0, pad.t, 0, pad.t + plotH);
      grad.addColorStop(0, rgbaFromHex(s.color || "#60a5fa", 0.24));
      grad.addColorStop(1, rgbaFromHex(s.color || "#60a5fa", 0.02));
      ctx.fillStyle = grad;
      ctx.fill();
    }
    ctx.strokeStyle = s.color || "#60a5fa";
    ctx.lineWidth = 2;
    ctx.beginPath();
    var started = false;
    coords.forEach(function (pt) {
      if (!started) { ctx.moveTo(pt.x, pt.y); started = true; }
      else ctx.lineTo(pt.x, pt.y);
    });
    ctx.stroke();
  });
  ctx.fillStyle = "#9ca3af";
  ctx.font = "11px system-ui, sans-serif";
  ctx.fillText(fmt(max), 6, pad.t + 10);
  ctx.fillText(fmt(min), 6, height - pad.b);
  var legendX = pad.l + 74;
  series.forEach(function (s) {
    ctx.fillStyle = s.color || "#60a5fa";
    ctx.fillText(s.label || s.key, legendX, pad.t + 14);
    legendX += ctx.measureText(s.label || s.key).width + 16;
  });
  if (options && options.pidError) {
    ctx.fillStyle = "#f87171";
    ctx.fillText("achat +", pad.l + 6, pad.t + 14);
    ctx.fillStyle = "#22c55e";
    ctx.fillText("injection -", pad.l + 6, height - pad.b - 6);
  }
}

function drawAreaChart(canvasId, series, unit) {
  drawLineChart(canvasId, series, {includeZero:true, unit:unit, area:true});
}

function drawConfiguredGraph(canvasId, graph) {
  if (!graph || graph.enabled === false) {
    var canvas = $(canvasId);
    if (!canvas) return;
    var ctx = canvas.getContext("2d");
    var width = canvas.parentNode ? canvas.parentNode.clientWidth || 320 : 320;
    canvas.width = width;
    canvas.height = Number(canvas.getAttribute("height")) || 180;
    ctx.fillStyle = "#101115";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = "#9ca3af";
    ctx.fillText("graphe desactive", 14, 24);
    return;
  }
  var series = (graph.series || []).map(function (s) {
    return {
      key:dataSourceKey(s.sourceType || (s.sensorId ? "sensor" : "energy"), s.sourceId || s.sensorId || s.id, s.valueKey),
      label:s.label || s.valueKey,
      color:s.color || "#60a5fa",
      sourceSeries:s
    };
  });
  drawSourceChart(canvasId, series, {area:graph.type === "area", includeZero:graph.type !== "pid", deadband:graph.type === "pid" ? pidState().deadbandW : null});
}

function drawSourceChart(canvasId, series, options) {
  var canvas = $(canvasId);
  if (!canvas) return;
  var rect = canvas.getBoundingClientRect();
  var dpr = window.devicePixelRatio || 1;
  var width = Math.max(280, Math.floor(rect.width || canvas.parentNode.clientWidth || 320));
  var height = Math.max(160, Number(canvas.getAttribute("height")) || 180);
  canvas.width = Math.floor(width * dpr);
  canvas.height = Math.floor(height * dpr);
  var ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#101115";
  ctx.fillRect(0, 0, width, height);
  var history = dashHistory.slice(-180);
  if (history.length < 2 || !series.length) {
    ctx.fillStyle = "#9ca3af";
    ctx.fillText(series.length ? "historique en cours..." : "aucune serie configuree", 14, 24);
    return;
  }
  var values = [];
  series.forEach(function (s) { history.forEach(function (p) { var v = p.data ? p.data[s.key] : null; if (isPlausibleChartValue(s.sourceSeries || {}, v)) values.push(v); }); });
  if (values.length < 2) {
    ctx.fillStyle = "#9ca3af";
    ctx.fillText("pas de valeur exploitable", 14, 24);
    return;
  }
  drawLineChartWithValues(ctx, width, height, history, series, values, options);
}

function robustChartValues(values) {
  var sorted = (values || []).filter(function (v) { return v != null && isFinite(v); }).sort(function (a, b) { return a - b; });
  if (sorted.length < 8) return sorted;
  var low = sorted[Math.floor(sorted.length * 0.05)];
  var high = sorted[Math.ceil(sorted.length * 0.95) - 1];
  if (!isFinite(low) || !isFinite(high) || low === high) return sorted;
  return sorted.filter(function (v) { return v >= low && v <= high; });
}

function isPlausibleChartValue(series, value) {
  if (value == null || !isFinite(value)) return false;
  var key = String((series && series.valueKey) || "").toUpperCase();
  var n = Number(value);
  if (key.indexOf("TEMP") >= 0) return n >= -30 && n <= 125;
  if (key.indexOf("PERCENT") >= 0 || key.indexOf("PCT") >= 0 || key === "COMMANDPERCENT" || key === "OUTPUTPERCENT") return n >= 0 && n <= 100;
  if (key.indexOf("VOLT") >= 0 || key === "VOLT") return n >= 0 && n <= 260;
  if (key.indexOf("FREQ") >= 0 || key === "FREQ") return n >= 45 && n <= 55;
  if (isPowerLikeValueKey(key)) return Math.abs(n) <= 100000;
  return true;
}

function sanitizeHistoryValue(source, value) {
  if (!isPlausibleChartValue({valueKey:source && (source.valueKey || source.key)}, value)) return null;
  return finiteOrNull(value);
}

function drawLineChartWithValues(ctx, width, height, history, series, values, options) {
  var pad = {l:44, r:12, t:12, b:26};
  var plotW = width - pad.l - pad.r;
  var plotH = height - pad.t - pad.b;
  var scaleValues = robustChartValues(values);
  var min = Math.min.apply(null, scaleValues);
  var max = Math.max.apply(null, scaleValues);
  if (options && options.includeZero !== false) { min = Math.min(min, 0); max = Math.max(max, 0); }
  if (max === min) max = min + 1;
  var span = max - min;
  min -= span * 0.12;
  max += span * 0.12;
  function xAt(i) { return pad.l + (history.length <= 1 ? 0 : i * plotW / (history.length - 1)); }
  function yAt(v) { return pad.t + plotH - ((v - min) * plotH / (max - min)); }
  ctx.strokeStyle = "#2a2b30";
  ctx.lineWidth = 1;
  for (var g = 0; g <= 4; g++) {
    var y = pad.t + g * plotH / 4;
    ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(width - pad.r, y); ctx.stroke();
  }
  if (min < 0 && max > 0) {
    var zy = yAt(0);
    ctx.strokeStyle = "#e5e7eb";
    ctx.beginPath(); ctx.moveTo(pad.l, zy); ctx.lineTo(width - pad.r, zy); ctx.stroke();
  }
  if (options && options.deadband != null) {
    var d = Math.abs(Number(options.deadband) || 0);
    ctx.fillStyle = "rgba(148,163,184,.12)";
    ctx.fillRect(pad.l, yAt(d), plotW, Math.max(1, yAt(-d) - yAt(d)));
  }
  series.forEach(function (s) {
    var coords = [];
    history.forEach(function (p, i) {
      var v = p.data ? p.data[s.key] : null;
      if (!isPlausibleChartValue(s.sourceSeries || {}, v)) return;
      coords.push({x:xAt(i), y:yAt(v)});
    });
    if (options && options.area && coords.length > 1) {
      var baseY = yAt(0);
      ctx.beginPath();
      ctx.moveTo(coords[0].x, baseY);
      coords.forEach(function (pt) { ctx.lineTo(pt.x, pt.y); });
      ctx.lineTo(coords[coords.length - 1].x, baseY);
      ctx.closePath();
      var grad = ctx.createLinearGradient(0, pad.t, 0, pad.t + plotH);
      grad.addColorStop(0, rgbaFromHex(s.color || "#60a5fa", 0.20));
      grad.addColorStop(1, rgbaFromHex(s.color || "#60a5fa", 0.02));
      ctx.fillStyle = grad;
      ctx.fill();
    }
    ctx.strokeStyle = s.color || "#60a5fa";
    ctx.lineWidth = 2;
    ctx.beginPath();
    coords.forEach(function (pt, i) {
      if (i === 0) ctx.moveTo(pt.x, pt.y);
      else ctx.lineTo(pt.x, pt.y);
    });
    ctx.stroke();
  });
  ctx.fillStyle = "#9ca3af";
  ctx.font = "11px system-ui, sans-serif";
  ctx.fillText(fmt(max), 6, pad.t + 10);
  ctx.fillText(fmt(min), 6, height - pad.b);
}

function drawPidErrorChart(canvasId) {
  drawLineChart(canvasId, [{key:"pidErrorW", label:"Erreur PID", color:"#f59e0b"}], {includeZero:true, deadband:pidState().deadbandW, pidError:true});
}

function drawPidOutputChart(canvasId) {
  drawLineChart(canvasId, [
    {key:"pidOutputPercent", label:"PID", color:"#38bdf8"},
    {key:"commandPercent", label:"Commande", color:"#f97316"}
  ], {min:0, max:100, includeZero:false, pidOutput:true});
}

function graphSourceOptions(selectedKey) {
  return getAvailableDataSources(state).map(function (source) {
    return (source.values || []).map(function (value) {
      var key = dataSourceKey(source.sourceType, source.id || source.sensorId, value.key);
      var label = source.name + " / " + value.label + " / " + fmt(value.currentValue) + (value.unit ? " " + value.unit : "") + " / " + (source.origin || source.sourceType);
      return '<option value="' + esc(key) + '" ' + (key === selectedKey ? "selected" : "") + '>' + esc(label) + '</option>';
    }).join("");
  }).join("");
}

function sourceSeriesFromKey(key) {
  var parts = String(key || "").split(":");
  var sourceType = parts[0] || "sensor";
  var sourceId = parts[1] || "";
  var valueKey = parts.slice(2).join(":");
  var sources = getAvailableDataSources(state);
  for (var i = 0; i < sources.length; i++) {
    var source = sources[i];
    if (source.sourceType !== sourceType || String(source.id || source.sensorId) !== sourceId) continue;
    for (var j = 0; j < source.values.length; j++) {
      var value = source.values[j];
      if (value.key !== valueKey) continue;
      return {sourceType:sourceType, sourceId:sourceId, sensorId:sourceType === "sensor" ? sourceId : undefined, valueKey:valueKey, label:source.name + " " + value.label, unit:value.unit || unitForValueKey(valueKey), color:colorForSeriesIndex(j)};
    }
  }
  return null;
}

function colorForSeriesIndex(index) {
  return ["#38bdf8", "#22c55e", "#f97316", "#a78bfa", "#ef4444", "#f59e0b"][index % 6];
}

function openDashboardGraphConfig(index) {
  var graph = dashboardConfig.graphs[index];
  if (!graph) return;
  var rows = (graph.series || []).map(function (series, seriesIndex) {
    var key = dataSourceKey(series.sourceType || (series.sensorId ? "sensor" : "energy"), series.sourceId || series.sensorId || series.id, series.valueKey);
    return '<div class="graphSeriesRow">' +
      '<select data-series-source="' + seriesIndex + '">' + graphSourceOptions(key) + '</select>' +
      '<input data-series-label="' + seriesIndex + '" value="' + esc(series.label || "") + '" placeholder="Libelle">' +
      '<input data-series-unit="' + seriesIndex + '" value="' + esc(series.unit || "") + '" placeholder="Unite">' +
      '<input data-series-color="' + seriesIndex + '" value="' + esc(series.color || colorForSeriesIndex(seriesIndex)) + '" placeholder="#38bdf8">' +
      '<button class="danger" onclick="removeDashboardGraphSeries(' + index + ',' + seriesIndex + ')">Supprimer</button>' +
    '</div>';
  }).join("");
  $("dashboardGraphEditor").innerHTML = '<section class="panel graphEditor"><div class="wideChartHead"><b>Configurer le graphe</b><button onclick="closeDashboardGraphConfig()">Fermer</button></div>' +
    '<div class="formGrid">' +
      '<label>Actif<select id="dashGraphEnabled"><option value="true" ' + (graph.enabled !== false ? "selected" : "") + '>oui</option><option value="false" ' + (graph.enabled === false ? "selected" : "") + '>non</option></select></label>' +
      '<label>Titre<input id="dashGraphTitle" value="' + esc(graph.title || "") + '"></label>' +
      '<label>Type<select id="dashGraphType">' + options(["line","area","pid","bar"], graph.type || "line") + '</select></label>' +
    '</div>' +
    '<div class="graphSeriesList">' + (rows || '<p class="muted">Aucune serie configuree.</p>') + '</div>' +
    '<div class="toolbar"><button onclick="addDashboardGraphSeries(' + index + ')">Ajouter une serie</button><button onclick="saveDashboardGraphEditor(' + index + ')">Sauvegarder</button></div>' +
    '<p class="muted">Les sources viennent de SensorManager/state global : capteurs locaux, ESP-NOW, PID, energie, actionneurs et systeme.</p>' +
  '</section>';
}

function closeDashboardGraphConfig() {
  var box = $("dashboardGraphEditor");
  if (box) box.innerHTML = "";
  clearDashboardEditingState();
}

function addDashboardGraphSeries(index) {
  var graph = dashboardConfig.graphs[index];
  if (!graph) return;
  var firstSource = getAvailableDataSources(state)[0];
  if (!firstSource || !firstSource.values || !firstSource.values.length) return alert("Aucune source disponible");
  var value = firstSource.values[0];
  graph.series = graph.series || [];
  graph.series.push({sourceType:firstSource.sourceType, sourceId:firstSource.id || firstSource.sensorId, sensorId:firstSource.sourceType === "sensor" ? (firstSource.sensorId || firstSource.id) : undefined, valueKey:value.key, label:firstSource.name + " " + value.label, unit:value.unit || unitForValueKey(value.key), color:colorForSeriesIndex(graph.series.length)});
  openDashboardGraphConfig(index);
  renderDashboardCanvases();
}

function removeDashboardGraphSeries(graphIndex, seriesIndex) {
  var graph = dashboardConfig.graphs[graphIndex];
  if (!graph || !graph.series) return;
  graph.series.splice(seriesIndex, 1);
  openDashboardGraphConfig(graphIndex);
  renderDashboardCanvases();
}

async function saveDashboardGraphEditor(index) {
  var graph = dashboardConfig.graphs[index];
  if (!graph) return;
  graph.enabled = $("dashGraphEnabled").value === "true";
  graph.title = $("dashGraphTitle").value || graph.title || "Graphe";
  graph.type = $("dashGraphType").value || "line";
  graph.series = (graph.series || []).map(function (series, seriesIndex) {
    var selected = sourceSeriesFromKey(document.querySelector('[data-series-source="' + seriesIndex + '"]').value) || series;
    selected.label = document.querySelector('[data-series-label="' + seriesIndex + '"]').value || selected.label;
    selected.unit = document.querySelector('[data-series-unit="' + seriesIndex + '"]').value || selected.unit || unitForValueKey(selected.valueKey);
    selected.color = document.querySelector('[data-series-color="' + seriesIndex + '"]').value || selected.color || colorForSeriesIndex(seriesIndex);
    return selected;
  });
  try {
    await saveDashboardConfig();
    clearDashboardEditingState();
    closeDashboardGraphConfig();
    render();
  } catch (e) {
    alert("Sauvegarde dashboard refusee: " + e.message);
  }
}

function temperatureSourceOptions() {
  var selected = selectedTemperatureSource();
  var selectedKey = selected ? dataSourceKey(selected.sourceType || "sensor", selected.sourceId || selected.sensorId || selected.id, selected.valueKey) : "";
  return '<option value="">Source non configuree</option>' + getAvailableDataSources(state).map(function (source) {
    return (source.values || []).filter(function (value) {
      return String(value.key).toUpperCase().indexOf("TEMP") >= 0 || value.key === "temperatureC";
    }).map(function (value) {
      var key = dataSourceKey(source.sourceType, source.id || source.sensorId, value.key);
      var label = source.name + " / " + value.label + " / " + fmt(value.currentValue) + (value.unit ? " " + value.unit : "") + " / " + (source.origin || source.sourceType);
      return '<option value="' + esc(key) + '" ' + (key === selectedKey ? "selected" : "") + '>' + esc(label) + '</option>';
    }).join("");
  }).join("");
}

async function saveTemperatureMainSource() {
  var value = $("temperatureMainSource").value;
  dashboardConfig.temperatureMainSource = value ? sourceSeriesFromKey(value) : null;
  try {
    await saveDashboardConfig();
    render();
  } catch (e) {
    alert("Sauvegarde source temperature refusee: " + e.message);
  }
}

function isTemperatureSensor(sensor) {
  if (!sensor) return false;
  return String(sensor.type || "").toUpperCase().indexOf("DS18") >= 0 ||
    hasValue(getSensorValue(sensor, "TEMP")) ||
    hasValue(getSensorValue(sensor, "temperatureC"));
}

function temperatureSensorOptions(selectedId) {
  var sensors = getTemperatureSensors();
  return '<option value="">Non configuree</option>' + sensors.map(function (sensor) {
    var value = sensorTemperatureValue(sensor);
    var status = sensor.ok ? "OK" : "perdu";
    var label = sensor.name + " - " + sensor.type + " - " + sensor.origin + " - " + (hasValue(value) ? fmt(value) + " C" : "--") + " - " + status;
    return '<option value="' + esc(sensor.id) + '" ' + (String(selectedId || "") === String(sensor.id) ? "selected" : "") + '>' + esc(label) + '</option>';
  }).join("");
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

function networkBalanceTooltip(point, keys) {
  var signedPower = Number(graphValue(point, "gridPowerW"));
  var valid = isFinite(signedPower);
  var cls = !valid ? "muted" : (signedPower < -20 ? "gridInject" : (signedPower > 20 ? "gridConsume" : "ok"));
  var achatW = valid ? Math.max(0, signedPower) : null;
  var injectionW = valid ? Math.max(0, -signedPower) : null;
  var html = tooltipLine("Puissance reseau", valid ? signedPower : null, "W", cls);
  html += tooltipLine("Achat reseau", achatW, "W", achatW > 0 ? "gridConsume" : "muted");
  html += tooltipLine("Injection reseau", injectionW, "W", injectionW > 0 ? "gridInject" : "muted");
  if (keys.indexOf("gridPowerFilteredW") >= 0) html += tooltipLine("Mesure filtree", graphValue(point, "gridPowerFilteredW"), "W", "muted");
  if (keys.indexOf("targetW") >= 0) html += tooltipLine("Consigne", graphValue(point, "targetW"), "W", "info");
  return html;
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
    html += keys.indexOf("gridPowerW") >= 0 && unit === "W"
      ? networkBalanceTooltip(point, keys)
      : keys.map(function (key, i) {
        return tooltipLine(labels[i] || key, graphValue(point, key), unit, classes[i] || "");
      }).join("");
  } else {
    var key = box.getAttribute("data-chart-key");
    var meta = seriesByKey(key);
    html += key === "gridPowerW"
      ? networkBalanceTooltip(point, [key])
      : tooltipLine(meta.label, graphValue(point, key), unit || meta.unit || "", meta.cls);
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

function updateDocumentTitle() {
  var name = state.moduleName || state.deviceName || state.name || "Routeur solaire";
  document.title = name;
}

async function loadStatus() {
  var previousSimulationMode = lastSimulationModeSeen;
  state = await api(page === "diagnostic" ? "/api/diagnostic" : "/api/status-lite");
  if (previousSimulationMode === true && state.simulationMode === false) resetDashboardGraphBuffers();
  lastSimulationModeSeen = !!state.simulationMode;
  updateDocumentTitle();
}

async function refresh() {
  try {
    await loadStatus();
    if (page === "dashboard" && !dashboardConfigLoaded) {
      await loadDashboardConfig();
    }
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
    if ((page === "dashboard" || page === "diagnostic") && !cache.actuators) {
      try {
        cache.actuators = await api("/api/actuators");
      } catch (actuatorError) {
        cache.actuators = {actuators:[]};
        state.lastWebWarning = actuatorError.message || "Actionneurs indisponibles";
      }
    }
    if (page === "dashboard" || page === "diagnostic") {
      try {
        cache.actuatorRuntime = await api("/api/actuators/runtime");
      } catch (runtimeError) {
        cache.actuatorRuntime = {actuators:Array.isArray(state.actuators) ? state.actuators : []};
      }
    }
    recordDashboardHistory();
    if (page === "dashboard" && updateDashboardLiveSections()) {
      return;
    }
    render();
  } catch (error) {
    $("app").innerHTML = '<h1>Erreur API</h1><div class="banner">' + esc(error.message) + '</div><p><a href="/">Retour secours</a> <a href="/api/status-lite">Tester status-lite</a> <a href="/lite">Interface Lite embarquee</a></p>';
  }
}

function updateDashboardLiveSections() {
  var energy = $("dashboardLiveEnergy");
  var main = $("dashboardLiveMain");
  var bottom = $("dashboardLiveBottom");
  if (!energy || !main || !bottom) return false;
  energy.innerHTML = energyTopBand();
  main.innerHTML = energyFlowSvg() + pidCockpitCard();
  bottom.innerHTML = activeSensorsPanel() + systemHealthPanel();
  requestChartUpdate();
  renderDashboardCanvases();
  updateNavStatus();
  return true;
}

function recordDashboardHistory() {
  var now = Date.now();
  if (dashHistory.length && now - lastHistorySampleMs < historySampleIntervalMs) return;
  lastHistorySampleMs = now;
  pruneDashboardHistoryData();
  var data = {};
  var energy = stateEnergy();
  var graphGrid = gridDerivedForGraph(energy.gridPowerW);
  getAvailableDataSources(state).forEach(function (source) {
    (source.values || []).forEach(function (value) {
      data[dataSourceKey(source.sourceType, source.id || source.sensorId, value.key)] = sanitizeHistoryValue(value, value.currentValue);
    });
  });
  data[dataSourceKey("energy", "energy", "gridBuyW")] = graphGrid.buy;
  data[dataSourceKey("energy", "energy", "gridInjectionW")] = graphGrid.injection;
  dashHistory.push({
    t: now,
    schemaVersion: dashboardHistorySchemaVersion,
    data: data,
    gridPowerW: sanitizeHistoryValue({key:"gridPowerW"}, energy.gridPowerW),
    gridBuyW: graphGrid.buy,
    gridInjectionW: graphGrid.injection,
    productionW: sanitizeHistoryValue({key:"productionW"}, energy.productionW),
    injectionW: sanitizeHistoryValue({key:"injectionW"}, state.injectionW),
    consumptionW: sanitizeHistoryValue({key:"consumptionW"}, state.consumptionW),
    surplusW: sanitizeHistoryValue({key:"surplusW"}, state.surplusW),
    surplusAvailableW: hasValue(state.surplusW) ? Math.max(Number(state.surplusW), 0) : null,
    heaterPowerW: sanitizeHistoryValue({key:"heaterPowerW"}, state.heaterPowerW),
    heaterPowerPositiveW: hasValue(state.heaterPowerW) ? Math.max(Number(state.heaterPowerW), 0) : null,
    pidErrorW: sanitizeHistoryValue({key:"pidErrorW"}, pidState().errorW),
    pidOutputPercent: sanitizeHistoryValue({key:"pidOutputPercent"}, pidState().outputPercent),
    commandPercent: sanitizeHistoryValue({key:"commandPercent"}, pidState().commandPercent),
    targetW: sanitizeHistoryValue({key:"targetW"}, pidState().targetGridW),
    deadbandW: sanitizeHistoryValue({key:"deadbandW"}, pidState().deadbandW),
    ssr1PowerPct: sanitizeHistoryValue({key:"ssr1PowerPct"}, state.ssr1PowerPct),
    ssr2PowerPct: sanitizeHistoryValue({key:"ssr2PowerPct"}, state.ssr2PowerPct),
    robotDynPowerPct: sanitizeHistoryValue({key:"robotDynPowerPct"}, state.robotDynPowerPct),
    heapFree: Number(state.heapFree) || 0,
    simulationMode: !!state.simulationMode
  });
  while (dashHistory.length > historyMaxPoints) dashHistory.shift();
  saveDashboardHistorySoon(true);
}

function dashboard() {
  stopGraphPolling();
  var wifiCls = statusClass(state.wifiConnected, state.networkMode === "AP" || state.networkMode === "AP_STA");
  var safetyCls = state.safetyTripped || state.safetyLevel === "CRITICAL" ? "bad" : (state.safetyLevel === "WARNING" || state.safetyLevel === "DEGRADED" ? "warn" : "ok");
  var ssid = state.wifiSsid || "non renseigne";
  var ip = state.stationIp || state.localIp || "-";
  var moduleName = state.moduleName || "Routeur solaire";

  return banner() +
    '<header class="yasTopbar cockpitHeader"><div class="cockpitTitle"><div><span>Dashboard</span><h1>' + esc(moduleName) + '</h1></div>' + routerMoodPanel("headerMood") + '</div><div class="statusBadges">' +
      statusBadge("Role", roleInfo().label, "info", roleInfo().detail) +
      statusBadge("WiFi", state.wifiConnected ? "connecte" : "AP local", wifiCls, ssid) +
      statusBadge("Safety", state.safetyLevel || "OK", safetyCls, state.safetyReason || "aucun defaut") +
      statusBadge("Simulation", state.simulationMode ? "active" : "off", state.simulationMode ? "warn" : "muted", state.simulationMode ? simRemainingText() : "reel") +
    '</div></header>' +
    '<div class="cockpitDashboard">' +
      '<div id="dashboardLiveEnergy">' + energyTopBand() + '</div>' +
      '<section id="dashboardLiveMain" class="cockpitMainGrid">' + energyFlowSvg() + pidCockpitCard() + '</section>' +
      modernDashboardGraphs() +
      '<section id="dashboardLiveBottom" class="cockpitBottomGrid">' + activeSensorsPanel() + systemHealthPanel() + '</section>' +
      '</div>';
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

function outputStateText(on) {
  return on ? "GPIO ON" : "GPIO OFF";
}

function gpioLevelText(high) {
  return high ? "niveau HIGH" : "niveau LOW";
}

async function sensorsPage() {
  await loadStatus();
  cache.sensors = await api("/api/sensors");
  try {
    cache.espnow = await api("/api/espnow");
  } catch (e) {
    cache.espnow = {discoveredNodes:[], peers:[]};
  }
  drawSensorsPage();
}

function drawSensorsPage() {
  var sensors = cache.sensors.sensors || [];
  var ds = cache.sensors.ds18b20 || [];
  var rows = "";
  sensors.forEach(function (s, i) {
    ensureJsyChannels(s);
    var live = genericSensorState(s);
    rows += '<tr class="' + esc(live.cls) + '"><td>' + esc(s.name || s.id) + '</td><td>' + esc(s.type) + '</td><td>' + sensorRoleText(s) + '</td><td>' + pinText(s) + '</td><td>' + sensorStatusBadge(live, sensorDetailTip(s, live)) + '</td><td class="actions"><button onclick="editSensor(' + i + ')">Modifier</button><button onclick="toggleSensor(' + i + ')">' + (s.enabled !== false ? "Desactiver" : "Activer") + '</button><button class="danger" onclick="deleteSensor(' + i + ')">Supprimer</button></td></tr>';
  });
  ds.forEach(function (s, i) {
    var temp = dsTemperature(i);
    var available = dsAvailable(i, temp);
    var cls = s.enabled === false ? "muted" : (available ? "ok" : (s.critical ? "bad" : "warn"));
    var statusText = s.enabled === false ? "desactive" : (available ? "OK" : (s.critical ? "critique absent" : "absent"));
    rows += '<tr class="' + esc(cls) + '"><td>' + esc(s.name || s.id) + '</td><td>DS18B20</td><td>' + esc(s.role) + '</td><td>GPIO ' + esc((cache.sensors.oneWireBus || {}).gpio || 13) + '</td><td>' + sensorStatusBadge({cls:cls, text:statusText}, dsSensorDetailTip(s, i, temp, available)) + '</td><td class="actions"><button onclick="editDsSensor(' + i + ')">Modifier</button><button onclick="toggleDsSensor(' + i + ')">' + (s.enabled !== false ? "Desactiver" : "Activer") + '</button><button class="danger" onclick="deleteDsSensor(' + i + ')">Supprimer</button></td></tr>';
  });
  $("app").innerHTML = banner() + '<h1>Capteurs</h1>' + dirtyNotice("sensors") + '<div class="toolbar"><button onclick="newSensor()">Ajouter capteur</button><button onclick="saveSensors()">Sauvegarder</button><button onclick="jsonEditor(\'sensors\')">JSON avance</button></div>' + oneWireBusPanel() + remoteAvailableSensorsPanel() + '<section id="sensorForm"></section><table><tr><th>Nom</th><th>Type</th><th>Role</th><th>Bus/GPIO</th><th>Etat/Valeur</th><th>Actions</th></tr>' + rows + '</table><pre id="scan"></pre>';
}

function oneWireBusPanel() {
  var bus = cache.sensors.oneWireBus || {};
  var gpio = bus.gpio == null ? 13 : Number(bus.gpio);
  var gpioChoices = [4, 5, 13, 14, 15, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33];
  if (gpioChoices.indexOf(gpio) < 0) gpioChoices.push(gpio);
  gpioChoices.sort(function (a, b) { return a - b; });
  return '<details class="panel oneWirePanel"><summary class="compactPanelHead"><div class="compactPanelTitle"><h2>Bus OneWire DS18B20</h2>' + inlineHelp("GPIO commun a toutes les sondes DS18B20. Le scan utilise ce bus et les sondes individuelles ne reglent plus leur GPIO.") + '</div><p>Toutes les sondes DS18B20 partagent ce bus.</p></summary><div class="oneWireSettingsGrid">' +
    '<label class="settingTile"><span>GPIO des sondes</span><select id="oneWireGpio">' + options(gpioChoices.map(String), String(gpio)) + '</select></label>' +
    '<label class="settingTile toggleSetting"><span>Bus actif</span><input id="oneWireEnabled" type="checkbox" ' + checked(bus.enabled !== false) + '><b data-on="Actif" data-off="Inactif"></b></label>' +
    '<label class="settingTile toggleSetting"><span>Scan au demarrage</span><input id="oneWireScanBoot" type="checkbox" ' + checked(bus.scanOnBoot !== false) + '><b data-on="Actif" data-off="Inactif"></b></label>' +
    '<label class="settingTile"><span>Intervalle lecture</span><input id="oneWireReadMs" type="number" min="1000" max="60000" value="' + esc(bus.readIntervalMs || 2000) + '"><small>ms</small></label>' +
    '</div><div class="compactPanelActions"><button onclick="applyOneWireBus()">Appliquer bus OneWire</button><button onclick="scanDs()">Scanner sur ce bus OneWire</button><span class="fieldHelp">Resistance de tirage conseillee : 4,7 kOhm entre DATA et 3V3.</span></div></details>';
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
    {id:"clamp1", name:"Pince 1", role:"production", measures:["current", "power", "pf", "energyPos", "energyNeg", "direction"]},
    {id:"clamp2", name:"Pince 2", role:"grid", measures:["current", "power", "pf", "energyPos", "energyNeg", "direction"]}
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

function sensorStatusBadge(live, tip) {
  return '<span class="statusTip" tabindex="0"><span class="badge ' + esc(live.cls || "") + '">' + esc(live.text || "") + '</span>' + (tip ? '<span class="hoverTip">' + tip + '</span>' : '') + '</span>';
}

function sensorDetailTip(s, live) {
  var name = s.name || s.id || "Capteur";
  var html = tipLine("Etat", live.text || "-", "", live.cls || "");
  html += tipLine("Nom", name, "");
  html += tipLine("Type", s.type || "N/A", "");
  html += tipLine("Source", (s.source || "local").toUpperCase(), "");
  html += tipLine("Role", s.role || "N/A", "");
  if ((s.source || "") === "espnow") {
    html += tipLine("MAC", s.mac || "N/A", "");
    if (s.remoteSensorId != null && s.remoteSensorId !== "") html += tipLine("sensorId", s.remoteSensorId, "");
  }
  html += sensorValueTipLines(s);
  return html;
}

function dsSensorDetailTip(s, index, temp, available) {
  return tipLine("Etat", available ? "OK" : (s.enabled === false ? "desactive" : "Absent / non lu"), "", available ? "ok" : (s.enabled === false ? "muted" : "bad")) +
    tipLine("Nom", s.name || s.id || "DS18B20", "") +
    tipLine("Role", s.role || "N/A", "") +
    tipLine("Bus", "GPIO " + ((cache.sensors.oneWireBus || {}).gpio || 13), "") +
    tipLine("Temperature", available ? temp : "N/A", available ? "C" : "") +
    tipLine("Derniere lecture", timeFromUptimeMs(dsLastReadMs(index)), "");
}

function sensorValueTipLines(s) {
  var type = s.type || "";
  var lines = "";
  if ((s.source || "") === "espnow") {
    if (type === "TIC Linky") return tipLine("GRID", state.ticGridPowerW, "W") + tipLine("PAPP", state.ticApparentPowerVA, "VA") + tipLine("IINST", state.ticCurrentA, "A");
    if (type === "JSY-MK-194T") return jsyValueTipLines(s);
    if (type === "DS18B20") {
      var remoteTemp = remoteSensorValue(s, "TEMP");
      return tipLine("TEMP", remoteTemp ? remoteTemp.value : temperatureValueForRole(remoteSensorRoleForConfig(s)), "C");
    }
    if (type === "Battery") return tipLine("BATV", state.batteryVoltageV, "V") + tipLine("BATA", state.batteryCurrentA, "A") + tipLine("BATP", state.batteryPowerW, "W") + tipLine("SOC", state.batterySocPct, "%");
    if (type === "Solar") return tipLine("POWER", state.productionW, "W");
  }
  if ((s.id || "") === "jsy_grid" || type === "JSY-MK-194T") {
    lines += jsyValueTipLines(s);
  } else if ((s.id || "") === "tic_linky" || type === "TIC Linky") {
    lines += tipLine("GRID", state.ticGridPowerW, "W");
    lines += tipLine("PAPP", state.ticApparentPowerVA, "VA");
    lines += tipLine("IINST", state.ticCurrentA, "A");
    lines += tipLine("Index", state.ticEnergyWh, "Wh");
  }
  return lines;
}

function jsyValueTipLines(s) {
  var channels = Array.isArray(s && s.channels) ? s.channels : defaultJsyChannels();
  var ch1 = channels[0] || defaultJsyChannels()[0];
  var ch2 = channels[1] || defaultJsyChannels()[1];
  return tipLine("Tension", state.gridVoltageV, "V") +
    tipLine("Frequence", state.gridFrequencyHz, "Hz") +
    tipLine((ch1.name || "Pince 1") + " puissance", state.activePowerW1, "W") +
    tipLine((ch1.name || "Pince 1") + " courant", state.currentA1, "A") +
    tipLine((ch1.name || "Pince 1") + " PF", state.powerFactor1, "") +
    tipLine((ch2.name || "Pince 2") + " puissance", state.activePowerW2, "W") +
    tipLine((ch2.name || "Pince 2") + " courant", state.currentA2, "A") +
    tipLine((ch2.name || "Pince 2") + " PF", state.powerFactor2, "");
}

function genericSensorState(s) {
  if (s.enabled === false) return {cls:"muted", text:"desactive", detail:""};
  var id = s.id || "";
  var type = s.type || "";
  var source = s.source || "";
  if (source === "espnow") {
    if (id === "jsy_grid" || type === "JSY-MK-194T") return state.jsyOnline ? {cls:"ok", text:"OK", detail:"ESP-NOW JSY"} : {cls:"bad", text:"Erreur", detail:"JSY ESP-NOW absent"};
    if (id === "tic_linky" || type === "TIC Linky") return state.ticAvailable ? {cls:"ok", text:"OK", detail:"ESP-NOW Linky"} : {cls:"warn", text:"Absent", detail:"Linky ESP-NOW non lu"};
    if (type === "Battery") return state.batteryOnline ? {cls:"ok", text:"OK", detail:"ESP-NOW batterie"} : {cls:"warn", text:"Attente", detail:"batterie ESP-NOW non lue"};
    if (type === "Solar") return hasValue(state.productionW) ? {cls:"ok", text:"OK", detail:fmt(state.productionW) + " W"} : {cls:"warn", text:"Attente", detail:"solaire ESP-NOW non lu"};
    if (type === "DS18B20") {
      var remoteTemp = remoteSensorValue(s, "TEMP");
      var fallbackTemp = temperatureValueForRole(remoteSensorRoleForConfig(s));
      return (remoteTemp && hasValue(remoteTemp.value)) || hasValue(fallbackTemp) ? {cls:"ok", text:"OK", detail:"ESP-NOW temperature"} : {cls:"warn", text:"Attente", detail:"TEMP non recue"};
    }
    return {cls:"ok", text:"actif", detail:"ESP-NOW configure"};
  }
  if (id === "jsy_grid" || type === "JSY-MK-194T") return state.jsyOnline ? {cls:"ok", text:"OK", detail:"trame valide"} : {cls:"bad", text:"Erreur", detail:"JSY absent ou timeout"};
  if (id === "tic_linky" || type === "TIC Linky") return state.ticAvailable ? {cls:"ok", text:"OK", detail:"trame valide"} : {cls:"warn", text:"Absent", detail:"TIC non lue"};
  return {cls:"ok", text:"actif", detail:"configure"};
}

function isSolarRouterReferenceSensor(s) {
  var source = String(state.gridPowerSource || "JSY").toUpperCase();
  var id = s.id || "";
  var type = s.type || "";
  if (source === "JSY") return id === "jsy_grid" || type === "JSY-MK-194T";
  if (source === "TIC") return id === "tic_linky" || type === "TIC Linky";
  if (source === "AUTO") {
    if (state.ticAvailable) return id === "tic_linky" || type === "TIC Linky";
    if (state.jsyOnline) return id === "jsy_grid" || type === "JSY-MK-194T";
  }
  return false;
}

function sensorRoleText(s) {
  if ((s.source || "") === "espnow") {
    return esc(remoteSensorRoleForConfig(s)) + (isSolarRouterReferenceSensor(s) ? ' <span class="badge ok">Reference routeur</span>' : "");
  }
  if ((s.id || "") === "jsy_grid" || (s.type || "") === "JSY-MK-194T") {
    var channels = s.channels || [];
    if (channels.length) {
      var html = channels.map(function (c, i) {
        return esc(c.name || c.id || ("voie " + (i + 1))) + " : " + esc(c.role || "non defini");
      }).join(" / ");
      if (isSolarRouterReferenceSensor(s)) html += ' <span class="badge ok">Reference routeur</span>';
      return html;
    }
  }
  return esc(s.role || "") + (isSolarRouterReferenceSensor(s) ? ' <span class="badge ok">Reference routeur</span>' : "");
}

function sensorExtraValue(s) {
  if ((s.source || "") === "espnow") {
    var type = s.type || "";
    if (type === "TIC Linky") {
      return '<div class="channelGrid"><span><b>GRID</b> ' + esc(fmt(state.ticGridPowerW)) + ' W</span><span><b>PAPP</b> ' + esc(fmt(state.ticApparentPowerVA)) + ' VA</span><span><b>IINST</b> ' + esc(fmt(state.ticCurrentA)) + ' A</span></div>';
    }
    if (type === "JSY-MK-194T") {
      return jsyChannelGridHtml(s);
    }
    if (type === "DS18B20") {
      var remoteTemp = remoteSensorValue(s, "TEMP");
      var role = remoteSensorRoleForConfig(s);
      var temp = remoteTemp ? remoteTemp.value : temperatureValueForRole(role);
      return '<div class="channelGrid"><span><b>TEMP</b> ' + esc(fmt(temp)) + ' C</span><span><small>' + esc(role || "role non defini") + '</small></span></div>';
    }
    if (type === "Battery") {
      return '<div class="channelGrid"><span><b>BATV</b> ' + esc(fmt(state.batteryVoltageV)) + ' V</span><span><b>BATA</b> ' + esc(fmt(state.batteryCurrentA)) + ' A</span><span><b>BATP</b> ' + esc(fmt(state.batteryPowerW)) + ' W</span><span><b>SOC</b> ' + esc(fmt(state.batterySocPct)) + ' %</span></div>';
    }
    if (type === "Solar") {
      return '<div class="channelGrid"><span><b>POWER</b> ' + esc(fmt(state.productionW)) + ' W</span></div>';
    }
  }
  if ((s.id || "") !== "jsy_grid" && (s.type || "") !== "JSY-MK-194T") return "";
  return jsyChannelGridHtml(s);
}

function jsyChannelGridHtml(s) {
  var channels = Array.isArray(s.channels) ? s.channels : defaultJsyChannels();
  var ch1 = channels[0] || defaultJsyChannels()[0];
  var ch2 = channels[1] || defaultJsyChannels()[1];
  return '<div class="channelGrid">' +
    '<span><b>Commun</b> ' + esc(fmt(state.gridVoltageV)) + ' V / ' + esc(fmt(state.gridFrequencyHz)) + ' Hz</span>' +
    '<span><b>' + esc(ch1.name || ch1.id || "Pince 1") + '</b> <small>' + esc(ch1.role || "custom") + '</small> ' + esc(fmt(state.activePowerW1)) + ' W / ' + esc(fmt(state.currentA1)) + ' A / PF ' + esc(fmt(state.powerFactor1)) + '</span>' +
    '<span><b>' + esc(ch2.name || ch2.id || "Pince 2") + '</b> <small>' + esc(ch2.role || "custom") + '</small> ' + esc(fmt(state.activePowerW2)) + ' W / ' + esc(fmt(state.currentA2)) + ' A / PF ' + esc(fmt(state.powerFactor2)) + '</span>' +
    '<span><b>Role grid</b> ' + esc(fmt(state.gridPowerW)) + ' W / ' + esc(fmt(state.gridCurrentA)) + ' A</span>' +
    '<span><b>Surplus</b> ' + esc(fmt(state.surplusW)) + ' W</span>' +
    '</div>';
}

function remoteAvailableSensorsPanel() {
  var sensors = remoteDiscoveredSensors();
  var rows = "";
  var currentMac = "";
  sensors.forEach(function (sensor, i) {
    var mac = String(sensor.mac || "").toUpperCase();
    if (mac !== currentMac) {
      currentMac = mac;
      rows += '<tr class="groupRow"><td colspan="6">' + esc(sensor.nodeLabel || sensor.nodeName || "ESP source") + ' <span class="muted">' + esc(mac) + '</span></td></tr>';
    }
    var alreadyAdded = isRemoteSensorConfigured(sensor);
    rows += '<tr class="' + (alreadyAdded ? 'muted' : '') + '"><td>' + esc(sensor.sensorName || "Capteur distant") + '</td><td>' + esc(remoteSensorConfigType(sensor)) + '</td><td>' + esc(remoteSensorConfigRole(sensor)) + '</td><td><span class="badge info">ESP-NOW</span><br><small>sensorId ' + esc(sensor.sensorId) + '</small></td><td>' + remoteSensorValuesPreview(sensor) + '</td><td>' + (alreadyAdded ? '<span class="badge muted">Deja ajoute</span>' : '<button onclick="addDiscoveredRemoteSensor(' + i + ')">Ajouter</button>') + '</td></tr>';
  });
  if (!rows) return "";
  return '<details class="panel remoteSensorsPanel"><summary class="compactPanelHead"><div class="compactPanelTitle"><h2>Capteurs ESP-NOW disponibles</h2></div><p>Decouverts automatiquement par SENSOR_DISCOVERY.</p></summary><table><tr><th>Nom</th><th>Type</th><th>Role</th><th>Source</th><th>Valeurs</th><th>Action</th></tr>' + rows + '</table></details>';
}

function remoteDiscoveredSensors() {
  return (state.remoteSensors || []).filter(function (sensor) {
    if (!sensor || sensor.sensorId == null || Number(sensor.sensorId) === 0) return false;
    if (!sensor.mac) return false;
    return true;
  }).sort(function (a, b) {
    var am = String(a.mac || "").toUpperCase();
    var bm = String(b.mac || "").toUpperCase();
    if (am !== bm) return am.localeCompare(bm);
    return Number(a.sensorId || 0) - Number(b.sensorId || 0);
  });
}

function isRemoteSensorConfigured(sensor) {
  var configured = (cache.sensors && cache.sensors.sensors) || [];
  return configured.some(function (item) {
    return (item.source || "") === "espnow" &&
      String(item.mac || "").toUpperCase() === String(sensor.mac || "").toUpperCase() &&
      Number(item.remoteSensorId) === Number(sensor.sensorId);
  });
}

function remoteSensorConfigType(sensor) {
  var text = String(sensor.sensorTypeText || "").toUpperCase();
  var type = Number(sensor.sensorType);
  if (text === "LINKY" || type === 1) return "TIC Linky";
  if (text === "JSY" || type === 2) return "JSY-MK-194T";
  if (text === "DS18B20" || text === "TEMP_HUM" || type === 3 || type === 4) return "DS18B20";
  if (text === "BATTERY" || type === 5) return "Battery";
  if (text === "SOLAR" || type === 6) return "Solar";
  return "Virtual";
}

function remoteSensorConfigRole(sensor) {
  var role = remoteSensorDiscoveryRole(sensor);
  if (role) return role;
  var type = remoteSensorConfigType(sensor);
  if (type === "TIC Linky") return "compteur_officiel";
  if (type === "JSY-MK-194T") return "mesure_reseau_principal";
  if (type === "Battery") return "stockage_principal";
  if (type === "Solar") return "production";
  if (type === "DS18B20") return "autre";
  return "custom";
}

function remoteSensorDiscoveryRole(sensor) {
  return (sensor && (sensor.sensorRole || sensor.role || sensor.sensor_role || sensor.remoteRole)) || "";
}

function remoteSensorValuesPreview(sensor) {
  var values = remoteSensorValueList(sensor);
  if (!values.length) return '<span class="muted">En attente valeurs</span>';
  return values.map(function (v) {
    return '<span class="badge muted">' + esc(remoteValueLabel(v.key || ("vt" + v.valueType))) + (v.unit ? " " + esc(v.unit) : "") + '</span>';
  }).join(" ");
}

function remoteSensorValueList(sensor) {
  var values = sensor && sensor.values;
  if (Array.isArray(values)) return values;
  if (values && typeof values === "object") {
    return Object.keys(values).map(function (key) {
      return {key:key, value:values[key]};
    });
  }
  return [];
}

function remoteValueLabel(key) {
  var labels = {
    VOLT:"Tension",
    FREQ:"Frequence",
    CH1_CURR:"Pince 1 courant",
    CH1_POWER:"Pince 1 puissance",
    CH1_PF:"Pince 1 PF",
    CH1_ENERGY_POS:"Pince 1 energie +",
    CH1_ENERGY_NEG:"Pince 1 energie -",
    CH1_EPOS:"Pince 1 energie +",
    CH1_ENEG:"Pince 1 energie -",
    CH1_DIR:"Pince 1 sens",
    CH2_CURR:"Pince 2 courant",
    CH2_POWER:"Pince 2 puissance",
    CH2_PF:"Pince 2 PF",
    CH2_ENERGY_POS:"Pince 2 energie +",
    CH2_ENERGY_NEG:"Pince 2 energie -",
    CH2_EPOS:"Pince 2 energie +",
    CH2_ENEG:"Pince 2 energie -",
    CH2_DIR:"Pince 2 sens"
  };
  return labels[key] || key;
}

async function addDiscoveredRemoteSensor(index) {
  var sensor = remoteDiscoveredSensors()[index];
  if (!sensor) return;
  if (isRemoteSensorConfigured(sensor)) return;
  cache.sensors = cache.sensors || await api("/api/sensors");
  cache.sensors.sensors = cache.sensors.sensors || [];
  var item = {
    id: remoteSensorGeneratedId(sensor),
    name: sensor.sensorName || remoteSensorConfigType(sensor),
    type: remoteSensorConfigType(sensor),
    role: remoteSensorConfigRole(sensor),
    source: "espnow",
    mac: sensor.mac || "",
    remoteNode: sensor.nodeName || "",
    remoteSensorId: Number(sensor.sensorId),
    remoteKey: "ALL",
    enabled: true,
    debug: false
  };
  if (item.type === "JSY-MK-194T") item.channels = defaultJsyChannels();
  cache.sensors.sensors.push(item);
  var response = await postJson("/api/sensors", cache.sensors);
  if (!response.ok) return alert("Ajout capteur ESP-NOW refuse: " + await response.text());
  cache.sensors = await api("/api/sensors");
  await refresh();
}

function remoteSensorGeneratedId(sensor) {
  var mac = String(sensor.mac || "").replace(/[^A-Fa-f0-9]/g, "").slice(-6).toLowerCase();
  return "espnow_" + (mac || "node") + "_" + String(sensor.sensorId || "sensor");
}

function remoteSensorForConfig(s) {
  var list = state.remoteSensors || [];
  var mac = String(s.mac || "").toUpperCase();
  var remoteSensorId = s.remoteSensorId == null || s.remoteSensorId === "" ? null : Number(s.remoteSensorId);
  var type = s.type || "";
  return list.find(function (item) {
    if (String(item.mac || "").toUpperCase() !== mac) return false;
    if (remoteSensorId != null && Number(item.sensorId) !== remoteSensorId) return false;
    if (type === "TIC Linky") return item.sensorTypeText === "LINKY" || item.sensorType === 1;
    if (type === "JSY-MK-194T") return item.sensorTypeText === "JSY" || item.sensorType === 2;
    if (type === "DS18B20") return item.sensorTypeText === "DS18B20" || item.sensorTypeText === "TEMP_HUM" || item.sensorType === 3 || item.sensorType === 4;
    if (type === "Battery") return item.sensorTypeText === "BATTERY" || item.sensorType === 5;
    if (type === "Solar") return item.sensorTypeText === "SOLAR" || item.sensorType === 6;
    return true;
  }) || null;
}

function remoteSensorValue(s, key) {
  var sensor = remoteSensorForConfig(s);
  if (!sensor || sensor.ok === false) return null;
  key = String(key || "").toUpperCase();
  return remoteSensorValueList(sensor).find(function (v) {
    return String(v.key || "").toUpperCase() === key || valueTypeLabel(Number(v.valueType)).toUpperCase() === key;
  }) || null;
}

function remoteSensorRoleForConfig(s) {
  var sensor = remoteSensorForConfig(s);
  return remoteSensorDiscoveryRole(sensor) || s.role || "";
}

function temperatureValueForRole(role) {
  return null;
}

function espNowNodeOptions(selectedMac) {
  var nodes = (cache.espnow && cache.espnow.discoveredNodes) || [];
  var opts = ['<option value="">Selectionner un ESP detecte</option>'];
  nodes.forEach(function (node) {
    var label = (node.nodeName || "ESP") + " - " + (node.mac || "") + " - " + (node.primarySensorText || "ESP-NOW");
    opts.push('<option value="' + esc(node.mac || "") + '" ' + (String(node.mac || "") === String(selectedMac || "") ? "selected" : "") + '>' + esc(label) + '</option>');
  });
  return opts.join("");
}

function espNowMeasureOptions(selected) {
  var keys = ["ALL", "GRID", "PAPP", "IINST", "SINSTS", "BASE", "VOLT", "FREQ", "CH1_CURR", "CH1_POWER", "CH1_PF", "CH1_EPOS", "CH1_ENEG", "CH1_DIR", "CH2_CURR", "CH2_POWER", "CH2_PF", "CH2_EPOS", "CH2_ENEG", "CH2_DIR", "TEMP", "HUM", "BATV", "BATA", "SOC", "BATP"];
  return options(keys, selected || "ALL");
}

function espNowSensorTypeOptions(selected) {
  return options(["TIC Linky", "JSY-MK-194T", "DS18B20", "Battery", "Solar", "Analog", "Virtual"], selected || "TIC Linky");
}

function espNowProfileHelp(type) {
  if (type === "TIC Linky") return "ALL importe GRID, PAPP, IINST, SINSTS et BASE. GRID positif = achat reseau, negatif = injection.";
  if (type === "JSY-MK-194T") return "ALL importe VOLT/FREQ et les mesures completes des deux pinces JSY.";
  if (type === "DS18B20") return "ALL importe TEMP et HUM. Le role choisi localement decide comment TEMP est utilisee par le routeur.";
  if (type === "Battery") return "ALL importe BATV, BATA, SOC et BATP pour l'etat batterie.";
  if (type === "Solar") return "ALL importe POWER, ENERGY, VOLT et CURR pour la production solaire distante.";
  return "ALL importe toutes les valeurs connues envoyees par ce noeud ESP-NOW.";
}

function readOnlyLine(label, value, badge) {
  return '<div class="readonlyLine"><span>' + esc(label) + '</span><b>' + esc(value == null || value === "" ? "-" : value) + '</b>' + (badge ? '<em class="badge info">' + esc(badge) + '</em>' : '') + '</div>';
}

function espNowOriginPanel(s) {
  var remote = remoteSensorForConfig(s) || {};
  var age = remote.ageMs != null ? fmt(remote.ageMs) + " ms" : (s.lastReceivedMs ? timeFromUptimeMs(s.lastReceivedMs) : "N/A");
  var lastFrame = remote.lastReceivedMs || remote.lastSeenMs || s.lastReceivedMs || "";
  var ok = remote.ok !== false && remote.timedOut !== true && (remote.ageMs == null || Number(remote.ageMs) < 30000);
  return '<div class="subPanel espNowOriginPanel"><h3>Origine ESP-NOW <span class="badge info">ESP-NOW</span></h3><div class="readonlyGrid">' +
    readOnlyLine("Nom noeud distant", remote.nodeName || s.remoteNode || "-") +
    readOnlyLine("MAC noeud distant", remote.mac || s.mac || "-") +
    readOnlyLine("Nom capteur distant", remote.sensorName || s.name || "-") +
    readOnlyLine("Type capteur distant", remote.sensorTypeText || s.type || "-") +
    readOnlyLine("Derniere trame recue", lastFrame ? timeFromUptimeMs(lastFrame) : "N/A") +
    readOnlyLine("Age derniere mesure", age) +
    readOnlyLine("Etat", ok ? "OK" : "perdu", ok ? "OK" : "perdu") +
    '</div><p class="muted">ESP-NOW est l origine de ce capteur. Les identifiants distants ne se modifient pas ici.</p></div>';
}

function sensorFormHtml(kind, index, s) {
  s = s || {};
  var isDs = kind === "ds";
  ensureJsyChannels(s);
  var type = isDs ? "DS18B20" : (s.type || "Virtual");
  var isEspNow = !isDs && (s.source || "") === "espnow";
  var isJsy = !isDs && ((s.id || "") === "jsy_grid" || type === "JSY-MK-194T");
  var isTic = !isDs && ((s.id || "") === "tic_linky" || type === "TIC Linky");
  var isLocalJsy = isJsy && !isEspNow;
  var isLocalTic = isTic && !isEspNow;
  var channels = s.channels || defaultJsyChannels();
  var roleOptions = sensorRolesForType(type);
  var selectedRole = isEspNow ? remoteSensorRoleForConfig(s) : (s.role || "");
  if (selectedRole && roleOptions.indexOf(selectedRole) < 0) roleOptions = [selectedRole].concat(roleOptions);
  var ticMode = s.mode || "historique";
  var ticBaudrate = s.baudrate || (ticMode === "standard" ? 9600 : 1200);
  var showEspNowExport = isDs || (!isEspNow && (isLocalJsy || isLocalTic || type === "Battery" || type === "Solar" || type === "Virtual"));
  var typeHtml = isDs ? '<input id="sensorType" type="hidden" value="DS18B20">' : (isLocalJsy ? '<label>Type<input id="sensorType" value="JSY-MK-194T" readonly></label>' : '<label>Type<select id="sensorType" onchange="updateSensorRoleOptions()">' + (isEspNow ? espNowSensorTypeOptions(type) : options(sensorTypes, type)) + '</select></label>');
  var roleHtml = isLocalJsy ? '<input id="sensorRole" type="hidden" value="' + esc(s.role || "mesure_reseau_principal") + '">' : '<label>Role<select id="sensorRole">' + options(roleOptions, selectedRole || roleOptions[0] || "custom") + '</select></label>';
  var sourceHtml = "";
  if (isDs) {
    sourceHtml = textField("sensorAddress", "Adresse OneWire", s.address || "") + '<p class="fieldHelp">Le GPIO ne se regle pas par sonde: il est commun aux DS18B20 dans le bloc Bus OneWire ci-dessus.</p>';
  } else if (isEspNow) {
    sourceHtml = '<input id="sensorSource" type="hidden" value="espnow"><input id="sensorMac" type="hidden" value="' + esc(s.mac || "") + '"><input id="sensorRemoteNode" type="hidden" value="' + esc(s.remoteNode || "") + '"><input id="sensorRemoteSensorId" type="hidden" value="' + esc(s.remoteSensorId == null ? "" : s.remoteSensorId) + '"><input id="sensorRemoteKey" type="hidden" value="' + esc(s.remoteKey || s.key || "ALL") + '">' + espNowOriginPanel(s) + '<div class="subPanel"><h3>Debug ESP-NOW</h3><div class="formGrid">' + selectField("espNowDebug", "Logger ce capteur", s.debug === true) + '</div><p class="muted">Active les logs detailles pour ce capteur distant uniquement: SENSOR_DATA, FAST_DATA, discovery et erreurs de mapping.</p></div>';
  } else if (isLocalJsy) {
    sourceHtml = '<input id="sensorSource" type="hidden" value="local"><input id="sensorMac" type="hidden" value="">' + '<div class="formGrid">' + field("sensorRx", "RX", s.rx == null ? 26 : s.rx) + field("sensorTx", "TX", s.tx == null ? 27 : s.tx) + '</div>';
  } else {
    sourceHtml = field("sensorGpio", "GPIO", s.gpio == null ? "" : s.gpio) + field("sensorRx", "RX", s.rx == null ? "" : s.rx) + field("sensorTx", "TX", s.tx == null ? "" : s.tx) + textField("sensorSource", "Source", s.source || "local") + textField("sensorMac", "MAC ESP-NOW", s.mac || "");
  }
  var advancedHtml = '<details class="advancedField"><summary>Avance</summary>';
  if (isLocalJsy) {
    advancedHtml += '<div class="readonlyGrid">' +
      readOnlyLine("ID technique", s.id || uniqueSensorId(s.name || "JSY reseau", index, kind)) +
      readOnlyLine("Baudrate reel detecte", state.jsyBaudrate || s.baudrate || "auto") +
      readOnlyLine("Derniere erreur JSY", state.jsyLastError || "-") +
      readOnlyLine("Age derniere mesure", state.lastJsyReadMs ? fmt(Math.max(0, ((Number(state.uptimeMs) || Number(state.uptime) * 1000 || 0) - Number(state.lastJsyReadMs)))) + " ms" : "N/A") +
      readOnlyLine("Compteur timeout", state.jsyTimeoutCount || 0) +
      readOnlyLine("Compteur CRC", state.jsyCrcErrorCount || 0) +
      '</div><p class="advancedActions"><button type="button" onclick="testJsy()">Tester JSY</button><button type="button" onclick="reconfigureJsy19200()">Passage en 19200</button></p><p class="muted">Le boot detecte seulement la vitesse qui fonctionne. Le passage en 19200 se lance ici volontairement, avec logs detailles.</p>';
  } else {
    advancedHtml += '<div class="readonlyGrid">' + readOnlyLine("ID technique", s.id || uniqueSensorId(s.name || type, index, kind)) + '</div>';
  }
  advancedHtml += '</details>';
  return '<h2>' + (index >= 0 ? "Modifier" : "Ajouter") + ' ' + (isDs ? "DS18B20" : "capteur") + '</h2><input id="sensorKind" type="hidden" value="' + kind + '"><input id="sensorIndex" type="hidden" value="' + index + '">' +
    '<div class="form">' +
    textField("sensorName", "Nom", s.name || "") +
    typeHtml +
    roleHtml +
    sourceHtml +
    '<label><input id="sensorEnabled" class="check" type="checkbox" ' + checked(s.enabled) + '> Actif</label>' +
    (isDs ? '<label><input id="sensorCritical" class="check" type="checkbox" ' + checked(s.critical) + '> Critique securite</label>' : '') +
    (showEspNowExport ? espNowExportPanel(s, isDs ? "ds18b20" : type) : '') +
    (isLocalJsy ? '<div class="subPanel"><h3>Modbus JSY</h3><div class="formGrid">' + field("jsyAddress", "Adresse Modbus", s.modbusAddress || s.address || 1) + '<label>Vitesse bauds<select id="jsyBaudrate">' + options(["19200", "4800"], String(s.baudrate || state.jsyBaudrate || 19200)) + '</select></label>' + field("jsyReadInterval", "Lecture ms", s.readIntervalMs || 350) + field("jsyTimeout", "Timeout ms", s.timeoutMs || 300) + '</div><p class="muted">La vitesse configuree est conservee au redemarrage. Si le capteur ne repond pas au boot, le firmware ne force plus un retour en 4800.</p></div><div class="subPanel"><h3>Voies amperemetriques JSY</h3><div class="formGrid"><label>Pince 1 nom<input id="jsyCh1Name" value="' + esc((channels[0] || {}).name || "Pince 1") + '"></label><label>Pince 1 role<select id="jsyCh1Role">' + options(jsyClampRoles, (channels[0] || {}).role || "production") + '</select></label><label>Pince 2 nom<input id="jsyCh2Name" value="' + esc((channels[1] || {}).name || "Pince 2") + '"></label><label>Pince 2 role<select id="jsyCh2Role">' + options(jsyClampRoles, (channels[1] || {}).role || "grid") + '</select></label></div><p class="muted">grid = arrivee reseau, production = solaire, load = charge dediee, custom = autre usage.</p></div>' : '') +
    (isLocalTic ? '<div class="subPanel"><h3>TIC Linky</h3><div class="formGrid"><label>Mode TIC<select id="ticMode" onchange="syncTicBaudrate()">' + options(["historique", "standard"], ticMode) + '</select></label>' + field("ticBaudrate", "Vitesse bauds", ticBaudrate) + field("ticTimeout", "Timeout ms", s.timeoutMs || 5000) + selectField("ticDebug", "Debug TIC ++", s.debug === true) + '</div><p class="muted">Standard = 9600 bauds 7E1. Historique = 1200 bauds 7E1. Active le debug seulement pendant les tests: il ajoute TIC_BAD_LINE et TIC_DECODE. TIC_DECODE utilise le tableau 6.2.2 Enedis: unites, horodates, triphase et producteur.</p></div>' : '') +
    '<input id="sensorId" type="hidden" value="' + esc(s.id || "") + '">' + advancedHtml +
    '</div><p class="formActions"><button onclick="applySensorForm()">Appliquer</button><button onclick="cancelSensorEdit()">Annuler</button></p>';
}

function espNowExportPanel(s, type) {
  var interval = s.espNowExportIntervalMs || (type === "JSY-MK-194T" ? 200 : (type === "ds18b20" ? 10000 : 1000));
  var minDelta = s.espNowMinDelta;
  if (minDelta == null) minDelta = type === "JSY-MK-194T" ? 5 : (type === "ds18b20" ? 0.2 : 10);
  var priority = s.espNowExportPriority == null ? (type === "JSY-MK-194T" ? 3 : (type === "ds18b20" ? 0 : 1)) : s.espNowExportPriority;
  return '<div class="subPanel"><h3>Export ESP-NOW</h3><div class="formGrid">' +
    selectField("espNowExportEnabled", "Exporter ce capteur", s.espNowExportEnabled === true) +
    field("espNowExportInterval", "Envoi toutes les ms", interval) +
    selectField("espNowSendOnChange", "Envoyer sur variation", s.espNowSendOnChange !== false) +
    field("espNowMinDelta", "Variation mini", minDelta) +
    '<label>Priorite<select id="espNowExportPriority">' + espNowPriorityOptions(priority) + '</select></label>' +
    '</div><p class="muted">Si cette option est inactive, le capteur reste local uniquement et aucune trame capteur ESP-NOW n est envoyee pour lui.</p></div>';
}

function espNowPriorityOptions(selected) {
  var list = [{value:0,label:"Basse"},{value:1,label:"Normale"},{value:2,label:"Haute"},{value:3,label:"Critique"}];
  return list.map(function (item) {
    return '<option value="' + item.value + '" ' + (String(item.value) === String(selected) ? "selected" : "") + '>' + esc(item.label) + '</option>';
  }).join("");
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
  if ($("espNowProfileHelp")) $("espNowProfileHelp").textContent = espNowProfileHelp(type);
}

function syncTicBaudrate() {
  if (!$("ticMode") || !$("ticBaudrate")) return;
  $("ticBaudrate").value = $("ticMode").value === "standard" ? 9600 : 1200;
}

function syncEspNowMacFromNode() {
  if (!$("sensorEspNowNode") || !$("sensorMac")) return;
  var mac = $("sensorEspNowNode").value;
  if (!mac) return;
  $("sensorMac").value = mac;
  var nodes = (cache.espnow && cache.espnow.discoveredNodes) || [];
  var node = nodes.find(function (item) { return item.mac === mac; });
  if (node && $("sensorRemoteNode")) $("sensorRemoteNode").value = node.nodeName || "";
}

async function testJsy() {
  await refresh();
  alert(state.jsyOnline ? "JSY OK - baudrate " + (state.jsyBaudrate || "-") : "JSY absent: " + (state.jsyLastError || "timeout"));
}

async function reconfigureJsy19200() {
  if (!confirm("Lancer le passage du JSY en 19200 bauds ? Les etapes seront visibles dans les logs.")) return;
  var response = await fetch("/api/jsy/reconfigure-19200", {method:"POST"});
  var text = await response.text();
  var result = {};
  try { result = JSON.parse(text); } catch (e) {}
  await refresh();
  alert(result.ok ? "JSY passe en 19200 bauds." : "Passage en 19200 echoue: " + (result.error || text || "erreur inconnue"));
}

function sensorWizardTypes() {
  var nodes = (cache.espnow && cache.espnow.discoveredNodes || []).length;
  var ds = cache.sensors && cache.sensors.ds18b20 ? cache.sensors.ds18b20.length : 0;
  return [
    {key:"tic", icon:"TIC", title:"TIC Linky", desc:"Compteur officiel, GRID/PAPP/IINST.", status:state.ticAvailable ? "detecte" : "non configure"},
    {key:"jsy", icon:"JSY", title:"JSY-MK-194T", desc:"Deux pinces, puissance rapide, tension et courant.", status:state.jsyOnline ? "detecte" : "non configure"},
    {key:"ds", icon:"TEMP", title:"DS18B20", desc:"Temperature ballon ou ambiance sur bus OneWire.", status:ds ? ds + " sonde(s)" : "a scanner"},
    {key:"espnow", icon:"NOW", title:"Capteur ESP-NOW distant", desc:"Importe un capteur publie par un autre ESP.", status:nodes ? nodes + " noeud(s)" : "en attente"},
    {key:"battery", icon:"BAT", title:"Batterie", desc:"Tension, courant, SOC et puissance batterie.", status:state.batteryOnline ? "detecte" : "optionnel"},
    {key:"solar", icon:"PV", title:"Solaire", desc:"Production PV locale, distante ou calculee.", status:hasValue(state.productionW) ? fmt(state.productionW) + " W" : "optionnel"},
    {key:"generic", icon:"IO", title:"Capteur local generique", desc:"GPIO, analogique, digital ou valeur virtuelle.", status:"manuel"}
  ];
}

function defaultSensorForWizard(type) {
  if (type === "tic") return {kind:"sensor", item:{id:"tic_linky", name:"TIC Linky", type:"TIC Linky", source:"local", role:"compteur_officiel", serial:"Serial1", rx:26, tx:27, mode:"historique", baudrate:1200, timeoutMs:5000, enabled:true}};
  if (type === "jsy") return {kind:"sensor", item:{name:"JSY reseau", type:"JSY-MK-194T", source:"local", serial:"Serial2", rx:26, tx:27, modbusAddress:1, baudrate:19200, readIntervalMs:350, timeoutMs:300, role:"mesure_reseau_principal", enabled:true, channels:defaultJsyChannels()}};
  if (type === "ds") return {kind:"ds", item:{name:"Sonde temperature", role:"", enabled:true, critical:false, unit:"C", espNowExportEnabled:false, espNowExportIntervalMs:10000, espNowSendOnChange:true, espNowMinDelta:0.2, espNowExportPriority:0}};
  if (type === "espnow") return {kind:"sensor", item:{enabled:true, source:"espnow", type:"TIC Linky", name:"Capteur ESP-NOW", role:"compteur_officiel", remoteKey:"ALL"}};
  if (type === "battery") return {kind:"sensor", item:{enabled:true, source:"local", type:"Battery", name:"Batterie", role:"stockage_principal", espNowExportEnabled:false}};
  if (type === "solar") return {kind:"sensor", item:{enabled:true, source:"local", type:"Solar", name:"Production solaire", role:"production", espNowExportEnabled:false}};
  return {kind:"sensor", item:{enabled:true, source:"local", type:"Virtual", name:"Capteur local", role:"custom"}};
}

function newSensor() {
  sensorWizardState = {step:1, type:"", kind:"sensor", item:null};
  renderSensorWizard();
}

function sensorWizardShell(title, body, actions) {
  $("sensorForm").innerHTML = '<div class="sensorWizard"><div class="wizardSteps"><span class="' + (sensorWizardState.step === 1 ? "active" : "") + '">1 Type</span><span class="' + (sensorWizardState.step === 2 ? "active" : "") + '">2 Configuration</span><span class="' + (sensorWizardState.step === 3 ? "active" : "") + '">3 Resume</span></div><h2>' + esc(title) + '</h2>' + body + '<div class="wizardActions">' + actions + '</div></div>';
}

function renderSensorWizard() {
  if (!sensorWizardState) newSensor();
  if (sensorWizardState.step === 1) {
    var cards = sensorWizardTypes().map(function (type) {
      return '<button class="sensorTypeCard ' + (sensorWizardState.type === type.key ? "selected" : "") + '" onclick="selectSensorWizardType(\'' + esc(type.key) + '\')"><b>' + esc(type.icon) + '</b><strong>' + esc(type.title) + '</strong><span>' + esc(type.desc) + '</span><small>' + esc(type.status) + '</small></button>';
    }).join("");
    return sensorWizardShell("Ajouter un capteur", '<div class="sensorTypeGrid">' + cards + '</div>', '<button onclick="cancelSensorWizard()">Annuler</button>');
  }
  if (sensorWizardState.step === 2) {
    var form = sensorFormHtml(sensorWizardState.kind, -1, sensorWizardState.item || {}).replace(/<p class="formActions">[\s\S]*?<\/p>$/, "");
    $("sensorForm").innerHTML = '<div class="sensorWizard"><div class="wizardSteps"><span>1 Type</span><span class="active">2 Configuration</span><span>3 Resume</span></div>' + form + '<div class="wizardActions"><button onclick="sensorWizardBack()">Retour</button><button onclick="sensorWizardToSummary()">Continuer</button><button onclick="cancelSensorWizard()">Annuler</button></div></div>';
    return;
  }
  renderSensorWizardSummary();
}

function selectSensorWizardType(type) {
  var preset = defaultSensorForWizard(type);
  sensorWizardState = {step:2, type:type, kind:preset.kind, item:preset.item};
  renderSensorWizard();
}

function sensorWizardBack() {
  sensorWizardState.step = Math.max(1, sensorWizardState.step - 1);
  renderSensorWizard();
}

function sensorWizardToSummary() {
  var collected = collectSensorFormItem(-1);
  sensorWizardState.kind = collected.kind;
  sensorWizardState.item = collected.item;
  sensorWizardState.step = 3;
  renderSensorWizard();
}

function renderSensorWizardSummary() {
  var item = sensorWizardState.item || {};
  var source = sensorWizardState.kind === "ds" ? "local / OneWire" : (item.source === "espnow" ? "ESP-NOW" : "local");
  var iface = sensorWizardState.kind === "ds" ? ("OneWire " + (item.address || "adresse a choisir")) : (item.source === "espnow" ? ((item.mac || "MAC a choisir") + (item.remoteSensorId != null ? " / sensorId " + item.remoteSensorId : "")) : ("GPIO " + (item.gpio == null ? "-" : item.gpio) + " RX " + (item.rx == null ? "-" : item.rx) + " TX " + (item.tx == null ? "-" : item.tx)));
  var badges = [source === "ESP-NOW" ? "ESP-NOW" : "LOCAL"];
  if (item.espNowExportEnabled) badges.push("EXPORT ESP-NOW");
  if (item.critical) badges.push("SECURITE");
  if (isSolarRouterReferenceSensor(item)) badges.push("REFERENCE ROUTEUR");
  var body = '<div class="sensorSummary"><div><span>Nom</span><b>' + esc(item.name || item.id || "Capteur") + '</b></div><div><span>Type</span><b>' + esc(sensorWizardState.kind === "ds" ? "DS18B20" : item.type || "") + '</b></div><div><span>Source</span><b>' + esc(source) + '</b></div><div><span>Role</span><b>' + esc(item.role || "-") + '</b></div><div><span>Interface</span><b>' + esc(iface) + '</b></div><div><span>Options</span><b>' + badges.map(function (b) { return '<span class="badge info">' + esc(b) + '</span>'; }).join(" ") + '</b></div></div>';
  sensorWizardShell("Resume avant creation", body, '<button onclick="sensorWizardBack()">Retour</button><button onclick="createSensorFromWizard()">Creer le capteur</button><button onclick="cancelSensorWizard()">Annuler</button>');
}

function createSensorFromWizard() {
  if (sensorWizardState.kind === "ds") {
    cache.sensors.ds18b20 = cache.sensors.ds18b20 || [];
    cache.sensors.ds18b20.push(sensorWizardState.item);
  } else {
    cache.sensors.sensors = cache.sensors.sensors || [];
    cache.sensors.sensors.push(sensorWizardState.item);
  }
  sensorWizardState = null;
  markDirty("sensors");
  drawSensorsPage();
}

function cancelSensorWizard() {
  sensorWizardState = null;
  $("sensorForm").innerHTML = "";
}

function cancelSensorEdit() {
  $("sensorForm").innerHTML = "";
}

function newEspNowSensor() { selectSensorWizardType("espnow"); }
function newJsySensor() { selectSensorWizardType("jsy"); }
function editSensor(index) { $("sensorForm").innerHTML = sensorFormHtml("sensor", index, (cache.sensors.sensors || [])[index]); }
function newDsSensor() { selectSensorWizardType("ds"); }
function editDsSensor(index) { $("sensorForm").innerHTML = sensorFormHtml("ds", index, (cache.sensors.ds18b20 || [])[index]); }

function collectSensorFormItem(index) {
  var kind = $("sensorKind").value;
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
    item.espNowExportEnabled = boolField("espNowExportEnabled");
    item.espNowExportIntervalMs = readNumber("espNowExportInterval", 10000);
    item.espNowSendOnChange = boolField("espNowSendOnChange");
    item.espNowMinDelta = readNumber("espNowMinDelta", 0.2);
    item.espNowExportPriority = readNumber("espNowExportPriority", 0);
    return {kind:kind, item:item};
  }
  item.type = type;
  item.source = $("sensorSource").value || "local";
  if (item.source !== "espnow") {
    item.gpio = readNumber("sensorGpio", undefined);
    item.rx = readNumber("sensorRx", undefined);
    item.tx = readNumber("sensorTx", undefined);
  }
  item.mac = $("sensorMac").value;
  if (item.source === "espnow") {
    item.remoteKey = $("sensorRemoteKey") ? $("sensorRemoteKey").value : "ALL";
    item.key = item.remoteKey;
    item.remoteNode = $("sensorRemoteNode") ? $("sensorRemoteNode").value : "";
    item.remoteSensorId = readNumber("sensorRemoteSensorId", undefined);
    item.debug = boolField("espNowDebug");
  } else {
    item.espNowExportEnabled = boolField("espNowExportEnabled");
    item.espNowExportIntervalMs = readNumber("espNowExportInterval", item.type === "JSY-MK-194T" ? 200 : 1000);
    item.espNowSendOnChange = boolField("espNowSendOnChange");
    item.espNowMinDelta = readNumber("espNowMinDelta", item.type === "JSY-MK-194T" ? 5 : 10);
    item.espNowExportPriority = readNumber("espNowExportPriority", item.type === "JSY-MK-194T" ? 3 : 1);
  }
  if (item.source !== "espnow" && (item.type === "JSY-MK-194T" || item.id === "jsy_grid")) {
    item.modbusAddress = readNumber("jsyAddress", 1);
    item.baudrate = readNumber("jsyBaudrate", 19200);
    item.readIntervalMs = readNumber("jsyReadInterval", 350);
    item.timeoutMs = readNumber("jsyTimeout", 300);
    item.channels = [
      {id:"clamp1", name:$("jsyCh1Name") ? $("jsyCh1Name").value : "Pince 1", role:$("jsyCh1Role") ? $("jsyCh1Role").value : "production", measures:["current", "power", "pf", "energyPos", "energyNeg", "direction"]},
      {id:"clamp2", name:$("jsyCh2Name") ? $("jsyCh2Name").value : "Pince 2", role:$("jsyCh2Role") ? $("jsyCh2Role").value : "grid", measures:["current", "power", "pf", "energyPos", "energyNeg", "direction"]}
    ];
  } else if (item.source !== "espnow" && (item.type === "TIC Linky" || item.id === "tic_linky")) {
    item.mode = $("ticMode") ? $("ticMode").value : "historique";
    item.baudrate = readNumber("ticBaudrate", item.mode === "standard" ? 9600 : 1200);
    item.timeoutMs = readNumber("ticTimeout", 5000);
    item.debug = boolField("ticDebug");
  }
  return {kind:kind, item:item};
}

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
    item.espNowExportEnabled = boolField("espNowExportEnabled");
    item.espNowExportIntervalMs = readNumber("espNowExportInterval", 10000);
    item.espNowSendOnChange = boolField("espNowSendOnChange");
    item.espNowMinDelta = readNumber("espNowMinDelta", 0.2);
    item.espNowExportPriority = readNumber("espNowExportPriority", 0);
    cache.sensors.ds18b20 = cache.sensors.ds18b20 || [];
    if (index >= 0) cache.sensors.ds18b20[index] = item; else cache.sensors.ds18b20.push(item);
  } else {
    item.type = type;
    item.source = $("sensorSource").value || "local";
    if (item.source !== "espnow") {
      item.gpio = readNumber("sensorGpio", undefined);
      item.rx = readNumber("sensorRx", undefined);
      item.tx = readNumber("sensorTx", undefined);
    }
    item.mac = $("sensorMac").value;
    if (item.source === "espnow") {
      item.remoteKey = $("sensorRemoteKey") ? $("sensorRemoteKey").value : "ALL";
      item.key = item.remoteKey;
      item.remoteNode = $("sensorRemoteNode") ? $("sensorRemoteNode").value : "";
      item.remoteSensorId = readNumber("sensorRemoteSensorId", undefined);
      item.debug = boolField("espNowDebug");
    } else {
      item.espNowExportEnabled = boolField("espNowExportEnabled");
      item.espNowExportIntervalMs = readNumber("espNowExportInterval", item.type === "JSY-MK-194T" ? 200 : 1000);
      item.espNowSendOnChange = boolField("espNowSendOnChange");
      item.espNowMinDelta = readNumber("espNowMinDelta", item.type === "JSY-MK-194T" ? 5 : 10);
      item.espNowExportPriority = readNumber("espNowExportPriority", item.type === "JSY-MK-194T" ? 3 : 1);
    }
    if (item.source !== "espnow" && (item.type === "JSY-MK-194T" || item.id === "jsy_grid")) {
      item.modbusAddress = readNumber("jsyAddress", 1);
      item.baudrate = readNumber("jsyBaudrate", 19200);
      item.readIntervalMs = readNumber("jsyReadInterval", 350);
      item.timeoutMs = readNumber("jsyTimeout", 300);
      item.channels = [
        {id:"clamp1", name:$("jsyCh1Name") ? $("jsyCh1Name").value : "Pince 1", role:$("jsyCh1Role") ? $("jsyCh1Role").value : "production", measures:["current", "power", "pf", "energyPos", "energyNeg", "direction"]},
        {id:"clamp2", name:$("jsyCh2Name") ? $("jsyCh2Name").value : "Pince 2", role:$("jsyCh2Role") ? $("jsyCh2Role").value : "grid", measures:["current", "power", "pf", "energyPos", "energyNeg", "direction"]}
      ];
    } else if (item.source !== "espnow" && (item.type === "TIC Linky" || item.id === "tic_linky")) {
      item.mode = $("ticMode") ? $("ticMode").value : "historique";
      item.baudrate = readNumber("ticBaudrate", item.mode === "standard" ? 9600 : 1200);
      item.timeoutMs = readNumber("ticTimeout", 5000);
      item.debug = boolField("ticDebug");
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
  if ((item.source || "") === "espnow") {
    var mac = item.mac || "";
    return '<span class="badge info">ESP-NOW</span>' +
      (mac ? '<br><small>' + esc(mac) + '</small>' : '') +
      (item.remoteSensorId != null ? '<br><small>sensorId ' + esc(item.remoteSensorId) + '</small>' : '');
  }
  var parts = [];
  if (item.gpio != null) parts.push("GPIO " + item.gpio);
  if (item.rx != null) parts.push("RX " + item.rx);
  if (item.tx != null) parts.push("TX " + item.tx);
  return esc(parts.join(" "));
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

function actuatorConfigIndex(id) {
  var arr = (cache.actuators && cache.actuators.actuators) || [];
  for (var i = 0; i < arr.length; i++) if (String(arr[i].id || "") === String(id || "")) return i;
  return -1;
}

function actuatorConfigById(id) {
  var index = actuatorConfigIndex(id);
  return index >= 0 ? cache.actuators.actuators[index] : null;
}

function actuatorRuntimeById(id) {
  return getActuatorById(id);
}

function mergedActuatorsRuntime() {
  return getActuators();
}

function actuatorRuntimeStatus(a) {
  if (!a) return {label:"Inconnu", cls:"muted", detail:"aucune donnee"};
  if (a.safetyLocked) return {label:"Securite", cls:"bad", detail:a.lastError || "bloque"};
  if (a.enabled === false) return {label:"Desactive", cls:"muted", detail:"configuration off"};
  if (a.timedOut) return {label:"Perdu", cls:"bad", detail:a.lastError || "timeout"};
  if (a.ok) return {label:"OK", cls:"ok", detail:hasValue(a.ageMs) ? "age " + fmt(a.ageMs) + " ms" : "pret"};
  return {label:"Degrade", cls:"warn", detail:a.lastError || "etat incomplet"};
}

function actuatorAckText(runtime) {
  if (!runtime || runtime.origin !== "ESP-NOW") return "LOCAL";
  if (runtime.commandAcked === true) return "ACK";
  return runtime.ackStatus || "NOT_SUPPORTED";
}

function actuatorCountByOrigin(list, origin) {
  return (list || []).filter(function (item) { return item.origin === origin; }).length;
}

function actuatorCountOk(list) {
  return (list || []).filter(function (item) { return item.ok; }).length;
}

function actuatorValueText(value, unit) {
  if (!hasValue(value)) return "--";
  return fmt(Number(value)) + (unit ? " " + unit : "");
}

function actuatorPercentBar(commandPercent, outputPercent) {
  var command = hasValue(commandPercent) ? Math.max(0, Math.min(100, Number(commandPercent))) : 0;
  var output = hasValue(outputPercent) ? Math.max(0, Math.min(100, Number(outputPercent))) : command;
  return '<div class="actuatorPercent"><i style="width:' + esc(output) + '%"></i><span>' + esc(actuatorValueText(command, "%")) + '</span></div><small>sortie ' + esc(actuatorValueText(output, "%")) + '</small>';
}

async function refreshActuatorRuntime(redraw) {
  try {
    cache.actuatorRuntime = await api("/api/actuators/runtime");
  } catch (e) {
    cache.actuatorRuntime = {actuators:[]};
  }
  if (redraw !== false && page === "actuators") drawActuatorsPage();
}

function showActuatorDetails(id) {
  var runtime = actuatorRuntimeById(id) || {};
  var configItem = actuatorConfigById(id) || {};
  var status = actuatorRuntimeStatus(runtime);
  var configHtml = esc(JSON.stringify(configItem, null, 2));
  var runtimeHtml = esc(JSON.stringify(runtime, null, 2));
  var html = '<div class="actuatorModalBackdrop" onclick="closeActuatorDetails(event)"><section class="actuatorModal" onclick="event.stopPropagation()">' +
    '<div class="wideChartHead"><b>' + esc(runtime.name || configItem.name || id) + '</b><button onclick="closeActuatorDetails()">Fermer</button></div>' +
    '<div class="actuatorDetailGrid">' +
      detailCell("Etat", status.label, status.cls, status.detail) +
      detailCell("Origine", runtime.origin || (String(configItem.source || "local").toLowerCase() === "espnow" ? "ESP-NOW" : "LOCAL"), "info", runtime.remoteMac || pinText(configItem)) +
      detailCell("Type", runtime.type || configItem.type || "--", "muted", runtime.electricalMode || configItem.mode || "--") +
      detailCell("Commande", actuatorValueText(runtime.commandPercent, "%"), "heat", "sortie " + actuatorValueText(runtime.outputPercent, "%")) +
      detailCell("Puissance", actuatorValueText(runtime.estimatedPowerW, "W"), "heat", "max " + actuatorValueText(runtime.maxPowerW || configItem.maxPowerW, "W")) +
      detailCell("Securite", runtime.safetyLocked ? "bloque" : "libre", runtime.safetyLocked ? "bad" : "ok", runtime.critical ? "critique" : "non critique") +
      detailCell("Dernier ordre", hasValue(runtime.lastCommandMs) ? runtime.lastCommandMs + " ms" : "--", "muted", "age " + actuatorValueText(runtime.ageMs, "ms")) +
      detailCell("ACK", actuatorAckText(runtime), runtime.commandAcked ? "ok" : "muted", hasValue(runtime.lastAckMs) ? runtime.lastAckMs + " ms" : (runtime.origin === "ESP-NOW" ? "non confirme" : "local")) +
      detailCell("Export ESP-NOW", configItem.espNowExportEnabled ? "actif" : "inactif", configItem.espNowExportEnabled ? "info" : "muted", configItem.espNowExportName || configItem.espNowExportRole || "--") +
    '</div><h3>Configuration</h3><pre>' + configHtml + '</pre><h3>Runtime</h3><pre>' + runtimeHtml + '</pre></section></div>';
  $("actuatorDetailHost").innerHTML = html;
}

function detailCell(label, value, cls, detail) {
  return '<div class="detailCell ' + esc(cls || "muted") + '"><span>' + esc(label) + '</span><b>' + esc(value) + '</b><small>' + esc(detail || "") + '</small></div>';
}

function closeActuatorDetails(event) {
  if (event && event.target && !event.target.classList.contains("actuatorModalBackdrop")) return;
  var host = $("actuatorDetailHost");
  if (host) host.innerHTML = "";
}

async function toggleActuatorDebug(index) {
  var actuator = (cache.actuators.actuators || [])[index];
  if (!actuator) return;
  actuator.debug = !actuator.debug;
  markDirty("actuators");
  drawActuatorsPage();
}

async function actuatorsPage() {
  cache.actuators = await api("/api/actuators");
  try {
    cache.espnow = await api("/api/espnow");
  } catch (e) {}
  try {
    cache.actuatorRuntime = await api("/api/actuators/runtime");
  } catch (e) {
    cache.actuatorRuntime = {actuators:[]};
  }
  drawActuatorsPage();
}

function drawActuatorsPage() {
  var actuators = mergedActuatorsRuntime();
  var remoteActuators = remoteDiscoveredActuators();
  var rows = actuators.map(function (a) {
    var index = actuatorConfigIndex(a.id);
    var status = actuatorRuntimeStatus(a);
    var origin = actuatorOrigin(a);
    var originCls = origin === "ESP-NOW" ? "info" : "muted";
    var remoteInfo = origin === "ESP-NOW" ? (a.remoteMac || a.remoteNodeId || "--") : pinText({gpio:a.gpio, rx:a.zeroCross, tx:a.control});
    var exportBadge = a.espNowExportEnabled ? '<br><span class="badge info">EXPORT ESP-NOW</span>' : '';
    return '<tr class="actuatorRow ' + esc(status.cls) + '">' +
      '<td><b>' + esc(a.name || a.id) + '</b><br><small>' + esc(a.id || "") + '</small></td>' +
      '<td><span class="badge ' + esc(originCls) + '">' + esc(origin) + '</span>' + exportBadge + '<br><small>' + esc(a.type || "SSR") + '</small></td>' +
      '<td><span class="badge ' + esc(status.cls) + '">' + esc(status.label) + '</span><br><small>' + esc(status.detail) + '</small></td>' +
      '<td>' + esc(a.mode || "--") + '<br><small>' + esc(a.electricalMode || "--") + '</small></td>' +
      '<td>' + actuatorPercentBar(a.commandPercent, a.outputPercent) + '</td>' +
      '<td>' + esc(actuatorValueText(a.estimatedPowerW, "W")) + '<br><small>max ' + esc(actuatorValueText(a.maxPowerW, "W")) + '</small></td>' +
      '<td>' + esc(a.source || "--") + '<br><small>' + esc(remoteInfo) + '</small></td>' +
      '<td class="actions">' +
        '<button onclick="showActuatorDetails(\'' + esc(a.id) + '\')">Details</button>' +
        (index >= 0 ? '<button onclick="editActuator(' + index + ')">Modifier</button>' : '') +
        (index >= 0 ? '<button onclick="toggleActuator(' + index + ')">' + esc(a.enabled === false ? "Activer" : "Desactiver") + '</button>' : '') +
        '<button onclick="testActuator(\'' + esc(a.id) + '\',25)">Test 25%</button>' +
        '<button onclick="forceOff(\'' + esc(a.id) + '\')">OFF</button>' +
        (index >= 0 ? '<button onclick="toggleActuatorDebug(' + index + ')">Debug ' + esc(a.debug ? "OFF" : "ON") + '</button>' : '') +
        (index >= 0 ? '<button class="danger" onclick="deleteActuator(' + index + ')">Supprimer</button>' : '') +
      '</td></tr>';
  }).join("");
  $("app").innerHTML = banner() + '<h1>Actionneurs</h1>' + helpBox("actuators") + dirtyNotice("actuators") +
    '<div class="toolbar"><button onclick="newActuator()">Ajouter actionneur</button><button onclick="refreshActuatorRuntime()">Rafraichir runtime</button><button onclick="refreshEspNowActuators()">Rafraichir ESP-NOW</button><button onclick="saveActuators()">Sauvegarder</button><button onclick="jsonEditor(\'actuators\')">JSON avance</button></div>' +
    '<section class="panel actuatorSummary"><b>' + actuators.length + ' actionneur(s)</b><span>' + actuatorCountByOrigin(actuators, "LOCAL") + ' LOCAL</span><span>' + actuatorCountByOrigin(actuators, "ESP-NOW") + ' ESP-NOW</span><span>' + remoteActuators.length + ' dispo ESP-NOW</span><span>' + actuatorCountOk(actuators) + ' OK</span></section>' +
    remoteAvailableActuatorsPanel() +
    '<section class="panel" id="actuatorForm">Selectionne un actionneur ou ajoute-en un nouveau.</section><div id="actuatorDetailHost"></div>' +
    '<table class="actuatorTable"><tr><th>Nom</th><th>Origine / type</th><th>Etat</th><th>Mode</th><th>Commande</th><th>Puissance</th><th>Source</th><th>Actions</th></tr>' + (rows || '<tr><td colspan="8">Aucun actionneur configure.</td></tr>') + '</table>';
}

function remoteAvailableActuatorsPanel() {
  var actuators = remoteDiscoveredActuators();
  if (!actuators.length) {
    var actuatorNodes = ((cache.espnow && cache.espnow.discoveredNodes) || []).filter(function (node) {
      return Number(node.capabilityFlags || 0) & 0x0040;
    });
    if (!actuatorNodes.length) return "";
    return '<section class="panel remoteSensorsPanel"><h2>Actionneurs ESP-NOW disponibles</h2><p class="muted">Un noeud annonce une capacite actionneur, mais aucun detail ACTUATOR_DISCOVERY exploitable n est encore present dans /api/espnow. Force une annonce ou verifie que le firmware source et recepteur sont au meme niveau.</p></section>';
  }
  var rows = actuators.map(function (actuator, i) {
    var added = isRemoteActuatorConfigured(actuator);
    return '<tr class="' + (added ? 'muted' : '') + '">' +
      '<td><b>' + esc(actuator.actuatorName || actuator.actuatorId || "Actionneur distant") + '</b><br><small>' + esc(actuator.nodeName || "") + '</small></td>' +
      '<td><span class="badge info">ESP-NOW</span><br><small>' + esc(actuator.mac || "") + '</small></td>' +
      '<td>' + esc(remoteActuatorConfigType(actuator)) + '<br><small>' + esc(actuator.electricalMode || "--") + '</small></td>' +
      '<td>' + esc(actuator.actuatorRole || "--") + '</td>' +
      '<td>' + esc(actuator.maxPowerW || 0) + ' W<br><small>' + esc(actuator.commandTimeoutMs || 0) + ' ms</small></td>' +
      '<td>' + (added ? '<span class="badge muted">Deja ajoute</span>' : '<button onclick="addDiscoveredRemoteActuator(' + i + ')">Ajouter</button>') + '</td>' +
    '</tr>';
  }).join("");
  return '<section class="panel remoteSensorsPanel"><div class="wideChartHead"><b>Actionneurs ESP-NOW disponibles</b><span>' + actuators.length + ' recu(s)</span></div><p class="muted">Decouverts automatiquement par ACTUATOR_DISCOVERY. Import manuel uniquement.</p><table><tr><th>Nom</th><th>Origine</th><th>Type / mode</th><th>Role</th><th>Puissance</th><th>Action</th></tr>' + rows + '</table></section>';
}

function remoteDiscoveredActuators() {
  var list = (cache.espnow && cache.espnow.remoteActuators) || [];
  return list.filter(function (actuator) {
    return actuator && actuator.mac && actuator.actuatorId;
  }).sort(function (a, b) {
    var am = String(a.mac || "").toUpperCase();
    var bm = String(b.mac || "").toUpperCase();
    if (am !== bm) return am.localeCompare(bm);
    return String(a.actuatorId || "").localeCompare(String(b.actuatorId || ""));
  });
}

function isRemoteActuatorConfigured(actuator) {
  var configured = (cache.actuators && cache.actuators.actuators) || [];
  return configured.some(function (item) {
    return String(item.source || "").toLowerCase() === "espnow" &&
      String(item.mac || "").toUpperCase() === String(actuator.mac || "").toUpperCase() &&
      String(item.remoteActuatorId || item.remoteId || item.id || "") === String(actuator.actuatorId || "");
  });
}

function remoteActuatorConfigType(actuator) {
  var text = String(actuator.actuatorTypeText || "").toUpperCase();
  if (text === "SSR") return "SSR";
  if (text === "TRIAC") return "RobotDyn Triac";
  if (text === "RELAY") return "Relay";
  if (text === "PWM") return "PWM";
  return "REMOTE";
}

function remoteActuatorGeneratedId(actuator) {
  var mac = String(actuator.mac || "remote").replace(/[^A-Za-z0-9]/g, "").slice(-6).toLowerCase();
  var id = String(actuator.actuatorId || actuator.actuatorName || "actuator").replace(/[^A-Za-z0-9_]/g, "_").toLowerCase();
  return uniqueActuatorId("espnow_" + mac + "_" + id, -1);
}

async function addDiscoveredRemoteActuator(index) {
  var actuator = remoteDiscoveredActuators()[index];
  if (!actuator || isRemoteActuatorConfigured(actuator)) return;
  cache.actuators = cache.actuators || await api("/api/actuators");
  cache.actuators.actuators = cache.actuators.actuators || [];
  var type = remoteActuatorConfigType(actuator);
  var modes = actuatorModesForType(type);
  var mode = actuator.electricalMode && modes.indexOf(actuator.electricalMode) >= 0 ? actuator.electricalMode : modes[0];
  cache.actuators.actuators.push({
    id: remoteActuatorGeneratedId(actuator),
    name: actuator.actuatorName || actuator.actuatorId || "Actionneur ESP-NOW",
    type:type,
    mode:mode,
    source:"espnow",
    origin:"ESP-NOW",
    mac: actuator.mac || "",
    remoteNodeId: actuator.nodeId == null ? "" : String(actuator.nodeId),
    remoteNodeName: actuator.nodeName || "",
    remoteActuatorId: actuator.actuatorId || "",
    usage: actuator.actuatorRole || "",
    role: actuator.actuatorRole || "",
    maxPowerW: Number(actuator.maxPowerW || 0),
    ttlMs: Number(actuator.commandTimeoutMs || 1000),
    espNowCommandTimeoutMs: Number(actuator.commandTimeoutMs || 1000),
    enabled:true,
    critical: actuator.critical === true,
    debug:false,
    espNowDebug:false
  });
  var response = await postJson("/api/actuators", cache.actuators);
  if (!response.ok) return alert("Ajout actionneur ESP-NOW refuse: " + await response.text());
  cache.actuators = await api("/api/actuators");
  cache.espnow = await api("/api/espnow");
  await refreshActuatorRuntime(false);
  if (page === "actuators") drawActuatorsPage();
  else if (page === "espnow") await espNowPage();
}

async function refreshEspNowActuators() {
  cache.espnow = await api("/api/espnow");
  if (page === "actuators") drawActuatorsPage();
  else if (page === "espnow") await espNowPage();
}

function actuatorFormHtml(index, a) {
  a = a || {};
  var type = a.type || "SSR";
  var modes = actuatorModesForType(type);
  var mode = modes.indexOf(a.mode || "") >= 0 ? a.mode : modes[0];
  var source = String(a.source || "local").toLowerCase();
  return '<h2>' + (index >= 0 ? "Modifier" : "Ajouter") + ' actionneur</h2><input id="actuatorIndex" type="hidden" value="' + index + '">' +
    '<div class="form">' +
    textField("actuatorName", "Nom", a.name || "") +
    '<label>Origine<select id="actuatorSource">' + options(["local", "espnow"], source) + '</select></label>' +
    '<label>Type<select id="actuatorType" onchange="updateActuatorModeOptions()">' + options(actuatorTypes, type) + '</select></label>' +
    '<label>Mode<select id="actuatorMode" onchange="updateActuatorModeHelp()">' + options(modes, mode) + '</select><span id="actuatorModeHelp" class="fieldHelp">' + esc(actuatorModeHelp[mode] || "") + '</span></label>' +
    field("actuatorGpio", "GPIO sortie", a.gpio == null ? "" : a.gpio) +
    field("actuatorControl", "GPIO controle triac", a.control == null ? "" : a.control) +
    field("actuatorZeroCross", "GPIO zero-cross", a.zeroCross == null ? "" : a.zeroCross) +
    textField("actuatorMac", "MAC ESP-NOW", a.mac || "") +
    textField("actuatorRemoteNode", "Node distant", a.remoteNodeId || a.nodeId || "") +
    selectField("actuatorActiveHigh", "Commande active HIGH", a.activeHigh !== false) +
    field("actuatorMaxPower", "Puissance max W", a.maxPowerW == null ? "" : a.maxPowerW) +
    field("actuatorCycle", "Cycle ms", a.cycleMs == null ? "" : a.cycleMs) +
    '<label>Usage<select id="actuatorUsage">' + actuatorUsageSelect(actuatorUsage(a)) + '</select><span class="fieldHelp">Aucun usage = masque dans les blocs du dashboard.</span></label>' +
    actuatorEspNowExportPanel(a, source) +
    '<label><input id="actuatorEnabled" class="check" type="checkbox" ' + checked(a.enabled) + '> Actif</label>' +
    '<label><input id="actuatorCritical" class="check" type="checkbox" ' + checked(a.critical) + '> Critique</label>' +
    '<label><input id="actuatorDebug" class="check" type="checkbox" ' + checked(a.debug) + '> Debug cible</label>' +
    '<details class="advancedField"><summary>Avance</summary>' + textField("actuatorId", "ID technique", a.id || "") + '<p class="muted">Laisse vide pour creer automatiquement un ID depuis le nom.</p></details>' +
    '</div><p><button onclick="applyActuatorForm()">Appliquer</button></p>';
}

function actuatorEspNowExportPanel(a, source) {
  var timeout = a.espNowCommandTimeoutMs || a.ttlMs || 1000;
  if (source === "espnow") {
    return '<div class="subPanel"><h3>Actionneur ESP-NOW importe</h3><div class="formGrid">' +
      textField("actuatorRemoteInfoMac", "MAC distante", a.mac || a.remoteMac || "") +
      textField("actuatorRemoteInfoNode", "Node distant", a.remoteNodeId || a.nodeId || "") +
      field("actuatorEspNowCommandTimeout", "Timeout commande ms", timeout) +
      selectField("actuatorEspNowDebug", "Debug ESP-NOW", a.espNowDebug === true || a.debug === true) +
      '</div><p class="muted">Cet actionneur est commande a distance. L import reste manuel; ce panneau ne declenche aucun import automatique.</p></div>';
  }
  return '<div class="subPanel"><h3>Export ESP-NOW</h3><div class="formGrid">' +
    selectField("actuatorEspNowExportEnabled", "Exporter cet actionneur", a.espNowExportEnabled === true) +
    textField("actuatorEspNowExportName", "Nom exporte", a.espNowExportName || a.name || a.id || "") +
    textField("actuatorEspNowExportRole", "Role exporte", a.espNowExportRole || a.role || a.usage || "actuator") +
    field("actuatorEspNowExportInterval", "Annonce toutes les ms", a.espNowExportIntervalMs || 30000) +
    field("actuatorEspNowCommandTimeout", "Timeout commande ms", timeout) +
    selectField("actuatorEspNowDebug", "Debug ESP-NOW", a.espNowDebug === true || a.debug === true) +
    '</div><p class="muted">Rend cet actionneur visible dans le discovery ESP-NOW pour import manuel sur un autre ESP. Aucune importation automatique n est activee.</p></div>';
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

function newActuator() { $("actuatorForm").innerHTML = actuatorFormHtml(-1, {type:"SSR", mode:"BURST_FIRE", cycleMs:1000, enabled:true, critical:true, usage:"", source:"local"}); }
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
    control: readNumber("actuatorControl", undefined),
    zeroCross: readNumber("actuatorZeroCross", undefined),
    activeHigh: boolField("actuatorActiveHigh"),
    maxPowerW: readNumber("actuatorMaxPower", undefined),
    cycleMs: readNumber("actuatorCycle", undefined),
    source: $("actuatorSource").value || "local",
    mac: $("actuatorMac").value || "",
    remoteNodeId: $("actuatorRemoteNode").value || "",
    usage: $("actuatorUsage").value,
    heater: $("actuatorUsage").value === "water_heater",
    espNowExportEnabled: boolField("actuatorEspNowExportEnabled"),
    espNowExportName: $("actuatorEspNowExportName") ? $("actuatorEspNowExportName").value : "",
    espNowExportRole: $("actuatorEspNowExportRole") ? $("actuatorEspNowExportRole").value : "",
    espNowExportIntervalMs: readNumber("actuatorEspNowExportInterval", 30000),
    espNowCommandTimeoutMs: readNumber("actuatorEspNowCommandTimeout", 1000),
    ttlMs: readNumber("actuatorEspNowCommandTimeout", 1000),
    espNowDebug: boolField("actuatorEspNowDebug"),
    enabled: $("actuatorEnabled").checked,
    critical: $("actuatorCritical").checked,
    debug: $("actuatorDebug").checked || boolField("actuatorEspNowDebug")
  };
  cache.actuators.actuators = cache.actuators.actuators || [];
  if (index >= 0) cache.actuators.actuators[index] = item; else cache.actuators.actuators.push(item);
  markDirty("actuators");
  drawActuatorsPage();
}

async function toggleActuator(index) {
  var actuator = cache.actuators.actuators[index];
  var willEnable = actuator.enabled === false;
  actuator.enabled = willEnable;
  if (!willEnable && actuator.id) {
    await fetch("/api/actuator/command", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:new URLSearchParams({id:actuator.id, command:"stop", value:"0"})});
    actuator.commandPercent = 0;
    actuator.outputPercent = 0;
    actuator.outputOn = false;
    state.heaterPowerW = 0;
  }
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
  if (response.ok) await refreshActuatorRuntime(false);
  drawActuatorsPage();
}

function commandFor(id) {
  var actuator = getActuatorById(id);
  return actuator ? fmt(getActuatorCommandPercent(actuator)) + " %" : "-";
}

async function forceOff(id) {
  await fetch("/api/actuator/command", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:new URLSearchParams({id:id, command:"stop", value:"0"})});
  await refreshActuatorRuntime();
}

async function testActuator(id, value) {
  var runtime = actuatorRuntimeById(id);
  if (runtime && (runtime.safetyLocked || runtime.timedOut || runtime.enabled === false || runtime.ok === false)) {
    if (!confirm("Actionneur " + id + " non OK (" + actuatorRuntimeStatus(runtime).label + "). Envoyer quand meme un test ?")) return;
  }
  if (Number(value) >= 100 && !confirm("Test 100% sur " + id + " ? Verifie que la charge est branchee en securite.")) return;
  await fetch("/api/actuator/command", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:new URLSearchParams({id:id, command:"setActuatorPercent", value:String(value)})});
  await refreshActuatorRuntime();
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
  $("app").innerHTML = banner() + '<h1>Automate</h1>' + helpBox("logic") + dirtyNotice("rules") + '<div class="toolbar"><button onclick="newRule()">Ajouter regle</button><button onclick="saveRules()">Sauvegarder</button><button onclick="validateRules()">Valider</button><button onclick="jsonEditor(\'rules\')">JSON avance</button></div><div id="validation"></div><section class="panel" id="ruleForm">Selectionne une regle ou ajoute-en une nouvelle.</section><table><tr><th>Etat</th><th>Regle</th><th>Priorite</th><th>Logique</th><th>Conditions</th><th>Actions</th><th>Commandes</th></tr>' + rows + '</table>';
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
  if (command === "setPowerFromSurplus") return '<label>Regulation<select data-a="regulation">' + options(["PID", "PROPORTIONAL"], a.regulation || "PID") + '</select></label><input data-a="maxHeaterPowerW" type="number" value="' + esc(a.maxHeaterPowerW || 1500) + '" placeholder="max W"><input data-a="value" type="hidden" value="0"><span class="muted">PID par defaut, proportionnel en secours</span>';
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
  clearSimulationHistory();
  await refresh();
  if (!state.simulationMode) {
    zeroDashboardPowerOutputs();
    render();
  }
}

function clearSimulationHistory() {
  resetDashboardGraphBuffers();
}

function zeroDashboardPowerOutputs() {
  state.productionW = 0;
  state.surplusW = 0;
  state.consumptionW = 0;
  state.heaterPowerW = 0;
  state.pidOutputPercent = 0;
  state.commandPercent = 0;
  state.ssr1PowerPct = 0;
  state.ssr2PowerPct = 0;
  state.robotDynPowerPct = 0;
  state.energy = state.energy || {};
  state.energy.productionW = 0;
  state.energy.surplusW = 0;
  state.energy.consumptionW = null;
  state.energy.heaterPowerW = 0;
  state.energy.heaterPercent = 0;
  state.pid = state.pid || {};
  state.pid.heaterPowerW = 0;
  state.pid.outputPercent = 0;
  state.pid.commandPercent = 0;
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
      field("devNodeId", "Node ID ESP-NOW", d.nodeId == null ? 0 : d.nodeId) +
      '<label>Role<select id="devRole">' + options(["MASTER","BACKUP","NODE_SENSOR","NODE_ACTUATOR","NODE_MIXED"], d.role || "MASTER") + '</select></label>' +
      selectField("devConfigured", "Configuration terminee", d.isConfigured !== false) +
      '<p class="muted">0 = auto depuis MAC. Utilise 1 a 254 pour imposer un identifiant unique.</p>' +
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
        field("maxRamp", "Rampe max %/s", r.maxOutputRampPercentPerSecond || 5) +
      '</div></div>' +
      '<div class="subPanel pidPanel"><h3>Regulation PID</h3><div class="pidControls">' +
        selectField("pidEnabled", "PID actif", r.pidEnabled !== false) +
        sliderField("pidKp", "Kp", r.kp || r.pidKp || 0.02, 0, 1, 0.01) +
        sliderField("pidKi", "Ki", r.ki || r.pidKi || 0.002, 0, 0.2, 0.001) +
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
  d.nodeId = readNumber("devNodeId", 0);
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

function espNowRoleText(flags) {
  flags = Number(flags) || 0;
  var out = [];
  if (flags & 0x01) out.push("source");
  if (flags & 0x02) out.push("destination");
  if (flags & 0x04) out.push("routeur");
  if (flags & 0x08) out.push("actionneur");
  return out.length ? out.join(", ") : "non declare";
}

function espNowFunctionalRoleText(node) {
  if (node && node.functionalRole) return node.functionalRole;
  var flags = Number(node && node.roleFlags) || 0;
  var sensor = !!(flags & 0x01);
  var actuator = !!(flags & 0x08);
  var router = !!(flags & 0x04);
  if ((sensor && actuator) || (router && (sensor || actuator))) return "MIXED";
  if (router) return "MASTER";
  if (actuator) return "ACTUATOR";
  if (sensor) return "SENSOR";
  return "UNKNOWN";
}

function espNowRedundancyRoleText(node) {
  if (node && node.redundancyRole) return node.redundancyRole;
  return "STANDALONE";
}

function espNowCapabilityText(flags) {
  flags = Number(flags) || 0;
  var out = [];
  if (flags & 0x0001) out.push("Linky");
  if (flags & 0x0002) out.push("JSY");
  if (flags & 0x0004) out.push("Temperature");
  if (flags & 0x0008) out.push("Batterie");
  if (flags & 0x0010) out.push("Solaire");
  if (flags & 0x0020) out.push("Routeur");
  if (flags & 0x0040) out.push("Actionneur");
  return out.length ? out.join(", ") : "aucune";
}

function espNowCapabilityCount(flags, mask) {
  return (Number(flags) & mask) ? 1 : 0;
}

function espNowAgeText(ms) {
  ms = Number(ms);
  if (!isFinite(ms) || ms === 4294967295) return "-";
  if (ms < 1000) return ms + " ms";
  return Math.round(ms / 1000) + " s";
}

function espNowRadioQuality(rssi) {
  var n = Number(rssi);
  if (!isFinite(n) || n === 0) return {text:"-", cls:"muted"};
  if (n >= -65) return {text:n + " dBm / bon", cls:"ok"};
  if (n >= -80) return {text:n + " dBm / moyen", cls:"warn"};
  return {text:n + " dBm / faible", cls:"bad"};
}

function espNowNodeSensors(info, node) {
  var mac = String((node && node.mac) || "").toUpperCase();
  return ((info && info.remoteSensors) || []).filter(function (sensor) {
    return String(sensor.mac || "").toUpperCase() === mac && Number(sensor.sensorId || 0) !== 0;
  }).sort(function (a, b) {
    return Number(a.sensorId || 0) - Number(b.sensorId || 0);
  });
}

function espNowImportedSensorCount(sensors) {
  return (sensors || []).filter(isRemoteSensorConfigured).length;
}

function espNowNodeActuators(info, node) {
  var mac = String((node && node.mac) || "").toUpperCase();
  return ((info && info.remoteActuators) || []).filter(function (actuator) {
    return String(actuator.mac || "").toUpperCase() === mac;
  }).sort(function (a, b) {
    return String(a.actuatorId || "").localeCompare(String(b.actuatorId || ""));
  });
}

function espNowNodeHealth(node) {
  var ok = node && node.ok !== false && node.lost !== true;
  var cls = ok ? "ok" : "bad";
  var label = ok ? "OK" : "Perdu";
  var frameAge = espNowAgeText(node && (node.lastFrameAgeMs != null ? node.lastFrameAgeMs : node.ageMs));
  return '<span class="badge ' + cls + '">' + label + '</span><small>trame ' + esc(frameAge) + '</small>';
}

function espNowNodeRows(nodes, info) {
  if (!nodes || !nodes.length) {
    return '<tr><td colspan="11">Aucun ESP-NOW detecte pour le moment. Lance au moins un autre ESP avec la brique de decouverte active.</td></tr>';
  }
  return nodes.map(function (node) {
    var sensors = espNowNodeSensors(info, node);
    var imported = espNowImportedSensorCount(sensors);
    var actuators = espNowNodeActuators(info, node);
    var importedActuators = actuators.filter(isRemoteActuatorConfigured).length;
    var radio = espNowRadioQuality(node.rssiDbm);
    var actuatorCount = actuators.length || espNowCapabilityCount(node.capabilityFlags, 0x0040);
    return '<tr>' +
      '<td>' + espNowNodeNameWithTip(node) + '</td>' +
      '<td>' + esc(node.mac || "-") + '</td>' +
      '<td>' + esc(node.nodeId == null ? "-" : node.nodeId) + '</td>' +
      '<td><span class="badge info">' + esc(espNowRedundancyRoleText(node)) + '</span></td>' +
      '<td><span class="badge muted">' + esc(espNowFunctionalRoleText(node)) + '</span><small>' + esc(espNowRoleText(node.roleFlags)) + '</small></td>' +
      '<td>' + sensors.length + ' propose(s)<br><small>' + imported + ' importe(s)</small></td>' +
      '<td>' + actuatorCount + '<br><small>' + importedActuators + ' importe(s)</small></td>' +
      '<td>' + espNowNodeHealth(node) + '</td>' +
      '<td>' + esc(node.lastFrameType || "-") + '<br><small>heartbeat ' + esc(espNowAgeText(node.lastHeartbeatAgeMs)) + '</small></td>' +
      '<td><span class="' + esc(radio.cls) + '">' + esc(radio.text) + '</span></td>' +
      '<td>' + (node.peerKnown ? '<span class="badge ok">Autorise</span> <button onclick="removeEspNowPeer(\'' + esc(node.mac) + '\')">Retirer</button>' : '<button onclick="addEspNowPeer(\'' + esc(node.mac) + '\')">Autoriser</button>') + '</td>' +
    '</tr>';
  }).join("");
}

function espNowNodeNameWithTip(node) {
  var name = node.nodeName || "ESP-NOW";
  return '<span class="espNodeTip" tabindex="0"><b>' + esc(name) + '</b><span class="hoverTip espNodeHover">' + espNowNodeSensorTip(node) + '</span></span>';
}

function espNowNodeSensorTip(node) {
  var mac = String(node.mac || "").toUpperCase();
  var sensors = ((cache.espnow && cache.espnow.remoteSensors) || state.remoteSensors || []).filter(function (sensor) {
    return String(sensor.mac || "").toUpperCase() === mac;
  }).sort(function (a, b) {
    return Number(a.sensorId || 0) - Number(b.sensorId || 0);
  });
  var actuators = espNowNodeActuators(cache.espnow, node);
  var html = '<div class="espNodeTipHead"><strong>' + esc(node.nodeName || "ESP-NOW") + '</strong><span>' + esc(node.mac || "-") + '</span></div>' +
    '<div class="readonlyGrid espNodeStats">' +
      readOnlyLine("Node ID", node.nodeId == null ? "-" : node.nodeId) +
      readOnlyLine("Redondance", espNowRedundancyRoleText(node)) +
      readOnlyLine("Role fonctionnel", espNowFunctionalRoleText(node)) +
      readOnlyLine("Firmware", node.firmwareVersion || "-") +
      readOnlyLine("Uptime", formatDuration(node.uptimeMs)) +
      readOnlyLine("Derniere trame", (node.lastFrameType || "-") + " / " + espNowAgeText(node.lastFrameAgeMs)) +
      readOnlyLine("Heartbeat", espNowAgeText(node.lastHeartbeatAgeMs)) +
      readOnlyLine("RSSI", espNowRadioQuality(node.rssiDbm).text) +
    '</div>';
  if (!sensors.length && !actuators.length) {
    return html + '<div class="espSensorCard empty"><span class="badge warn">En attente</span><p>Aucune discovery capteur/actionneur recue</p><small>' + esc(espNowCapabilityText(node.capabilityFlags)) + '</small></div>';
  }
  sensors.forEach(function (sensor) {
    html += espNowSensorCard(sensor);
  });
  actuators.forEach(function (actuator) {
    html += espNowActuatorCard(actuator);
  });
  return html;
}

function espNowSensorCard(sensor) {
  var alreadyAdded = isRemoteSensorConfigured(sensor);
  var values = remoteSensorValueList(sensor);
  var valueTags = values.length ? values.map(function (value) {
    return '<i>' + esc(value.key || ("vt" + value.valueType)) + (value.unit ? ' <em>' + esc(value.unit) + '</em>' : '') + '</i>';
  }).join("") : '<i class="muted">valeurs en attente</i>';
  return '<section class="espSensorCard">' +
    '<strong>' + esc(sensor.sensorName || ("sensorId " + sensor.sensorId)) + '</strong>' +
    '<span class="badge ' + (alreadyAdded ? "ok" : "muted") + '">' + (alreadyAdded ? "Ajoute" : "Disponible") + '</span>' +
    '<span class="espSensorMeta">' + esc(sensor.sensorTypeText || sensor.sensorType || "-") + ' - ' + esc(sensor.sensorRole || "role non declare") + '</span>' +
    '<div class="espSensorValues">' + valueTags + '</div>' +
    '</section>';
}

function espNowActuatorCard(actuator) {
  var alreadyAdded = isRemoteActuatorConfigured(actuator);
  var tags = [
    actuator.electricalMode || "",
    hasValue(actuator.maxPowerW) ? actuator.maxPowerW + " W" : "",
    actuator.origin || "ESP-NOW"
  ].filter(function (value) { return value !== ""; }).map(function (value) {
    return '<i>' + esc(value) + '</i>';
  }).join("");
  return '<section class="espSensorCard">' +
    '<strong>' + esc(actuator.actuatorName || actuator.actuatorId || "Actionneur distant") + '</strong>' +
    '<span class="badge ' + (alreadyAdded ? "ok" : "muted") + '">' + (alreadyAdded ? "Ajoute" : "Disponible") + '</span>' +
    '<span class="espSensorMeta">' + esc(remoteActuatorConfigType(actuator)) + ' - ' + esc(actuator.actuatorRole || "role non declare") + '</span>' +
    '<div class="espSensorValues">' + (tags || '<i class="muted">valeurs en attente</i>') + '</div>' +
    '</section>';
}

function espNowPeerRows(peers) {
  if (!peers || !peers.length) return '<tr><td colspan="2">Aucun peer autorise.</td></tr>';
  return peers.map(function (mac) {
    return '<tr><td>' + esc(mac) + '</td><td><button onclick="removeEspNowPeer(\'' + esc(mac) + '\')">Retirer</button></td></tr>';
  }).join("");
}

async function espNowPage() {
  var info = await api("/api/espnow");
  cache.espnow = info;
  try {
    cache.actuators = await api("/api/actuators");
  } catch (e) {}
  var nodes = info.discoveredNodes || [];
  var peers = info.peers || [];
  $("app").innerHTML = banner() + '<h1>ESP-NOW</h1>' + helpBox("espnow") +
    '<div class="toolbar"><button onclick="refresh()">Actualiser</button><button onclick="forceEspNowAnnounce()">Forcer annonce maintenant</button><a href="/api/espnow">JSON API</a></div>' +
    '<div class="settingsGrid">' +
      '<section class="panel"><h2>Etat local</h2>' +
        miniState("ESP-NOW", info.ready ? "pret" : "off", info.ready ? "ok" : "bad") +
        miniState("MAC locale", info.mac || "-", "info") +
        miniState("Node ID", info.nodeId == null ? "-" : info.nodeId, "info") +
        miniState("Redondance", info.redundancyRole || "STANDALONE", "info") +
        miniState("Role fonctionnel", info.functionalRole || espNowFunctionalRoleText(info), "info") +
        miniState("Roles radio", espNowRoleText(info.roleFlags), "info") +
        miniState("Capacites", espNowCapabilityText(info.capabilityFlags), "info") +
        miniState("Exports capteurs", (info.exports || []).filter(function (item) { return item.exportEnabled; }).length, "info") +
        miniState("Exports actionneurs", (info.actuatorExports || []).filter(function (item) { return item.exportEnabled; }).length, "info") +
        miniState("Decouverte", (info.discoveryIntervalMs || 3000) + " ms", "muted") +
        '<p class="muted">Les exports actifs sont annonces automatiquement apres sauvegarde et periodiquement. Le bouton sert seulement au diagnostic.</p>' +
      '</section>' +
      '<section class="panel"><h2>Debug transport</h2><div class="formGrid">' +
        selectField("espNowDebugTransmission", "Debug transmission", info.debugTransmission === true) +
        selectField("espNowDebugReception", "Debug reception", info.debugReception === true) +
      '</div><p><button onclick="saveEspNowDebug()">Sauvegarder debug ESP-NOW</button></p><p class="muted">Transmission logue les TX FAST_DATA, SENSOR_DISCOVERY, DIAGNOSTIC et HEARTBEAT. Reception logue les trames FAST_DATA, SENSOR_DISCOVERY et DIAGNOSTIC recues. Les erreurs restent visibles meme debug coupe.</p></section>' +
    '</div>' +
    '<h2>ESP detectes</h2><table class="espNowTable"><tr><th>Nom</th><th>MAC</th><th>Node ID</th><th>Redondance</th><th>Role fonctionnel</th><th>Capteurs</th><th>Actionneurs</th><th>Sante</th><th>Derniere trame</th><th>Radio</th><th>Action</th></tr>' + espNowNodeRows(nodes, info) + '</table>' +
    '<h2>Peers autorises</h2><table><tr><th>MAC</th><th>Action</th></tr>' + espNowPeerRows(peers) + '</table>';
}

async function saveEspNowDebug() {
  var body = new URLSearchParams({
    debugTransmission: boolField("espNowDebugTransmission") ? "true" : "false",
    debugReception: boolField("espNowDebugReception") ? "true" : "false"
  });
  var response = await fetch("/api/espnow/config", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:body});
  if (!response.ok) return alert("Sauvegarde debug ESP-NOW refusee: " + await response.text());
  await espNowPage();
}

async function forceEspNowAnnounce() {
  var nodeResponse = await fetch("/api/espnow/discovery/announce", {method:"POST"});
  if (!nodeResponse.ok) return alert("Annonce ESP-NOW refusee: " + await nodeResponse.text());
  var response = await fetch("/api/espnow/sensors/announce", {method:"POST"});
  if (!response.ok) return alert("Annonce capteurs ESP-NOW refusee: " + await response.text());
  await espNowPage();
}

async function addEspNowPeer(mac) {
  var body = new URLSearchParams({mac:mac});
  var response = await fetch("/api/espnow/peer", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:body});
  if (!response.ok) return alert("Ajout peer refuse: " + await response.text());
  await espNowPage();
}

async function addEspNowPeerFromInput() {
  var mac = $("espNowPeerMac").value;
  await addEspNowPeer(mac);
}

async function removeEspNowPeer(mac) {
  if (!confirm("Retirer ce peer ESP-NOW ?")) return;
  var body = new URLSearchParams({mac:mac});
  var response = await fetch("/api/espnow/peer/remove", {method:"POST", headers:{"Content-Type":"application/x-www-form-urlencoded"}, body:body});
  if (!response.ok) return alert("Retrait peer refuse: " + await response.text());
  await espNowPage();
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
      tipLine("Frequence", state.gridFrequencyHz, "Hz") +
      tipLine("Derniere lecture", timeFromUptimeMs(state.lastJsyReadMs), "");
  }
  if (kind === "tic") {
    return tipLine("Etat", state.ticStatus || "N/A", "", state.ticAvailable ? "ok" : "warn") +
      tipLine("Puissance", state.ticGridPowerW, "W") +
      tipLine("Apparente", state.ticApparentPowerVA, "VA") +
      tipLine("Courant", state.ticCurrentA, "A") +
      tipLine("Index", state.ticEnergyWh, "Wh") +
      tipLine("Tarif", state.ticTariff || "N/A", "") +
      tipLine("Periode", state.ticPeriod || "N/A", "") +
      tipLine("Derniere lecture", timeFromUptimeMs(state.lastTicReadMs), "") +
      tipLine("Erreurs", state.ticErrorCount, "");
  }
  var temp = dsTemperature(index);
  var available = dsAvailable(index, temp);
  return tipLine("Etat", available ? "OK" : "Absent / non lu", "", available ? "ok" : "bad") +
    tipLine("Usage", "usage non configure", "") +
    tipLine("Temperature", available ? temp : "N/A", available ? "C" : "") +
    tipLine("Derniere lecture", timeFromUptimeMs(dsLastReadMs(index)), "") +
    tipLine("Nom", dsLabel(index), "");
}

function diagnosticPage() {
  var events = state.events || [];
  var localJsy = configuredLocalSensorByType("JSY-MK-194T", "jsy_grid");
  var localTic = configuredLocalSensorByType("TIC Linky", "tic_linky");
  var actuatorStates = diagnosticActuatorStates();
  var dsStates = [
    dsConfigured(0) ? miniState(dsLabel(0), dsAvailable(0, dsTemperature(0)) ? fmt(dsTemperature(0)) + " C" : "Absent / non lu", dsAvailable(0, dsTemperature(0)) ? tempClass(dsTemperature(0)) : "bad", sensorHoverTip("ds", 0)) : "",
    dsConfigured(1) ? miniState(dsLabel(1), dsAvailable(1, dsTemperature(1)) ? fmt(dsTemperature(1)) + " C" : "Absent / non lu", dsAvailable(1, dsTemperature(1)) ? tempClass(dsTemperature(1)) : "bad", sensorHoverTip("ds", 1)) : "",
    dsConfigured(2) ? miniState(dsLabel(2), dsAvailable(2, dsTemperature(2)) ? fmt(dsTemperature(2)) + " C" : "Absent / non lu", dsAvailable(2, dsTemperature(2)) ? tempClass(dsTemperature(2)) : "bad", sensorHoverTip("ds", 2)) : ""
  ].join("") || miniState("DS18B20", "Aucune sonde configuree", "muted");
  var localSensorStates = (localJsy ? miniState(localJsy.name || "JSY-MK-194T", state.jsyOnline ? "OK" : "Absent", state.jsyOnline ? "ok" : "bad", sensorHoverTip("jsy")) : "") +
    (localTic ? miniState(localTic.name || "TIC Linky", state.ticAvailable ? "OK" : "Absent", state.ticAvailable ? "ok" : "warn", sensorHoverTip("tic")) : "") +
    dsStates;
  $("app").innerHTML = banner() + '<h1>Diagnostic & Simulation</h1>' + helpBox("diagnostic") + '<div class="settingsGrid">' +
    '<section class="panel"><h2>Diagnostic systeme</h2>' +
      miniState("Securite", state.safetyLevel || "-", state.safetyTripped ? "bad" : "ok") +
      miniState("Raison", state.safetyReason || "aucune", state.safetyTripped ? "bad" : "muted") +
      miniState("WiFi", state.wifiConnected ? "connecte" : "AP/local", state.wifiConnected ? "ok" : "warn") +
      miniState("Mode reseau", state.networkMode || "-", "info") +
      miniState("Heap libre", state.heapFree ? Math.round(state.heapFree / 1024) + " Ko" : "-", "ok") +
      '<p><button onclick="identifyRouterLeds()">Identifier ESP 2 min</button></p>' +
      '<p class="muted">Fait clignoter rapidement les LED bleue et rouge configurees ensemble.</p>' +
      '<p><a href="/fs">Voir LittleFS</a> <a href="/api/diagnostic">JSON diagnostic</a></p>' +
    '</section>' +
    '<section class="panel"><h2>Capteurs</h2>' +
      localSensorStates +
    '</section>' +
    '<section class="panel"><h2>Simulation</h2>' + simulationControlsHtml() + '</section>' +
    '<section class="panel"><h2>Sorties calculees</h2>' +
      actuatorStates +
      '<p class="muted">En simulation, les sorties 230 V restent forcees OFF.</p>' +
    '</section>' +
    '</div><h2>Evenements</h2><p><button onclick="clearLogs()">Reset logs</button></p><table><tr><th>Heure</th><th>Niveau</th><th>Code</th><th>Message</th></tr>' +
    events.map(function (event) {
      return '<tr><td>' + esc(timeFromUptimeMs(event.timestampMs)) + '</td><td>' + esc(event.level) + '</td><td>' + esc(event.code) + '</td><td>' + esc(event.message) + '</td></tr>';
    }).join("") + '</table>';
}

function diagnosticActuatorStates() {
  var actuators = getActuators();
  if (!actuators.length) return miniState("Actionneurs", "aucun", "muted");
  return actuators.map(function (actuator) {
    var pct = getActuatorCommandPercent(actuator);
    var status = actuatorRuntimeStatus(actuator);
    return actuatorBar(actuator.name || actuator.id || "Actionneur", pct) +
      miniState((actuator.name || actuator.id || "Actionneur") + " etat", status.label, status.cls) +
      miniState((actuator.name || actuator.id || "Actionneur") + " origine", actuatorOrigin(actuator), actuatorOrigin(actuator) === "ESP-NOW" ? "info" : "muted");
  }).join("");
}

async function identifyRouterLeds() {
  var response = await fetch("/api/diagnostic/identify", {method:"POST"});
  if (!response.ok) return alert("Identification refusee: " + await response.text());
  alert("Identification lancee: les LED clignotent pendant 2 minutes.");
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
  var githubOta = sys.githubOta || {};
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
    dashboardBlock("OTA GitHub Releases", "verification et mise a jour volontaire",
      '<div class="blockMetricGrid">' +
        blockMetric("Firmware local", githubOta.localFirmwareVersion || sys.firmwareVersion || "N/A", "", "info", "") +
        blockMetric("LittleFS local", githubOta.localLittlefsVersion || storage.version || "N/A", "", "info", "") +
        blockMetric("Version GitHub", "-", "", "muted", "Cliquer verifier") +
      '</div><div id="githubOtaStatus" class="pendingBox">Aucune verification GitHub effectuee.</div>' +
      '<div class="toolbar">' +
        '<button onclick="checkGithubOta()">Verifier GitHub</button>' +
        '<button class="danger" onclick="updateGithubFirmware()">Mettre a jour firmware</button>' +
        '<button class="danger" onclick="updateGithubLittlefs()">Mettre a jour LittleFS</button>' +
        '<a href="' + esc(githubOta.releaseUrl || "https://github.com/auvinetandre-coder/Prj_esp32_2026/releases/latest") + '">Release GitHub</a>' +
      '</div><p class="muted">Aucune mise a jour automatique. Firmware et LittleFS restent separes.</p>', "githubOta") +
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
        blockMetric("SSR1 / SSR2", fmt(solar.ssr1Percent) + " / " + fmt(solar.ssr2Percent), "%", "muted", "") +
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

async function checkGithubOta() {
  var box = $("githubOtaStatus");
  if (box) box.textContent = "Verification GitHub en cours...";
  try {
    var response = await fetch("/api/ota/github/check");
    var text = await response.text();
    var info;
    try { info = JSON.parse(text); } catch (parseError) { info = {ok:false, error:text || parseError.message}; }
    if (!response.ok) info.ok = false;
    if (!box) return;
    var cls = info.ok ? (info.updateAvailable ? "pendingBox" : "help") : "banner";
    box.className = cls;
    box.innerHTML =
      '<b>' + (info.ok ? "GitHub accessible" : "Verification refusee") + '</b><br>' +
      'Version distante: ' + esc(info.remoteVersion || "N/A") + '<br>' +
      'Firmware: local ' + esc(info.localFirmwareVersion || "N/A") + ' / GitHub ' + esc(info.remoteFirmwareVersion || "N/A") +
      (info.firmwareUpdateAvailable ? ' <span class="badge warn">MAJ disponible</span>' : ' <span class="badge ok">OK</span>') + '<br>' +
      'LittleFS: local ' + esc(info.localLittlefsVersion || "N/A") + ' / GitHub ' + esc(info.remoteLittlefsVersion || "N/A") +
      (info.littlefsUpdateAvailable ? ' <span class="badge warn">MAJ disponible</span>' : ' <span class="badge ok">OK</span>') +
      (info.notes ? '<br>Notes: ' + esc(info.notes) : '') +
      (info.error ? '<br>Erreur: ' + esc(info.error) : '');
  } catch (e) {
    if (box) {
      box.className = "banner";
      box.textContent = "Verification GitHub impossible: " + e.message;
    }
  }
}

async function updateGithubFirmware() {
  if (!confirm("Telecharger firmware.bin depuis GitHub Releases et redemarrer ?")) return;
  $("app").innerHTML = '<h1>OTA firmware GitHub...</h1><div class="panel">Telechargement en cours. Ne coupe pas l alimentation.</div>';
  var response = await fetch("/api/ota/github/firmware", {method:"POST"});
  var text = await response.text();
  if (!response.ok) {
    $("app").innerHTML = '<h1>OTA firmware refusee</h1><div class="banner">' + esc(text) + '</div>';
    return;
  }
  $("app").innerHTML = '<h1>OTA firmware appliquee</h1><div class="panel">L ESP32 redemarre. Attends quelques secondes puis recharge /app.</div>';
}

async function updateGithubLittlefs() {
  if (!confirm("Sauvegarder /config, telecharger littlefs.bin depuis GitHub Releases et redemarrer ?")) return;
  $("app").innerHTML = '<h1>OTA LittleFS GitHub...</h1><div class="panel">Sauvegarde config puis telechargement en cours. Ne coupe pas l alimentation.</div>';
  var response = await fetch("/api/ota/github/littlefs", {method:"POST"});
  var text = await response.text();
  if (!response.ok) {
    $("app").innerHTML = '<h1>OTA LittleFS refusee</h1><div class="banner">' + esc(text) + '</div>';
    return;
  }
  $("app").innerHTML = '<h1>OTA LittleFS appliquee</h1><div class="panel">L ESP32 redemarre puis restaure /config si necessaire.</div>';
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
  if (page === "espnow") return espNowPage();
  if (isUserEditingSettings() && updateDashboardLiveSections()) return;
  $("app").innerHTML = dashboard();
  refreshLabelPatch();
  renderDashboardCanvases();
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
["focusin", "input", "change", "pointerdown"].forEach(function (eventName) {
  document.addEventListener(eventName, function (event) {
    if (dashboardEditingContainer(event.target)) markDashboardSettingsEditing(event.target, eventName === "input" || eventName === "change");
  }, true);
});
document.addEventListener("focusout", function (event) {
  if (!dashboardEditingContainer(event.target)) return;
  setTimeout(function () {
    if (!dashboardEditingContainer(document.activeElement)) clearDashboardEditingState();
  }, 0);
}, true);
window.addEventListener("beforeunload", function () {
  saveGraphHistorySoon(true);
  saveDashboardHistorySoon(true);
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
