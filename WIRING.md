# Schema de cablage texte

```text
ESP32
|- GPIO16  <- TX JSY-MK-194T
|- GPIO17  -> RX JSY-MK-194T
|- GPIO32  <- TIC Linky via adaptation niveau / optocoupleur
|- GPIO4   <-> Bus OneWire DS18B20 haut, milieu, bas
|            + resistance 4,7 kOhm vers 3V3
|- GPIO26  -> SSR1 chauffe-eau principal
|- GPIO25  -> SSR2 auxiliaire
|- GPIO27  <- RobotDyn zero-cross
`- GPIO33  -> RobotDyn control
```

Les sorties de puissance doivent passer par des interfaces isolees et correctement dimensionnees. Ne jamais connecter directement une charge secteur a l'ESP32.

