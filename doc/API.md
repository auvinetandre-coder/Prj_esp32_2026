Evolution du 21 octobre 2026 : voir [Routage generique](ROUTAGE_GENERIQUE.md) pour les nouveaux champs et la migration.

# API.md - Contrat API RouteurSolaireESP32

Mise a jour du 21 octobre 2026 : roles et redondance supprimes du firmware, des configurations actives, des API et de la WebUI ; onglet Secours retire. Voir [le contrat actuel et la migration](SUPPRESSION_ROLES.md). Les observations datees du 20 octobre ci-dessous sont historiques pour ces points.

Relecture du 20 octobre 2026 : `/api/state` et `/api/status-lite` appellent `sendStatusLite()` dans `src/web/web_ui.cpp`. Les champs historiques à plat coexistent avec `energy`, `pid`, `sensors`, `actuators`, `automate`, `mqttProduction`, `systemHealth` et `performance` ; les préserver. Les exemples sont partiels ; les sections « recommandée » et maintenance expriment aussi des cibles, pas un schéma garanti. Voir le [bilan](ETAT_DU_PROJET.md), l'[audit courant](AUDIT_2026-11-04.md) et les [routes ESP-NOW](ESP_NOW.md#api-présentes-dans-le-main).

## Authentification et validité

Les routes de configuration/commande du main suivent `requireAuth()` et `system.webAuth` ; l'authentification est activée par défaut. Cela ne protège pas les routes de la factory recovery, dont l'authentification reste un défaut ouvert.

Les nombres non finis sont convertis en `null` dans les champs numériques construits par `sendStatusLite()`. Le client doit différencier `null`, zéro et absence de propriété. `pidEnabled` existe à plat ; `pid.enabled` n'est pas rempli actuellement. La commande et `outputPowerW` ne sont pas une preuve électrique d'exécution ; vérifier la source et le caractère estimé/mesuré.

`consumptionW` est la partie positive de la puissance réseau, donc l'achat, et non la consommation totale du logement. La convention et la sélection effective de mesure du PID sont documentées dans [PID unique](PID_UNIQUE.md).

## Objectif

Documenter les API utilisees par la WebUI et la maintenance.

Les API existantes doivent rester compatibles quand elles sont encore appelees par la WebUI.

---

## Endpoint principal

```http
GET /api/state
```

`/api/state` et `/api/status-lite` partagent actuellement le même handler ; conserver cette compatibilité.

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
  "outputPowerW": 600,
  "outputPercent": 48,
  "primaryTemperatureC": 54.2
}
```

---

## pid

```json
{
  "mode": "AUTO",
  "targetGridW": 0,
  "deadbandW": 30,
  "errorW": 350,
  "outputPercent": 48,
  "commandPercent": 48,
  "status": "INJECTION",
  "source": "LOCAL_JSY"
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

- `origin` : `LOCAL`, `ESP-NOW` ou `MQTT` pour le capteur de production ;
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
  "bootPartition": "ota_0",
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
  "bootInfo": "ota_0 @0x120000 size=1966080",
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

## Commandes actionneurs effectivement présentes

`GET /api/actuators/runtime` expose les sorties ; `POST /api/actuator/command` reçoit des arguments formulaire : `id`, `command`, `value` et, facultativement, `controlMode`.

| Requête | Comportement |
|---|---|
| `controlMode=manual`, `value` fini 0..100 | Maintien en RAM d'une cible active ; commande forcée à `setActuatorPercent` |
| Même mode avec `value=0` | OFF maintenu contre les règles ; distinct du mode système OFF |
| `controlMode=auto` | Libération du maintien, commande stop, puis retour à l'arbitrage automatique |
| `controlMode` absent | Demande ponctuelle historique, sans maintien implicite |

Réponse acceptée : HTTP **202**, `{"ok":true,"queued":true}`. Elle indique une demande en attente de l'automate. Erreurs du nouveau chemin : 400 (mode/cible/valeur), 409 (retour AUTO visant une autre cible manuelle), 423 (sécurité critique). Une seule cible peut être maintenue ; pas de persistance du maintien après reboot. L'API ne fournit pas d'acquittement applicatif distant ; `ackStatus` vaut `NOT_SUPPORTED` pour ESP-NOW.

### Diagnostic du mode MULTI_SINUS

Dans `GET /api/actuators/runtime`, une entrée SSR locale en mode `MULTI_SINUS` distingue les champs suivants.

| Champ | Signification |
| --- | --- |
| `commandPercent` | Consigne demandée en pourcentage |
| `outputOn` | État logique courant du GPIO |
| `theoreticalDutyPercent` | Rapport théorique de la trame active en pourcentage |
| `timingFault` | Défaut de cadence verrouillé |
| `missedSteps` | Nombre de pas manqués détectés |
| `maxTickIntervalUs` | Plus grand intervalle observé entre deux pas, en microsecondes |

Les compteurs de cadence sont communs au timer qui pilote les canaux locaux. La puissance estimée repose sur le rapport théorique et la puissance nominale configurée. Voir le [guide SSR](MULTI_SINUS.md).

## Configuration et production

`GET/POST /api/device`, `/api/system`, `/api/sensors`, `/api/actuators`, `/api/rules` lisent/sauvent les JSON correspondants. Les alias POST `/api/config/<nom>` sont conservés. Une liste `rules:[]` avec son enveloppe/version valide est maintenant acceptée. Les options explicites de suppression capteurs/actionneurs et d'effacement Wi-Fi restent nécessaires lorsqu'elles s'appliquent.

La commande `setPowerFromLinky` publie sa cible effective +100 W dans `gridSetpointW` et `pid.targetGridW`, sans réécrire la consigne configurée. Le paramètre optionnel `maxOutputDropPercentPerSecond` est exposé à plat ; défaut 100 points/s pour la branche Linky, montée selon la rampe existante.

`mqttProduction` et le capteur `mqtt_production` sont décrits dans [MQTT_PRODUCTION.md](MQTT_PRODUCTION.md). `sensors[].values` est un objet, notamment `POWER` pour cette production.

## Limites du contrat à corriger

- Certaines erreurs de sauvegarde produisent encore HTTP 200 avec `ok:false`. Le statut HTTP seul ne suffit pas à conclure au succès.
- Les tests locaux vérifient la conversion watts/pourcentage jusqu'à la consigne finale. L'exécution physique reste à vérifier sur banc.
- Le mode système OFF est prioritaire dans l'arbitrage. Le OFF manuel d'une cible agit sur cette cible.
- Les 33 chemins API littéraux relevés dans `app.js` sont enregistrés dans le Main. Ce contrôle statique couvre les chemins.
