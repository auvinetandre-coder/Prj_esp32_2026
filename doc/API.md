# API.md - Contrat API RouteurSolaireESP32

## Objectif

Documenter les API utilisees par la WebUI et la maintenance.

Les API existantes doivent rester compatibles quand elles sont encore appelees par la WebUI.

---

## Endpoint principal

```http
GET /api/state
```

Si `/api/state` est un alias de `/api/status-lite`, conserver cette compatibilite.

---

## Structure recommandee

```json
{
  "energy": {},
  "pid": {},
  "sensors": [],
  "actuators": [],
  "systemHealth": {},
  "performance": {},
  "versions": {},
  "ota": {}
}
```

---

## energy

Convention :

- `gridPowerW > 0` : achat reseau ;
- `gridPowerW < 0` : injection reseau.

Exemple :

```json
{
  "productionW": 1250,
  "gridPowerW": -350,
  "heaterPowerW": 600,
  "heaterPercent": 48,
  "tankTempC": 54.2
}
```

---

## pid

```json
{
  "enabled": true,
  "mode": "AUTO",
  "targetGridW": 0,
  "deadbandW": 30,
  "errorW": -350,
  "outputPercent": 48,
  "commandPercent": 48,
  "status": "STABLE",
  "source": "ESPNOW_JSY"
}
```

---

## sensors[]

Tous les capteurs locaux et ESP-NOW doivent apparaitre ici.

```json
{
  "id": "jsy_grid_remote",
  "name": "JSY reseau",
  "type": "JSY-MK-194T",
  "origin": "ESP-NOW",
  "ok": true,
  "enabled": true,
  "ageMs": 120,
  "values": {
    "VOLT": 230.1,
    "CH1_POWER": -350,
    "CH2_POWER": 600,
    "PF": 0.98
  }
}
```

Regles :

- `origin` : `LOCAL` ou `ESP-NOW` ;
- `values` doit etre un objet ;
- valeurs absentes tolerees ;
- afficher `--` cote WebUI si absent.

---

## versions

```json
{
  "firmware": "20260606-07",
  "littlefs": "20260606-07",
  "build": "20260606-07",
  "runningPartition": "ota_0",
  "bootPartition": "factory",
  "mainPartition": "ota_0"
}
```

Les informations de partition doivent venir de BootManager quand elles sont disponibles.

---

## ota / maintenance

Le champ `ota` doit representer l'etat de maintenance Recovery/Main, pas une promesse GitHub OTA.

Exemple :

```json
{
  "runningInfo": "ota_0 @0x120000 size=1966080",
  "bootInfo": "factory @0x10000 size=1114112",
  "mainInfo": "ota_0 @0x120000 size=1966080",
  "recoveryAvailable": true,
  "localUpdateOnly": true
}
```

---

## Endpoints de maintenance

Endpoints ou actions attendus :

```http
POST /api/system/recovery
upload local firmware.bin depuis recovery
upload local LittleFS depuis recovery si conserve
```

Regles :

- action utilisateur obligatoire ;
- firmware main et LittleFS separes ;
- firmware ecrit uniquement dans `ota_0` ;
- recovery conserve en `factory` ;
- `/config` preserve ;
- erreurs JSON claires ;
- logs detailles ;
- GitHub OTA cote ESP32 non valide actuellement.
