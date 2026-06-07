#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

static const uint8_t RECOVERY_HOLD_GPIO = 0;
static const uint32_t MAINTENANCE_WINDOW_MS = 0;
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 12000;
static const char *RECOVERY_SSID_PREFIX = "RSrecoverymode_";
static const char *RECOVERY_PASSWORD = "recovery1234";
static const char *DEFAULT_LOCAL_SSID = "TASPASLEDROIT";
static const char *DEFAULT_LOCAL_PASSWORD = "AA110671AA";
static const char *RECOVERY_NVS_NAMESPACE = "recovery";
static const bool AUTO_CONNECT_LOCAL_WIFI = true;
static const bool ENABLE_LOCAL_WIFI_FORM = true;

WebServer server(80);

static bool stayRecovery = false;
static bool otaOk = false;
static bool otaFailed = false;
static bool otaStarted = false;
static bool fsOk = false;
static bool fsFailed = false;
static bool fsStarted = false;
static bool stationConnected = false;
static size_t otaWritten = 0;
static size_t fsWritten = 0;
static esp_ota_handle_t otaHandle = 0;
static String stationSsid;
static String stationIp = "0.0.0.0";
static String lastMessage = "Recovery pret";
static String lastFirmwareState = "Aucun upload firmware";
static String lastFirmwareFile = "-";
static String lastFsState = "Aucun upload LittleFS";
static String lastFsFile = "-";
static String recoveryApSsid;

static String htmlEscape(const String &text) {
  String out;
  out.reserve(text.length() + 8);
  for (size_t i = 0; i < text.length(); ++i) {
    char c = text[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else out += c;
  }
  return out;
}

static String uptimeText() {
  uint32_t seconds = millis() / 1000;
  uint32_t hours = seconds / 3600;
  uint32_t minutes = (seconds / 60) % 60;
  seconds %= 60;
  char buf[20];
  snprintf(buf, sizeof(buf), "T+%02lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);
  return String(buf);
}

static String uploadState(const char *kind, const String &fileName, const String &state, size_t written) {
  String out = uptimeText();
  out += " - ";
  out += kind;
  out += " - ";
  out += fileName.length() ? fileName : "-";
  out += " - ";
  out += state;
  if (written > 0) {
    out += " - ";
    out += String(written);
    out += " octets";
  }
  return out;
}

static String makeRecoverySsid() {
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X", (uint16_t)(ESP.getEfuseMac() & 0xFFFF));
  return String(RECOVERY_SSID_PREFIX) + suffix;
}

static const esp_partition_t *mainPartition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "ota_0");
}

static const esp_partition_t *littleFsPartition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
}

static String partitionInfo(const esp_partition_t *part) {
  if (!part) return "N/A";
  String out = part->label;
  out += " @0x";
  out += String(part->address, HEX);
  out += " size=";
  out += String(part->size);
  return out;
}

static bool setNextBootMain() {
  const esp_partition_t *main = mainPartition();
  return main && esp_ota_set_boot_partition(main) == ESP_OK;
}

static bool bootToMain(uint32_t delayMs) {
  if (!setNextBootMain()) return false;
  delay(delayMs);
  ESP.restart();
  return true;
}

static bool isWifiPlaceholder(const String &value) {
  if (value.length() == 0) return true;
  String text = value;
  text.toUpperCase();
  return text.indexOf("A_CONFIGURER") >= 0 || text.indexOf("ACONFIGURER") >= 0;
}

static bool connectToLocalWifi(const String &ssid, const String &password) {
  if (isWifiPlaceholder(ssid)) {
    lastMessage = "SSID WiFi local vide";
    return false;
  }

  stationConnected = false;
  stationSsid = ssid;
  stationIp = "0.0.0.0";
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  lastMessage = String("Connexion WiFi local: ") + ssid;

  const uint32_t started = millis();
  while (millis() - started < WIFI_CONNECT_TIMEOUT_MS) {
    server.handleClient();
    if (WiFi.status() == WL_CONNECTED) {
      stationConnected = true;
      stationIp = WiFi.localIP().toString();
      lastMessage = String("WiFi local connecte: ") + stationIp;
      Serial.print(F("Recovery STA IP: "));
      Serial.println(stationIp);
      return true;
    }
    delay(50);
  }

  WiFi.disconnect(false);
  lastMessage = "Connexion WiFi local echouee, AP recovery actif";
  return false;
}

static bool readSavedWifi(String &ssid, String &password) {
  Preferences prefs;
  ssid = DEFAULT_LOCAL_SSID;
  password = DEFAULT_LOCAL_PASSWORD;
  if (prefs.begin(RECOVERY_NVS_NAMESPACE, true)) {
    ssid = prefs.getString("ssid", DEFAULT_LOCAL_SSID);
    password = prefs.getString("pass", DEFAULT_LOCAL_PASSWORD);
    prefs.end();
  }
  if (isWifiPlaceholder(ssid)) ssid = DEFAULT_LOCAL_SSID;
  if (!password.length()) password = DEFAULT_LOCAL_PASSWORD;
  return !isWifiPlaceholder(ssid) && password.length() > 0;
}

static bool saveWifi(const String &ssid, const String &password) {
  if (isWifiPlaceholder(ssid)) return false;
  Preferences prefs;
  if (!prefs.begin(RECOVERY_NVS_NAMESPACE, false)) return false;
  bool ok = prefs.putString("ssid", ssid) > 0;
  ok = prefs.putString("pass", password) > 0 && ok;
  prefs.end();
  return ok;
}

static String htmlPage() {
  const esp_partition_t *main = mainPartition();
  const esp_partition_t *fs = littleFsPartition();
  String html;
  html.reserve(9800);
  html += F("<!doctype html><html lang='fr'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>RouteurSolaire Recovery</title><style>body{font-family:system-ui;margin:0;background:#111827;color:#f9fafb;font-size:14px}.top{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:12px 14px;border-bottom:1px solid #374151}.top h1{font-size:22px;margin:0}.top p{margin:0;color:#9ca3af}.grid{display:grid;grid-template-columns:2.1fr 1fr;gap:10px;padding:10px;align-items:start}.card{border:1px solid #374151;background:#1f2937;border-radius:8px;padding:10px;min-width:0}.wide{grid-row:span 2}.full{grid-column:1/-1}h2{font-size:17px;margin:0 0 8px}.kv{display:grid;grid-template-columns:128px minmax(0,1fr);gap:5px 8px}.kv span:nth-child(odd){color:#9ca3af}.kv span:nth-child(even),.state{word-break:break-word}a,button,input{font:inherit}input{box-sizing:border-box;width:100%;padding:8px;margin:4px 0;background:#030712;color:#fff;border:1px solid #374151;border-radius:6px}input[type=submit],a.btn{background:#1f2937;color:#fff;border:1px solid #374151;border-radius:6px;padding:9px 10px;text-decoration:none;display:inline-block}.danger{background:#3b1111;border-color:#ef4444}.actions{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px}.compact input[type=submit]{padding:7px 9px;margin:0}.muted{color:#9ca3af}.bar{height:12px;background:#030712;border:1px solid #374151;border-radius:6px;overflow:hidden;margin:7px 0}.fill{height:100%;width:0;background:#22c55e;transition:width .15s}.state{font-size:.9rem;margin:6px 0}.hint{margin:0 0 8px}@media(max-width:980px){.grid{grid-template-columns:1fr}.wide{grid-row:auto}.actions{grid-template-columns:1fr}.top{display:block}.top p{margin-top:4px}}</style></head><body>");
  html += F("<header class='top'><h1>RouteurSolaire Recovery</h1><p>");
  html += WiFi.softAPIP().toString();
  html += F("</p></header><main class='grid'>");
  html += F("<section class='card wide'><h2>Etat</h2><div class='kv'><span>Active</span><span>");
  html += partitionInfo(esp_ota_get_running_partition());
  html += F("</span><span>Boot</span><span>");
  html += partitionInfo(esp_ota_get_boot_partition());
  html += F("</span><span>Main ota_0</span><span>");
  html += main ? partitionInfo(main) : "introuvable";
  html += F("</span><span>LittleFS</span><span>");
  html += fs ? partitionInfo(fs) : "introuvable";
  html += F("</span><span>SSID AP</span><span>");
  html += htmlEscape(recoveryApSsid);
  html += F("</span><span>AP</span><span>http://");
  html += WiFi.softAPIP().toString();
  html += F("</span><span>WiFi local</span><span>");
  html += stationConnected ? String("connecte a ") + stationSsid + " - http://" + stationIp : "non connecte";
  html += F("</span><span>Statut</span><span>");
  html += lastMessage;
  html += F("</span><span>Firmware</span><span id='lastFirmware'>");
  html += htmlEscape(lastFirmwareState);
  html += F("</span><span>LittleFS</span><span id='lastFs'>");
  html += htmlEscape(lastFsState);
  html += F("</span></div><div class='actions compact'><form method='POST' action='/boot-main'><input type='submit' value='Demarrer ota_0'></form><form method='POST' action='/stay'><input type='submit' value='Rester recovery'></form></div></section>");
  if (ENABLE_LOCAL_WIFI_FORM) {
    html += F("<section class='card'><h2>WiFi local</h2><form method='POST' action='/wifi'><label>SSID<input name='ssid' value='");
    html += stationSsid;
    html += F("' required></label><label>Mot de passe<input name='pass' type='password'></label><input type='submit' value='Connecter'></form></section>");
  }
  html += F("<section class='card'><h2>Firmware</h2><p class='hint muted'>ota_0, sans redemarrage.</p><form id='firmwareForm' method='POST' action='/upload' enctype='multipart/form-data'><input type='file' name='firmware' accept='.bin' required><div class='bar'><div id='firmwareBar' class='fill'></div></div><p id='firmwareState' class='state muted'>Pret</p><input class='danger' type='submit' value='Ecrire ota_0'></form></section>");
  html += F("<section class='card'><h2>LittleFS</h2><p class='hint muted'>spiffs, sans redemarrage.</p><form id='fsForm' method='POST' action='/upload-fs' enctype='multipart/form-data'><input type='file' name='littlefs' accept='.bin' required><div class='bar'><div id='fsBar' class='fill'></div></div><p id='fsState' class='state muted'>Pret</p><input class='danger' type='submit' value='Ecrire spiffs'></form></section></main>");
  html += F("<script>function h(d){return d.toLocaleTimeString('fr-FR',{hour:'2-digit',minute:'2-digit',second:'2-digit'});}function bind(formId,barId,stateId,lastId,key,label){var f=document.getElementById(formId),bar=document.getElementById(barId),st=document.getElementById(stateId),last=document.getElementById(lastId);var saved=localStorage.getItem(key);if(saved)last.textContent=saved;f.addEventListener('submit',function(e){e.preventDefault();var file=f.querySelector('input[type=file]').files[0];if(!file)return;bar.style.width='0%';st.textContent='Envoi de '+file.name+'...';var xhr=new XMLHttpRequest();xhr.open('POST',f.action);xhr.setRequestHeader('X-Recovery-XHR','1');xhr.upload.onprogress=function(ev){if(ev.lengthComputable){var p=Math.round(ev.loaded*100/ev.total);bar.style.width=p+'%';st.textContent='Envoi '+p+'% - '+ev.loaded+' / '+ev.total+' octets';}};xhr.onload=function(){var ok=xhr.status>=200&&xhr.status<300;bar.style.width=ok?'100%':'0%';var msg=h(new Date())+' - '+label+' - '+file.name+' - '+(ok?'OK':'ERREUR')+' - '+file.size+' octets';st.textContent=msg;last.textContent=msg;localStorage.setItem(key,msg);};xhr.onerror=function(){var msg=h(new Date())+' - '+label+' - '+file.name+' - ERREUR reseau';st.textContent=msg;last.textContent=msg;localStorage.setItem(key,msg);};xhr.send(new FormData(f));});}bind('firmwareForm','firmwareBar','firmwareState','lastFirmware','rs_fw_last','Firmware');bind('fsForm','fsBar','fsState','lastFs','rs_fs_last','LittleFS');</script>");
  html += F("</body></html>");
  return html;
}

static void routes() {
  server.on("/", HTTP_GET, []() { server.send(200, "text/html; charset=utf-8", htmlPage()); });
  server.onNotFound([]() { server.send(200, "text/html; charset=utf-8", htmlPage()); });
  server.on("/stay", HTTP_POST, []() {
    stayRecovery = true;
    lastMessage = "Maintien en recovery demande";
    server.sendHeader("Location", "/", true);
    server.send(303, "text/plain", "Recovery");
  });
  server.on("/boot-main", HTTP_POST, []() {
    lastMessage = "Bascule vers ota_0";
    server.send(200, "text/html; charset=utf-8", F("<h1>Boot ota_0...</h1>"));
    bootToMain(250);
  });
  server.on("/wifi", HTTP_POST, []() {
    stayRecovery = true;
    if (saveWifi(server.arg("ssid"), server.arg("pass"))) {
      connectToLocalWifi(server.arg("ssid"), server.arg("pass"));
      server.sendHeader("Location", "/", true);
      server.send(303, "text/plain", "WiFi");
    } else {
      lastMessage = "Sauvegarde WiFi NVS refusee";
      server.sendHeader("Location", "/", true);
      server.send(303, "text/plain", "WiFi");
    }
  });
  server.on("/upload", HTTP_POST, []() {
    if (otaOk) {
      if (server.hasHeader("X-Recovery-XHR")) {
        server.send(200, "text/plain", lastFirmwareState);
      } else {
        server.sendHeader("Location", "/", true);
        server.send(303, "text/plain", "Firmware OK");
      }
    } else {
      stayRecovery = true;
      server.send(500, "text/html; charset=utf-8", String("<h1>Upload refuse</h1><p>") + lastMessage + "</p><p><a href='/'>Retour recovery</a></p>");
    }
  }, []() {
    HTTPUpload &upload = server.upload();
    const esp_partition_t *main = mainPartition();
    if (!main) {
      otaFailed = true;
      lastMessage = "Partition ota_0 introuvable";
      return;
    }
    if (upload.status == UPLOAD_FILE_START) {
      otaOk = false;
      otaFailed = false;
      otaStarted = false;
      otaWritten = 0;
      otaHandle = 0;
      lastFirmwareFile = upload.filename;
      lastFirmwareState = uploadState("Firmware", lastFirmwareFile, "demarre", 0);
      esp_err_t err = esp_ota_begin(main, OTA_SIZE_UNKNOWN, &otaHandle);
      otaStarted = err == ESP_OK;
      if (!otaStarted) {
        otaFailed = true;
        lastMessage = String("esp_ota_begin ota_0: ") + esp_err_to_name(err);
        lastFirmwareState = uploadState("Firmware", lastFirmwareFile, lastMessage, 0);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (otaFailed || !otaStarted) return;
      if (otaWritten + upload.currentSize > main->size) {
        otaFailed = true;
        lastMessage = "Firmware trop gros pour ota_0";
        lastFirmwareState = uploadState("Firmware", lastFirmwareFile, lastMessage, otaWritten);
        esp_ota_abort(otaHandle);
        return;
      }
      esp_err_t err = esp_ota_write(otaHandle, upload.buf, upload.currentSize);
      if (err != ESP_OK) {
        otaFailed = true;
        lastMessage = String("esp_ota_write ota_0: ") + esp_err_to_name(err);
        lastFirmwareState = uploadState("Firmware", lastFirmwareFile, lastMessage, otaWritten);
        esp_ota_abort(otaHandle);
        return;
      }
      otaWritten += upload.currentSize;
    } else if (upload.status == UPLOAD_FILE_END) {
      if (otaFailed || !otaStarted) return;
      esp_err_t err = esp_ota_end(otaHandle);
      otaOk = err == ESP_OK;
      lastMessage = otaOk ? String("Firmware local ecrit dans ota_0: ") + otaWritten + " octets"
                          : String("esp_ota_end ota_0: ") + esp_err_to_name(err);
      lastFirmwareState = uploadState("Firmware", lastFirmwareFile, otaOk ? "OK" : lastMessage, otaWritten);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      otaFailed = true;
      lastMessage = "Upload annule";
      lastFirmwareState = uploadState("Firmware", lastFirmwareFile, lastMessage, otaWritten);
      if (otaStarted) esp_ota_abort(otaHandle);
    }
  });
  server.on("/upload-fs", HTTP_POST, []() {
    if (fsOk) {
      if (server.hasHeader("X-Recovery-XHR")) {
        server.send(200, "text/plain", lastFsState);
      } else {
        server.sendHeader("Location", "/", true);
        server.send(303, "text/plain", "LittleFS OK");
      }
    } else {
      stayRecovery = true;
      server.send(500, "text/html; charset=utf-8", String("<h1>Upload LittleFS refuse</h1><p>") + lastMessage + "</p><p><a href='/'>Retour recovery</a></p>");
    }
  }, []() {
    HTTPUpload &upload = server.upload();
    const esp_partition_t *fs = littleFsPartition();
    if (!fs) {
      fsFailed = true;
      lastMessage = "Partition spiffs introuvable";
      return;
    }
    if (upload.status == UPLOAD_FILE_START) {
      fsOk = false;
      fsFailed = false;
      fsStarted = false;
      fsWritten = 0;
      lastFsFile = upload.filename;
      lastFsState = uploadState("LittleFS", lastFsFile, "demarre", 0);
      esp_err_t err = esp_partition_erase_range(fs, 0, fs->size);
      fsStarted = err == ESP_OK;
      if (!fsStarted) {
        fsFailed = true;
        lastMessage = String("Erase spiffs: ") + esp_err_to_name(err);
        lastFsState = uploadState("LittleFS", lastFsFile, lastMessage, 0);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (fsFailed || !fsStarted) return;
      if (fsWritten + upload.currentSize > fs->size) {
        fsFailed = true;
        lastMessage = "Image LittleFS trop grosse pour spiffs";
        lastFsState = uploadState("LittleFS", lastFsFile, lastMessage, fsWritten);
        return;
      }
      esp_err_t err = esp_partition_write(fs, fsWritten, upload.buf, upload.currentSize);
      if (err != ESP_OK) {
        fsFailed = true;
        lastMessage = String("Write spiffs: ") + esp_err_to_name(err);
        lastFsState = uploadState("LittleFS", lastFsFile, lastMessage, fsWritten);
        return;
      }
      fsWritten += upload.currentSize;
    } else if (upload.status == UPLOAD_FILE_END) {
      if (fsFailed || !fsStarted) return;
      fsOk = fsWritten > 0 && fsWritten <= fs->size;
      lastMessage = fsOk ? String("LittleFS ecrit dans spiffs: ") + fsWritten + " octets"
                         : "Upload LittleFS vide";
      lastFsState = uploadState("LittleFS", lastFsFile, fsOk ? "OK" : lastMessage, fsWritten);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      fsFailed = true;
      lastMessage = "Upload LittleFS annule";
      lastFsState = uploadState("LittleFS", lastFsFile, lastMessage, fsWritten);
    }
  });
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F("=== RouteurSolaire Recovery ==="));
  Serial.print(F("Running partition: "));
  Serial.println(partitionInfo(esp_ota_get_running_partition()));
  Serial.print(F("Main partition: "));
  Serial.println(partitionInfo(mainPartition()));

  pinMode(RECOVERY_HOLD_GPIO, INPUT_PULLUP);
  stayRecovery = digitalRead(RECOVERY_HOLD_GPIO) == LOW;

  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP);
  IPAddress apIp(192, 168, 4, 1);
  IPAddress apGateway(192, 168, 4, 1);
  IPAddress apSubnet(255, 255, 255, 0);
  recoveryApSsid = makeRecoverySsid();
  WiFi.softAPConfig(apIp, apGateway, apSubnet);
  WiFi.softAP(recoveryApSsid.c_str(), RECOVERY_PASSWORD);
  routes();
  server.begin();
  Serial.print(F("Recovery SSID: "));
  Serial.println(recoveryApSsid);
  Serial.print(F("Recovery AP: http://"));
  Serial.println(WiFi.softAPIP());
  String savedSsid;
  String savedPassword;
  if (AUTO_CONNECT_LOCAL_WIFI && readSavedWifi(savedSsid, savedPassword)) {
    connectToLocalWifi(savedSsid, savedPassword);
  } else {
    stationSsid = savedSsid.length() ? savedSsid : DEFAULT_LOCAL_SSID;
  }

  const uint32_t started = millis();
  while (MAINTENANCE_WINDOW_MS > 0 && !stayRecovery && millis() - started < MAINTENANCE_WINDOW_MS) {
    server.handleClient();
    delay(5);
  }
  if (MAINTENANCE_WINDOW_MS > 0 && !stayRecovery) {
    Serial.println(F("No maintenance request, booting ota_0"));
    if (!bootToMain(250)) Serial.println(F("ERROR: unable to boot ota_0"));
  } else {
    Serial.println(F("Recovery maintenance mode"));
  }
}

void loop() {
  server.handleClient();
}
