# GPIO Lab

Le labo GPIO d'Onyx (`user/Apps/gpiolab`) sur UIKit et GpioKit, sans pont. Une seule vue dessine la fenêtre, les contrôles sont
UIKit, ses enfants. Compilation : `pibuild app /src/apps/gpiolab gpiolab`. Options : `--sim`, `--demo`, `--code`, `--run`, `--tab i2c|edges`.

* **Repris** : calculs du BME280, image de test du SSD1306, hôte BASIC de la vue Code, exemples.
* **Réécrit** : tout l'affichage. Le fond suit les jetons du thème ; la carte, les broches et les niveaux gardent leurs couleurs.
* **Manque** : présentation PocketUI (tiroir, 640 x 480) : fenêtre de 940 x 580 au moins ; marque d'erreur dans la marge (la
  ligne est éclairée) ; griser le segment PWM d'une broche qui n'en fait pas (un message le dit).
* **Essayé sous QEMU, simulateur** : démonstration, chronogramme, I2C, écran, BME280, vue Code exécutée, Givre et Onyx Noir.
* **Pas essayé** : le connecteur réel (service `gpio` : broches sous QEMU seulement ; PWM, I2C, SPI jamais) ; l'onglet Fronts
  avec de vrais fronts ; pas à pas.
