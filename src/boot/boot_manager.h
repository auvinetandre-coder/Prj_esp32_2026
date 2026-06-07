#pragma once

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace BootManager {

static const char *FACTORY_LABEL = "factory";
static const char *MAIN_LABEL = "ota_0";

inline const esp_partition_t *findPartitionByLabel(const char *label) {
  if (!label) return nullptr;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (it) {
    const esp_partition_t *part = esp_partition_get(it);
    if (part && strncmp(part->label, label, sizeof(part->label)) == 0) {
      esp_partition_iterator_release(it);
      return part;
    }
    it = esp_partition_next(it);
  }
  return nullptr;
}

inline String partitionInfo(const esp_partition_t *part) {
  if (!part) return "N/A";
  String out = part->label;
  out += " @0x";
  out += String(part->address, HEX);
  out += " size=";
  out += String(part->size);
  return out;
}

inline String getRunningPartitionInfo() {
  return partitionInfo(esp_ota_get_running_partition());
}

inline String getBootPartitionInfo() {
  return partitionInfo(esp_ota_get_boot_partition());
}

inline String getMainPartitionInfo() {
  return partitionInfo(findPartitionByLabel(MAIN_LABEL));
}

inline bool setNextBoot(const char *label) {
  const esp_partition_t *part = findPartitionByLabel(label);
  return part && esp_ota_set_boot_partition(part) == ESP_OK;
}

inline bool setNextBootFactory() {
  return setNextBoot(FACTORY_LABEL);
}

inline bool setNextBootMain() {
  return setNextBoot(MAIN_LABEL);
}

inline bool bootToFactory(uint32_t delayMs = 100) {
  if (!setNextBootFactory()) return false;
  delay(delayMs);
  ESP.restart();
  return true;
}

inline bool bootToMain(uint32_t delayMs = 100) {
  if (!setNextBootMain()) return false;
  delay(delayMs);
  ESP.restart();
  return true;
}

}  // namespace BootManager
