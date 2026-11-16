# Le son du PC : pilote Intel HDA (étude du 13/11)

But : le son sur Korin PC (HP ZBook Fury G8), avec le même découpage que le Pi (`docs/SON.md`) :
AudioKit → service `sound` (mélange, volumes, sortie principale) → un **pilote par matériel**
inscrit auprès de sound (`sndif`). Le pilote nouveau s'appelle **`hda`** (`src/svc/hda`, C, PC).

## 1. Le matériel du ZBook (journal pcid du 13/11)

| Fonction | Identifiant | Classe | Contenu |
|---|---|---|---|
| `0000:00:1f.3` | `8086:43c8` | **04/01/00** (multimédia) | contrôleur audio du PCH Tiger Lake-H : **cAVS** (DSP « SST/SOF » + contrôleur HDA). BAR0 16 Ko (registres HDA), BAR4 1 Mo (le DSP), MSI 64 bits. Codecs attendus sur le lien HDA : **Realtek ALC285** (adresse 0 : haut-parleurs, prise casque combinée) et le codec **HDMI/DP d'Intel** (adresse 2, alimenté par le pilote d'affichage) |
| `0000:01:00.1` | `10de:10fa` | 04/03/00 (HDA) | le HDA de la NVIDIA T1200 : un codec HDMI/DP NVIDIA |

La classe 04/01 (et non 04/03) dit que le micrologiciel a choisi le **mode DSP** : sous Linux,
`intel-dspcfg` charge alors SOF, parce que la machine a des **micros numériques (DMIC)** branchés
sur le PCH : ils ne passent que par le DSP. Les registres HDA de BAR0 restent ceux de la norme
(c'est ce qu'utilise `snd_hda_intel` avec `dsp_driver=1`).

## 2. Sortir le DSP du chemin (« mode HDA historique »)

Le cAVS est un contrôleur HDA normal augmenté de **capacités étendues** chaînées à partir du
registre `LLCH` (BAR0 + 0x14 ; chaque en-tête : version 31:28, identifiant 27:16, suivant 15:0) :

| Id | Nom | Ce qu'on en fait |
|---|---|---|
| 1 | GTS (horodatage) | rien |
| 2 | **ML** (liens) | lien 0 à +0x40 : `LCAP` +0, `LCTL` +4 (`SPA` bit 16 : allumer, `CPA` bit 23 : allumé, `SCF` 3:0 : horloge du lien). On vérifie que le lien est allumé (SPA = CPA = 1), sinon on l'allume et on attend CPA. Linux change aussi `SCF` quand il vaut 0 (horloge de 6 MHz) : **pas fait**, valeurs au journal |
| 3 | **PP** (processing pipe) | `PPCTL` +4 : `GPROCEN` bit 30 (le DSP s'intercale entre le DMA de l'hôte et le lien), `PIE` bit 31, `PROCEN(n)` bits 0–29 (flux « découplés »). **On les met à 0** : DMA et lien couplés, le DSP n'est plus sur le chemin. Une remise à zéro du contrôleur (CRST) les remet aussi à 0 |
| 4, 5 | SPB, DRSM | rien |

Le DSP lui-même (BAR4 : `ADSPCS`, son micrologiciel) **n'est jamais touché** : il reste éteint.
En plus, comme Linux pour le Skylake et après (fonctions décrites, réécrites) : dans la config PCI,
`CGCTL` (0x48) bit 6 `MISCBDCGE` coupé pendant la remise à zéro du contrôleur puis remis ;
`TCSEL` (0x44) bits 2:0 à 0 ; `DEVC` (0x78) bit 11 (« no snoop ») à 0 : DMA cohérent avec les
caches (la mémoire de `DMA_ALLOC` du PC est en cache).

## 3. Le pilote `hda`

* **Lancement** : hwmanager, `drivers.txt` : `pci:04/03 = /sys/svc/hda` (HDA standard :
  QEMU `intel-hda`, la NVIDIA) et `pci:8086:43c8 = /sys/svc/hda` (le cAVS du Tiger Lake en
  classe 04/01 ; une clé précise, la classe 04/01 couvre d'autres cartes). Un pilote par
  contrôleur.
* **Contrôleur** : `PCI_OP_CLAIM`, BAR0, remise à zéro (CRST 0 puis 1, 521 µs au moins pour les
  codecs), `STATESTS` (codecs présents), CORB (256 entrées) et RIRB (256) en DMA ; repli sur
  l'interface de commande immédiate (`ICOI`/`IRII`/`ICIS`) si le CORB ne répond pas ; MSI
  (`pci_irq_setup`) pour la fin de période du flux et les réponses non sollicitées, sinon
  scrutation toutes les 5 ms.
* **Codec** (générique, d'après la norme HDA 1.0a) : groupe de fonctions audio (AFG), D0,
  widgets (`AUDIO_WIDGET_CAP`), pins (`CONFIG_DEFAULT` : connectivité, appareil, lieu),
  listes de connexions. Chemin de sortie cherché en profondeur (5 étages au plus) du pin vers un
  DAC analogique : sélecteurs réglés (`SET_CONNECT_SEL`), entrée voulue des mixers ouverte et les
  autres **fermées** (pas de retour du micro dans les haut-parleurs), amplis à 0 dB et ouverts,
  widgets en D0, `EAPD` sur les pins qui l'ont. Haut-parleurs (appareil 1, ou sortie de ligne
  fixe), casque (2), sortie de ligne en prise (0). Tous les DAC du chemin reçoivent **le même
  flux** (étiquette 1) : un seul flux de sortie suffit.
* **Prise casque** : pins à détection de présence (`PIN_CAP` bit 2) : `SET_UNSOLICITED_ENABLE`,
  `GCTL.UNSOL` ; à l'événement (et toutes les 500 ms par sûreté), `GET_PIN_SENSE` : casque
  présent → pin casque allumé (`OUT_EN | HP_EN`), haut-parleurs coupés (contrôle du pin à 0) ;
  retiré → l'inverse. Au journal.
* **Flux de sortie** : premier descripteur de sortie (après les `ISS` d'entrée), BDL de 4
  périodes de 512 trames (16 bits stéréo), `IOC` à chaque période ; fréquence 44,1 kHz si le DAC
  la propose (sound n'a rien à rééchantillonner), sinon 48 kHz. Chaque période jouée (lue par
  `LPIB`) est remplie de nouveau par `sndif_read` (silence s'il manque des trames, compté par sound).
* **Entrée** : pin micro en prise (appareil 0xA), sinon micro fixe, sinon entrée de ligne ; chemin
  vers un ADC (le même parcours), descripteur d'entrée, étiquette 2 ; inscrite auprès de sound
  (`sndif_input`) ; DMA lancé seulement quand une capture écoute (`SR_WANTED`).
* **Volume** : le volume reste **dans sound** (logiciel, comme le Pi : Mixer, volet, applet
  inchangés). Les amplis du codec sont mis à 0 dB (le pas `offset` de leurs capacités) et ouverts.
  **Volume de départ modéré** : le pilote demande à sound un volume de départ de 50 (gain 0,25,
  −12 dB) pour sa sortie quand `sound.txt` n'en retient pas (nouveau champ `d` de
  `SND_OP_DRV_OUTPUT`, ignoré par l'asm du Pi) ; option `hda_volume=N` (0–100).
* **Type** : la sortie s'inscrit comme `SND_OUT_PWM` (1, « sortie analogique, prise jack ») et
  l'entrée comme `SND_IN_I2S` (2, « micro hors USB ») : même rang que la prise jack du Pi (l'USB
  passe devant). Nom : « Analogique (ALC285) ».

## 4. Les particularités des ALC285 de HP (décrites, réécrites ; rien de copié)

D'après la table des corrections de Linux pour ce modèle (sous-système `103c:8870` Fury 15,6" G8,
`103c:886d` Fury 17,3" G8) :

* **Coefficient d'initialisation de l'ampli** : 100 ms après l'initialisation du codec, le
  coefficient Realtek **0x65 = 0** (coefficients : widget 0x20, `SET_COEF_INDEX` puis
  `SET_PROC_COEF`). Sans lui, les haut-parleurs peuvent rester muets.
* **LED** : LED « muet » sur le GPIO 2 (masque 0x04) et LED « micro coupé » sur le GPIO 0
  (masque 0x01) du codec : mises en sortie, éteintes (données 0).
* **EAPD** (générique des ALC285) : coefficient 0x36 bit 13 et coefficient 0x10 bit 9 mis à 0,
  puis `EAPD` allumé sur les pins de sortie.
* **Non fait** : l'étalonnage de l'ampli du casque (`alc285_hp_init` de Linux : une suite de
  coefficients 0x58, 0x4a, 0x67… avec des attentes, contre le « pop ») et le mode casque-micro
  combiné (coefficients 0x48, 0x49, 0x44, 0x63). À faire si le casque reste muet ou craque.
* Option `hda_quirks=0` : aucune de ces corrections (codec générique pur).

## 5. Ce qui reste hors de portée

* **Micros numériques (DMIC)** du ZBook : branchés sur le PCH, lus seulement par le **DSP** (SOF :
  micrologiciel signé d'Intel, topologie, IPC). Hors de cette étape ; il faudrait un pilote du DSP.
  Le micro de la prise casque passe, lui, par l'ALC285.
* **HDMI/DP d'Intel** (codec 2 du même lien) : il n'apparaît qu'une fois l'audio de l'affichage
  allumé par le pilote graphique (« power well » audio, `intelgpu`) ; il demande ELD, InfoFrame,
  canaux. Plus tard.
* **HDMI de la NVIDIA** : le contrôleur HDA est standard (le même pilote le prend : énumération,
  pins numériques au journal), mais le son ne sort que si l'**affichage NVIDIA** envoie un signal
  HDMI, ce que Korin ne programme pas (pas de pilote NVIDIA). Le pilote voit qu'aucune sortie
  analogique n'existe et s'arrête sans erreur (code 0 : hwmanager ne le relance pas).
  Linux règle en plus, pour la NVIDIA, la config PCI 0x4e (bits 3:0) et 0x4c/0x4d (cohérence des
  flux) : faits aussi, sans effet sans affichage.
* Mise en veille du codec, gestion d'énergie D3, réglage de l'horloge du lien (`SCF`).

## 6. sound sur le PC

Le service `sound` n'existait qu'en assembleur (Pi). Il est **repris en C** (`src/svc/sound/sound.c`,
pour les deux machines) pas à pas d'après `sound.asm`, `mix.inc`, `settings.inc` : même protocole,
mêmes anneaux, même tampon d'état, même `sound.txt`, même sujet `korin.audio.state` du bus. Le Pi
garde l'asm (le C n'y remplace rien aujourd'hui) ; sur le PC, `skel/pc/etc/services.txt` lance
`/sys/svc/sound`. Pas de canaux DMA du micrologiciel sur le PC (demande de canal → -1).

## 7. Essais

* **QEMU q35** : `-device intel-hda -device hda-duplex,audiodev=snd0 -audiodev wav,id=snd0,path=…` :
  `sontest` (gamme puis 440 Hz) → le wav de QEMU : fréquence et niveau relus (FFT), attendus
  ≈ 10 000 × gain du volume 50 (0,25) ≈ 2 500 de crête.
* **ZBook** : journal (`/journal.txt`) et écoute, volume de départ modéré (§3).
