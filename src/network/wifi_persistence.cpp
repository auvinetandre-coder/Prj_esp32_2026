#include "wifi_manager.h"
#include "wifi_persistence.h"
#include <LittleFS.h>
#include "../diagnostics/console_serial.h"

static const char *wifiCheckpointPaths[] = {"/diagnostics/wifi-a.bin", "/diagnostics/wifi-b.bin"};

static bool readWifiCheckpoint(const char *path, WifiPersistedRecord &record) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  const size_t length = file.size();
  const size_t got = length == sizeof(record) ? file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) : 0;
  file.close();
  return got == sizeof(record) && wifiPersistedValid(record, length);
}

void SolarWiFiManager::beginDiagnosticsPersistence() {
  if (persistenceAvailable) return;
  persistenceAvailable = LittleFS.totalBytes() > 0;
  if (!persistenceAvailable) { persistenceStatus = "LITTLEFS_INDISPONIBLE"; return; }
  WifiPersistedRecord chosen, candidate;
  bool found = false, existed = false;
  for (uint8_t slot = 0; slot < 2; ++slot) {
    existed |= LittleFS.exists(wifiCheckpointPaths[slot]);
    if (readWifiCheckpoint(wifiCheckpointPaths[slot], candidate) && (!found || wifiGenerationNewer(candidate.generation, chosen.generation))) {
      chosen = candidate; persistedSlot = slot; found = true;
    }
  }
  if (!found) { persistenceStatus = existed ? "HISTORIQUE_ILLISIBLE" : "PREMIER_DEMARRAGE"; return; }
  // Called before event registration: no concurrent callback can change this ring yet.
  diagnosticBootId = chosen.bootId + 1;
  if (!diagnosticBootId) diagnosticBootId = 1;
  diagnosticTotal = chosen.total; persistedTotal = chosen.total;
  diagnosticCount = chosen.count; diagnosticHead = chosen.count % diagnosticCapacity;
  for (uint8_t i = 0; i < chosen.count; ++i) diagnosticEvents[i] = chosen.events[i];
  persistedGeneration = chosen.generation;
  lastConnectedAvailable = chosen.hasLastConnected;
  if (chosen.hasLastConnected) { lastConnectedState = chosen.lastConnected; diagnosticLastConnectedBoot = chosen.lastConnectedBoot; }
  previousBootState = chosen.lastState; previousBootId = chosen.bootId; previousBootAvailable = true;
  persistenceStatus = "HISTORIQUE_RESTAURE";
}

void SolarWiFiManager::persistDiagnostics(uint32_t now) {
  if (!persistenceAvailable || now < 3000UL) return;
  // At most one attempt per 10 seconds (including failures), periodic photo every 5 minutes.
  if (persistenceAttempted && uint32_t(now - lastPersistMs) < 10000UL) return;
  uint32_t currentTotal;
  portENTER_CRITICAL(&diagnosticMux);
  currentTotal = diagnosticTotal;
  portEXIT_CRITICAL(&diagnosticMux);
  if (persistedGeneration && currentTotal == persistedTotal && uint32_t(now - lastPersistMs) < 300000UL) return;
  lastPersistMs = now; persistenceAttempted = true;
  if (!LittleFS.exists("/diagnostics") && !LittleFS.mkdir("/diagnostics")) { persistenceStatus = "ERREUR_REPERTOIRE"; return; }
  WifiPersistedRecord record{};
  record.bytes = sizeof(record); record.generation = persistedGeneration + 1; record.bootId = diagnosticBootId;
  record.count = copyDiagnostics(record.lastState, record.events, diagnosticCapacity, record.total);
  record.hasLastConnected = copyLastConnected(record.lastConnected);
  record.lastConnectedBoot = diagnosticLastConnectedBoot;
  record.crc = wifiDiagnosticCrc(reinterpret_cast<const uint8_t *>(&record), offsetof(WifiPersistedRecord, crc));
  // Only overwrite the inactive copy. A failed or torn write leaves the previous valid slot intact.
  const uint8_t target = persistedSlot == 0 ? 1 : 0;
  File file = LittleFS.open(wifiCheckpointPaths[target], "w");
  if (!file) { persistenceStatus = "ERREUR_OUVERTURE"; return; }
  const size_t written = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record));
  file.flush(); file.close();
  WifiPersistedRecord verified;
  if (written != sizeof(record) || !readWifiCheckpoint(wifiCheckpointPaths[target], verified) || verified.generation != record.generation || verified.crc != record.crc) {
    persistenceStatus = "ERREUR_ECRITURE_VERIFICATION"; return;
  }
  persistedSlot = target; persistedGeneration = record.generation; persistedTotal = record.total;
  persistenceStatus = "SAUVEGARDE_VERIFIEE";
}
