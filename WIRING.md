# Schema de cablage texte

```text
ESP32
|- GPIO26  <- TX JSY-MK-194T
|- GPIO27  -> RX JSY-MK-194T
|- GPIO26  <- TIC Linky via adaptation niveau / optocoupleur si source TIC
|- GPIO13  <-> Bus OneWire DS18B20
|            + resistance 4,7 kOhm vers 3V3
|- GPIO5   -> SSR1 chauffe-eau principal
|- GPIO17  -> SSR2 auxiliaire
|- GPIO?   <- RobotDyn zero-cross a renseigner selon PCB
|- GPIO33  -> RobotDyn control
|- GPIO18  -> OLED SSD1309 SCLK
|- GPIO19  -> OLED SSD1309 SDA/MOSI
|- GPIO16  -> OLED SSD1309 RES
|- GPIO4   -> OLED SSD1309 DC
`- GPIO15  -> OLED SSD1309 CS
```

Attention: GPIO26 et GPIO27 sont reserves au JSY sur ce PCB. Ne pas les reutiliser pour SSR ou RobotDyn.
GPIO4 et GPIO15 peuvent influencer le boot selon le montage. Si l'ESP32 ne demarre pas avec l'ecran branche, verifier DC/CS.

Les sorties de puissance doivent passer par des interfaces isolees et correctement dimensionnees. Ne jamais connecter directement une charge secteur a l'ESP32.

## SSR en mode MULTI_SINUS

Utiliser un SSR AC zero-cross avec une charge résistive. Vérifier la tension et le courant de son entrée, la polarité configurée et le niveau OFF au reset. Le moteur utilise l’horloge interne de l’ESP32. L’entrée zero-cross du RobotDyn appartient à son circuit de commande. Voir les [essais sur banc](doc/MULTI_SINUS.md#validation-sur-banc-avant-utilisation).
