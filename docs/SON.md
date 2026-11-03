# Le son en service — sound, ses pilotes et AudioKit

Le son sort du noyau (feuille de route, section 6). Même découpage que l'USB (`docs/USB.md`) :
**la mécanique** (les pilotes de sortie, un par matériel) est séparée de **la politique** (sound :
mixage, sortie principale, volumes, routage), et les programmes ne voient qu'**AudioKit**.

```
 application ─▶ AudioKit ─▶ anneau du flux ─▶ sound ─────────────▶ anneau de sortie ─▶ pilote ─▶ DMA / usbd
 (Mixer, barre  (décodeurs,   (tampon partagé,  · mélange par sortie  (tampon partagé,     pwmaudio   PWM (jack)
  de menus      synthèse,      s16 stéréo        · volumes, routage    s16 stéréo à la      hdmiaudio  MAI HDMI0
  compris)      conversion)    44,1 kHz)         · sortie principale   fréquence de la      usbaudio   isochrone
                                                 · capture, boucle     sortie)              i2smic     ◀─ PCM (micro)
```

* **sound** (`SD0:/sys/svc/sound`, service « sound ») : un process, deux threads (mélange ; requêtes).
  Il ne touche à **aucun registre** : il tient les flux, les sorties, les volumes, et mélange.
* **Pilotes** : un process par matériel, clients du service « sound ». Ils convertissent le PCM
  mélangé au format du matériel et l'y envoient. Un pilote qui plante ne coupe que sa sortie.
* **AudioKit** (`SD0:/lib/audiokit.so`) : le seul chemin vers le son, pour les applications comme
  pour le Mixer et la barre de menus. Plus aucun appel système de son.

Aucune interruption, comme partout ailleurs : tout est scruté (pilotes et sound : toutes les 5 ms).

**Décisions de l'utilisateur (29/10/2026)**, appliquées ici : le service s'appelle **`sound`**
(`SD0:/sys/svc/sound`, service nommé « sound ») et reste **agnostique du matériel** comme netd ;
**chaque pilote audio vit dans son process** (`pwmaudio`, `hdmiaudio`, `i2smic`, `usbaudio`) et
**s'inscrit auprès de sound** comme sortie ou entrée (`src/svc/inc/sndif.inc`, sur le modèle de
`netif.inc` pour netd : anneau partagé + état + « je suis là » toutes les 2 s) ; noms anglais :
`SD0:/etc/sound.txt`, options `sound=kernel`, `sound_virtual=2`, désactivations en `no_xxx`
(`no_pwmaudio`...) ; AudioKit (`SD0:/lib/audiokit.so`) est le seul chemin des programmes vers
le son. L'avancement réel est au §9.

## 1. Le noyau aujourd'hui (ce qu'on reprend)

| Fichier | Lignes | Contenu | Devient |
|---|---|---|---|
| `src/kernel/arch/pi4/audio.asm` | 975 | flux (8 × 64 Ko), thread « son » (cœur 1), `snd_mix_chunk` (blocs de 1 024 trames, ~23 ms, volume 0..256, saturation), sortie virtuelle cadencée par `time_ms`, PWM1 (GPIO 40/41, 125 MHz / 2 834) par DMA cyclique sur deux tampons de 8 Ko, `resample_s16` (linéaire 16.16), capture (un enregistreur, anneau 64 Ko, boucle), appels 28–35 | mixage, flux, capture → **sound** ; PWM et DMA → **pwmaudio** ; `resample_s16` → sound et AudioKit |
| `audio_hdmi.asm` | 266 | MAI HDMI0 en IEC958, 44,1 kHz, ACR (N = 6 272), InfoFrame audio, DREQ 10 ; actif seulement si le firmware a allumé l'audio (`HDMI_RAM_CFG` bit 16) | **hdmiaudio** |
| `audio_i2s.asm` | 273 | micro INMP441 (GPIO 18–20), PCM maître 2,8224 MHz, DMA DREQ 3, `i2s_poll` dans le thread « son » | **i2smic** |
| `usb_audio.asm` (historique, `git show usb-noyau:…`) | 691 | UAC1 sur DWC2 : analyse des interfaces AudioStreaming, 44,1 ou 48 kHz, un paquet isochrone par ms (thread scruté), capture IN, pas de retour de synchronisation, pas de SPLIT | **usbaudio** (+ isochrone dans usbd) |

La fin d'un DMA se voit **par scrutation** : le thread lit `CONBLK_AD` du canal toutes les 5 ms
et remplit le tampon que le DMA ne lit pas. Les pilotes en espace utilisateur font pareil
(`dev_map`, `dma_alloc`, `fw_call` : appels 87–89). Toutes les zones utiles sont dans
`DEV_ZONE1` (0xFC00_0000–0xFF80_0000) : DMA `0xFE007000`, PWM `0xFE20C800`, horloges
`0xFE101000`, GPIO, PCM `0xFE203000`, HDMI `0xFEF00700`/`0xFEF20000`. **Aucun ajout au noyau
n'est nécessaire.** `mbox_get_fw_rev` (aussi utilisé par `board`, `genet`, `v3d`) reste au noyau.

Utilisateurs actuels des appels 28–35 : `piano`, `recorder` (magnéto), `sontest`.

## 2. Architecture

### 2.1 Pilotes séparés, sound sans registres

| Pilote | Matériel | Lancé par | Arrêté par |
|---|---|---|---|
| `pwmaudio` | PWM1 → prise jack | `services.txt` (non détectable sur le Pi 4) | jamais ; sous QEMU il sort aussitôt (code 0 : init2 ne le relance pas) |
| `hdmiaudio` | MAI HDMI0 | **hwmanager** (écran branché qui annonce le son, §5) | **hwmanager** (`kill`, écran débranché) |
| `usbaudio` | carte son USB (UAC1) | **hwmanager**, `drivers.txt` : `usb:01/01 = SD0:/sys/svc/usbaudio` (l'interface AudioControl : un pilote par carte) | lui-même (`E_NODEV` de usbd au retrait) |
| `i2smic` | micro I2S sur le connecteur | `services.txt`, ligne commentée par défaut (question 4) | jamais |
| *virtuelle* | aucune : sound consomme au rythme de `time_ms` | sound (toujours là) | – |

Pourquoi pas dans sound : hwmanager lance et arrête les pilotes (décision de l'utilisateur), un
plantage reste local, et c'est le modèle d'usbd. Le prix : un anneau de plus (≈ 3 blocs, §2.3).

**Canaux DMA** : deux pilotes qui choisissent seuls « le premier canal Lite libre » prendraient
le même. sound distribue les canaux (masque du firmware, `GET_DMA_CHANNELS`, canaux 7–14) à
l'enregistrement du pilote ; chaque pilote ne touche qu'aux registres de **son** canal, plus le
bit de son canal dans `DMA_ENABLE` (lecture-écriture partagée : risque, §8).

### 2.2 Formats et fréquences

* **Flux** (application → sound) : toujours **s16 stéréo 44,1 kHz** (le format d'aujourd'hui).
  AudioKit convertit dans le process de l'application (mono → stéréo, 8/24/32 bits, autres
  fréquences par son rééchantillonneur) : sound reste simple et le coût est chez l'appelant.
* **Mélange** : à 44,1 kHz, une fois **par sortie active** (accumulateur s32, saturation).
* **Sortie** : à la fréquence du pilote, annoncée à l'enregistrement (PWM et HDMI : 44,1 kHz ;
  USB : 44,1 kHz si la carte la propose, sinon 48 kHz → `resample_s16` dans sound).
* NEON est permis en espace utilisateur (contexte sauvegardé) : `sqadd`/`smull` pour le mélange,
  plus tard.

### 2.3 Anneaux, tailles, latence

Tous les anneaux sont des **tampons partagés** (`buf_create` par sound, `buf_grant`, `buf_map`),
lus sans appel système, comme les anneaux de HIDManager.

| Anneau | Écrit par | Lu par | Taille | En-tête (64 o) |
|---|---|---|---|---|
| flux | AudioKit (+0 tête) | sound (+8 queue) | 64 Ko (~370 ms) | +16 volume du flux 0..256, +24 état (0 ouvert, 1 sortie perdue, 2 fermé par sound), +32 sortie courante, +40 trames jouées (position), +48 manques, +56 type de la sortie |
| sortie | sound (tête) | pilote (queue) | 32 Ko (sound garde 3 blocs d'avance) | +40 trames jouées, +48 manques (comptés par le pilote), +56 fréquence |
| entrée (pilote) | pilote | sound | 64 Ko | +48 pertes, +56 fréquence, +64 « écoutée » (1 : une capture l'écoute, le pilote allume son micro) |
| capture flux | sound | AudioKit | 64 Ko | +16 source effective, +48 pertes |

(Réalisé : l'en-tête occupe une page entière, les données commencent à +4096 ; compteurs d'octets
monotones, constantes `SR_*` de `src/inc/sound.inc`.)

Bloc : **1 024 trames** au départ (comme aujourd'hui), 512 visé (étape 8) si la scrutation tient.

| Étape du chemin | Retard |
|---|---|
| anneau du flux | ce que l'application a d'avance ; AudioKit plafonne à ~2 blocs par défaut (`ak_latency`) |
| sound → anneau de sortie | sound mélange dès qu'un bloc est libre, garde 2 à 3 blocs d'avance (~46–70 ms) |
| pilote → DMA | double tampon : 1 à 2 blocs (~23–46 ms) |
| **total** | **~100–140 ms** (1 024 trames), ~50–70 ms (512) |

**Réveils** : `futex_wake` ne réveille que les threads du process appelant (`syscall.asm` : il
parcourt `P_THREADS`) : pas de réveil entre process. Donc **scrutation** : pilotes et thread de
mélange `sleep 5` quand rien n'est à faire ; AudioKit, s'il doit attendre de la place,
`sleep` 5 ms en boucle. Plus tard, un futex partagé (clé = adresse physique) réduirait la
consommation ; ce n'est pas un préalable.

**Horloges** : chaque pilote consomme au rythme de son matériel ; sound remplit chaque anneau de
sortie selon sa place libre (modèle « tiré »). Deux sorties ne dérivent donc jamais l'une contre
l'autre. Une carte USB sans retour de synchronisation : sound corrige le pas du rééchantillonneur
de ±0,1 % selon le remplissage de l'anneau.

## 3. AudioKit

Kit partagé comme AppKit/UIKit/NetKit (`docs/KITS.md`), table qui ne fait que grandir. Le
modèle est celui d'Onyx (`Onyx/docs/16-AUDIOKIT.md`), réécrit en assembleur, **sans copie**
(réimplémentation d'après les spécifications).

| Groupe | Fonctions | Où |
|---|---|---|
| Flux | `ak_open(fréquence, canaux, bits, sortie)` (sortie 0 = celle du process, donc la principale) → flux ; `ak_write(flux, tampon, octets)` (non bloquant → octets pris) ; `ak_write_all` (attend la place) ; `ak_space`, `ak_pending`, `ak_position`, `ak_status`, `ak_drain`, `ak_latency` ; `ak_volume(flux, 0..256)` ; `ak_close` | kit + sound |
| Capture | `ak_rec_open(source)` (micro, boucle, ou une entrée), `ak_rec_read`, `ak_rec_status`, `ak_rec_close` | kit + sound |
| Mixer | `ak_sorties`, `ak_clients`, `ak_vol_sortie`, `ak_vol_client`, `ak_router`, `ak_principale`, `ak_etat` (numéro de génération, §4.3) | kit → sound |
| Lecteur | `ak_play(chemin, sortie)` (WAV, FLAC, MP3, Ogg Vorbis, Ogg Opus, MIDI, FM Song ; type d'après les premiers octets), `ak_play_pause`, `ak_play_stop`, `ak_play_seek`, `ak_play_pos`, `ak_play_state`, `ak_play_volume`, `ak_play_decoder` (→ `ak_dec_info`, `ak_dec_tag` : ID3, Vorbis, INFO) | kit : **un thread** dans le process de l'application décode et écrit dans le flux |
| Décodage | `ak_dec_open` (fichier), `ak_dec_open_mem`, `ak_dec_read(trames)`, `ak_dec_seek`, `ak_dec_tag`, `ak_dec_close` : WAV, **FLAC**, **MP3** (MPEG-1/2/2.5 couche III), **Ogg Vorbis**, **Ogg Opus** ; `ak_codec_*` : paquets d'un démultiplexeur (MediaKit) | kit |
| Encodage | `ak_enc_open`, `ak_enc_write`, `ak_enc_close` : WAV, **MP3** (CBR), **Ogg Opus** (CELT) ; Vorbis non fait | kit (étape 9) |
| MIDI | `ak_synth_new`, `ak_synth_midi`, `ak_synth_render`, `ak_synth_render_f32` (General MIDI, 16 canaux, 64 voix ; SoundFont 2 de `SD0:/res/soundfonts`, sinon banque FM) ; `ak_sf_*` ; fichiers .mid par `ak_dec_*` et `ak_play` | kit |
| FM | `ak_fm_new`, `ak_fm_instrument`, `ak_fm_start`, `ak_fm_stop`, `ak_fm_render`, `ak_fm_live` : la synthèse de FM Tracker (d'après `fmsynth.h` d'Onyx, même PCM) ; fichiers .fms par `ak_dec_*` et `ak_play` | kit |
| Outils | `ak_resample`, `ak_mix_s16`, `ak_gain_s16`, `ak_volume_curve`, notes (`ak_note_mhz`...), en-tête WAV (`ak_wav_header`) | kit |

Les décodeurs et synthés sont **dans le kit** (une seule copie pour tous ; FMTracker, le lecteur
média, les jeux de gamelib les partagent), et tournent **dans le process de l'application** :
sound ne décode jamais (un MP3 corrompu ne fait tomber que son lecteur). Ce qui ne sert qu'à un
programme (l'éditeur de motifs de FMTracker) reste dans le programme.

**Appels 28–35** : pendant la transition, AudioKit les utilise si le service « sound » est absent
(retour en arrière). À la fin, ils rendent `ENOSYS` et leurs numéros restent réservés (comme
60–65). `sndlib.inc` ne garde que des constantes, puis disparaît au profit d'`audiokit.inc`.

## 4. Le mixer

### 4.1 Modèle

* **Sorties** : numéro (≥ 1), type (virtuelle, jack, HDMI, USB), nom (« Prise jack »,
  « HDMI : <nom EDID> », « USB : <produit> »), fréquence, volume 0..100, muette.
* **Sortie principale** : une des sorties présentes. Préférence enregistrée dans
  `SD0:/etc/sound.txt` (type + nom, et les volumes des sorties). Sans préférence présente :
  USB > HDMI > jack > virtuelle. La virtuelle n'est montrée que si elle est seule.
* **Clients** : un par process qui a au moins un flux (pid, nom donné par AudioKit à
  l'ouverture, volume 0..100, sortie choisie : 0 = suit la principale).
* **Gain d'un flux** = volume du flux (l'application) × volume du client (le Mixer) × volume de
  la sortie, appliqué dans sound, courbe « à l'oreille » (`ak_volume_curve` : le carré du volume).
* **Routage** : `ROUTE(pid, sortie)` déplace **tous** les flux du process ; les nouveaux
  suivent. Un flux ouvert avec une sortie explicite la garde jusqu'au prochain `ROUTE`.
  Bascule au bord d'un bloc : pas de coupure audible au-delà d'un bloc.
* **Sortie qui disparaît** : ses flux passent sur la principale (recalculée si c'était elle),
  état « sortie perdue » dans l'en-tête de l'anneau (l'application peut s'en moquer).

### 4.2 Service « sound »

| Op | Entrée | Réponse | Pour |
|---|---|---|---|
| 1 `SND_OP_OPEN` | a = sortie (0 : celle du client) ; données : nom du programme | résultat = flux, b = tampon partagé | AudioKit |
| 2 `SND_OP_CLOSE` | a = flux | 0 | AudioKit |
| 3 `SND_OP_REC_OPEN` | a = source (0 micro, 1 boucle de la principale, ≥ 2 : entrée n) | flux de capture, b = tampon | AudioKit |
| 4 `SND_OP_STATE` | – | b = tampon d'état (lecture seule), §4.3 | Mixer, barre de menus |
| 5 `SND_OP_OUT_VOLUME` | a = sortie, b = volume, c = muet | 0 | idem |
| 6 `SND_OP_CLIENT_VOLUME` | a = pid (0 : soi-même), b = volume, c = muet | 0 | idem, applications |
| 7 `SND_OP_ROUTE` | a = pid, b = sortie (0 : la principale) | 0 | Mixer |
| 8 `SND_OP_MAIN` | a = sortie | 0 (préférence écrite dans `sound.txt`) | volet, Mixer, applet |
| 20 `SND_OP_DRV_OUTPUT` | a = type, b = fréquence, c = 1 si DMA voulu ; données : nom | sortie, b = tampon (anneau de sortie), c = canal DMA | pilotes |
| 21 `SND_OP_DRV_INPUT` | idem pour un micro | entrée, b = tampon | i2smic, usbaudio |
| 22 `SND_OP_DRV_ALIVE` | a = sortie (ou entrée \| 0x1000) | 0, `E_INVAL` si inconnue (sound relancé : le pilote se réinscrit, en redemandant son canal DMA : c = 0x100 + canal) | pilotes (`sndif_tick`, toutes les 2 s) |

Sources de capture (op 3, `ak_rec_open`) : 0 micro (l'entrée USB s'il y en a une, sinon la
première ; `E_NOSYS` sans micro, comme l'ancien appel 32), 1 boucle de la principale,
0x100 + n boucle de la sortie n, 0x200 + n entrée n.

sound est prévenu des fins de process (`SVF_FINS`) : flux d'une application fermés, sortie d'un
pilote arrêté retirée. Un seul thread sert les requêtes ; le thread de mélange lit les tables
sous un verrou court (le seul partage entre les deux).

### 4.3 État partagé et avis

* **Tampon d'état** (op 4), réécrit par sound : numéro de génération, sorties, principale,
  clients (pid, nom, volume, sortie, crête du dernier bloc pour un vumètre). Le Mixer et la barre
  de menus le relisent toutes les 200 ms et ne redessinent que si la génération a changé (comme
  les menus publiés, `docs/ABI.md`).
* **Avis à l'utilisateur** : sound envoie à notifyd (op 1, `inc/notify.inc`) « Son : HDMI
  branché », « Son : carte USB retirée, son sur la prise jack ». Sans bureau : rien.

### 4.4 Interfaces

| Où | Contenu |
|---|---|
| Barre de menus (`src/sys/menubar`), icône haut-parleur à droite (barrée si muette) | clic : **volet simple** (fenêtre sans cadre, comme une notification) : curseur du volume de la principale, case muet, liste des sorties pour choisir la principale ; clic droit : menu « Avancé... » → lance le Mixer. Réalisé : `src/sys/soundmenu` (comme `wifimenu`), dessins communs dans `src/inc/sounddraw.inc` |
| Application **Mixer** (`src/apps/mixer`) | en haut, une ligne par sortie (étoile = principale, nom, curseur, muet, crête) ; en bas, une ligne par process qui joue : nom, liste déroulante de sa sortie (`ui_dropdown` d'UIKit : Principale ou une sortie précise, `ak_route`), curseur, muet, crête |
| Applet **Son** du panneau | sortie principale préférée, volumes de départ, micro I2S (oui/non), son d'essai |

## 5. Détection

| Sortie | Qui détecte | Comment |
|---|---|---|
| Jack (PWM) | personne | `services.txt` ; `pwmaudio` sort (code 0) sous QEMU (révision du firmware, comme `snd_is_real_pi`) |
| HDMI | **hwmanager**, toutes les 2 s (il scrute déjà l'I2C) | état « hot plug » du bloc HDMI0 (`dev_map` du bloc ; registre HOTPLUG, bit 0, à confirmer sur le PocketTerm) ; au front montant, EDID par le firmware (`fw_call`, tag `0x00030020` GET_EDID_BLOCK, blocs 0 puis 1) : extension CEA (tag 2), octet 3 bit 6 « audio de base » ou un bloc Audio Data → `drivers.txt` : `hdmi0:audio = SD0:/sys/svc/hdmiaudio 0` ; au front descendant, `kill` du pilote |
| USB | **usbd** → hwmanager (op `HW_OP_USB`) | interface classe 01 sous-classe 01 (AudioControl : une annonce par carte ; les AudioStreaming 01/02 n'ont pas de ligne) ; `usbaudio` lit les descripteurs (op 1), choisit l'interface alternative et la fréquence (`SET_INTERFACE`, `SET_CUR`), ouvre le tuyau isochrone |

hdmiaudio ne dépend plus de l'allumage par le firmware : il programme lui-même MAI, ACR et
InfoFrame (c'est déjà le cas dans `hdmi_audio_start`), mais ni la PHY ni le mode vidéo. Le
firmware doit voir un écran **HDMI** (pas DVI) au démarrage (`hdmi_drive=2` sinon) : question 3.

### 5.1 Isochrone dans usbd (préalable de usbaudio)

| Besoin | DWC2 (USB-C ; QEMU) | xHCI (USB-A) |
|---|---|---|
| ouverture | op 8 `ISO_OPEN` (7 était déjà `BULK_NB`) : périphérique, extrémité (bit 7 IN), taille max., intervalle, **octets par ms en 16.16** (176,4 à 44,1 kHz : 176 ou 180 o selon le cumul) → tuyau + tampon partagé en anneau (≥ 32 ms) | idem |
| cadence | un paquet par trame (1 ms FS) sur un canal hôte réservé ; ne tient pas dans la boucle à 1–4 ms d'usbd : **un second thread** d'usbd, propriétaire exclusif des canaux isochrones (registres de canal séparés ; `HAINT`/`GINTSTS` en lecture seulement) | TRB Isoch mis en file **8 à 16 ms d'avance** (SIA), rechargés en vidant l'anneau d'événements : la boucle actuelle suffit |
| capture (micro USB) | canal IN, un paquet par ms, vers l'anneau | TRB Isoch IN |
| limites acceptées | pas d'extrémité de retour (corrigé par sound, §2.3) ; pas de SPLIT (carte FS derrière un hub HS sur le DWC2) | – |
| fermeture | op 9 `ISO_CLOSE` ; fin de process | idem |

usbaudio : contrôle UAC1 (alternatives, fréquence, unité de volume ignorée : le volume est
dans sound), enregistrement auprès de sound, puis recopie anneau de sortie sound → anneau
isochrone usbd toutes les 2–4 ms. UAC2 (cartes USB 2.0 HS récentes) : plus tard (question 6).

## 6. Étapes

Chaque étape démarre, se teste, et se défait (le noyau garde son son tant que l'étape 7 n'est
pas faite : `sound=kernel` dans `SD0:/etc/options.txt` le rallume ; sound, au démarrage, ouvre un
flux par l'appel 28 : s'il réussit, le son du noyau tourne et sound s'arrête proprement).

| # | Étape | QEMU raspi4b | PocketTerm | Retour |
|---|---|---|---|---|
| 1 | **AudioKit v1** sur les appels 28–35 (flux, capture, outils) ; `piano`, `recorder`, `sontest` y passent | sortie virtuelle : `sontest` voit l'anneau se vider au bon rythme | jack/HDMI du noyau inchangés | ancien binaire |
| 2 | **sound** + sortie virtuelle + service « sound » ; AudioKit le préfère | magnéto en source « boucle » enregistre ce que joue le piano : le mélange est vérifié sans haut-parleur | idem (le noyau n'est plus démarré par défaut : `sound=kernel`) | `sound=kernel` |
| 3 | **pwmaudio** (services.txt), canaux DMA distribués par sound | sort aussitôt (code 0) | **jack** (si accessible, question 1) | `sound=kernel` |
| 4 | **Mixer** : volumes, principale, routage, `sound.txt`, avis notifyd, icône et volet de la barre de menus, application Mixer, applet Son ; option `sound_virtual=2` (deux sorties virtuelles, pour essayer le routage) | routage entre deux virtuelles, vérifié par la boucle | idem | ancienne barre |
| 5 | **hdmiaudio** + détection HDMI dans hwmanager | non émulé (jamais lancé : pas d'écran annoncé) | écran du PocketTerm (son ? question 2), téléviseur ; débrancher / rebrancher | ligne retirée de `drivers.txt` |
| 6 | **Capture** : `i2smic` ; micro de la boucle par sortie | boucle seulement | micro I2S si câblé | ligne commentée |
| 7 | **Isochrone dans usbd** (DWC2 puis xHCI) + **usbaudio** (lecture, puis micro USB) | **oui** : `build.ps1 run` passe déjà `-device usb-audio` (DWC2, 48 kHz) | carte USB sur l'USB-A (xHCI) | ligne retirée de `drivers.txt` |
| 8 | **Retrait du noyau** : `audio.asm`, `audio_hdmi.asm`, `audio_i2s.asm`, appels 28–35 → `ENOSYS`, `snd_proc_exit` ; blocs de 512 trames si la scrutation tient | tout l'existant passe | tout l'existant passe | branche `son-noyau` |
| 9 | **AudioKit, formats** (en parallèle dès l'étape 2) : WAV, FM (FMTracker), MIDI + SoundFont, décodage MP3, OGG Vorbis, FLAC, Opus (ajouté le 29/10/2026), puis encodages MP3 et Opus (Vorbis si le temps le permet) | lecture de fichiers via la sortie virtuelle + boucle (comparaison d'empreintes avec un décodage de référence fait sous Windows) | les MP3 de `SD1:` | fonction absente = format refusé |

**Ce qui quitte le noyau** (étape 8) : ≈ **1 460 lignes** (975 + 266 + 273, moins
`mbox_get_fw_rev` et `GET_DMA_CHANNELS` qui restent avec la boîte aux lettres), le thread
« son » du cœur 1, `snd_streams`/`snd_mixbuf`/`snd_pcm16` (~25 Ko de données), 8 appels système.
Le stub `audio_init` du Pi 5 part aussi. (`usb_audio.asm`, 691 lignes, est déjà parti avec la
pile USB.)

Estimation en espace utilisateur : sound ~1 200, pwmaudio ~300, hdmiaudio ~400, i2smic ~300,
usbaudio ~700, isochrone usbd ~400 (DWC2) + ~300 (xHCI), AudioKit cœur ~800, Mixer ~600,
volet ~250. Formats : FM ~600, MIDI + SF2 ~1 500, MP3 ~2 500, Vorbis ~3 000, encodeurs bien plus.

## 7. Dépendances

| De | Pour | Quoi |
|---|---|---|
| Section 4 (fichiers, `docs/FICHIERS.md`) | étape 9 | lecture en continu et `seek` de gros fichiers sur `SD1:` (exFAT) par vfs, sans pause plus longue que l'avance du lecteur (~370 ms) : le thread lecteur d'AudioKit lit par blocs de 32–64 Ko en avance ; SoundFont (plusieurs Mo) chargée une fois ; écriture des encodeurs et du magnéto |
| Section 5 (réseau) | rien | une radio par Internet viendra plus tard par NetKit + les décodeurs, sans rien changer ici |
| usbd isochrone (§5.1) | étape 7 | DWC2 (second thread) et xHCI (TRB Isoch) |
| hwmanager | étape 5 | scrutation HDMI + clés `hdmi0:` dans `drivers.txt` |
| Noyau | rien d'obligatoire | futex partagé (confort) ; `dma_alloc` « mémoire basse » quand toute la RAM sera utilisée (le DMA Lite n'adresse que le bas de la mémoire) |

## 8. Risques et questions ouvertes

Risques :
* **Scrutation et gigue** : un pilote non réveillé à temps vide son double tampon → craquement.
  Parade : 3 blocs d'avance, pilotes à priorité haute si l'ordonnanceur le permet, mesure des
  manques (+48 de l'en-tête) affichée par `sontest`.
* **`DMA_ENABLE` partagé** entre pwmaudio, hdmiaudio et i2smic (lecture-modification-écriture
  concurrente) : chacun ne met que son bit une fois au départ ; sinon sound le fait pour eux.
* **HDMI à chaud** : le firmware ne reprogramme peut-être ni la vidéo ni la PHY après un
  rebranchement ; l'audio d'un écran branché après le démarrage peut ne jamais marcher.
* **Isochrone DWC2** par scrutation : le précédent noyau l'avait fait (QEMU), jamais validé sur
  vrai Pi ; un hub HS devant une carte FS demande les transactions SPLIT.
* **MP3 et OGG en assembleur** : gros travail (virgule fixe ou NEON), surtout les encodeurs ;
  aucune copie de minimp3, stb_vorbis ou libvorbis (réimplémentation propre).

Questions pour l'utilisateur :
1. **PocketTerm** : la prise jack du Pi 4 est-elle accessible dans le boîtier ? Y a-t-il un
   haut-parleur intégré (et sur quoi : jack, HDMI, USB) ?
2. L'**écran HDMI 3,5"** du PocketTerm annonce-t-il du son dans son EDID ? (sinon la seule
   sortie HDMI testable est un téléviseur.)
3. Accepte-t-on `hdmi_drive=2` dans `config.txt` (forcer le mode HDMI, utile si l'EDID est mal lu) ?
4. **Micro I2S** : `i2smic` dans `services.txt` actif par défaut, ou commenté (l'applet Son
   l'active) ?
5. Une sortie **qui se branche** devient-elle principale d'elle-même (comme Onyx pour l'USB), ou
   seulement si c'est la préférée enregistrée ?
6. Volumes **par application** retenus d'une fois sur l'autre (par nom de programme dans
   `sound.txt`) ou remis à 100 à chaque lancement ? Routage retenu aussi ?
7. **Encodage MP3 et OGG** : nécessaire dès le départ (magnéto en MP3 ?) ou après les
   décodeurs ? Débit visé (128 kb/s CBR suffit-il) ?
8. **SoundFont** : reprendre GeneralUser GS d'Onyx (licence libre, quelques Mo dans
   `SD0:/res/soundfonts`) ou une banque plus petite ?
9. Latence : ~100 ms convient-il au piano et à FMTracker en jeu direct, ou faut-il viser
   les blocs de 512 trames dès l'étape 3 ?
10. UAC2 (cartes USB 2.0 récentes, casques) : nécessaire, ou UAC1 suffit pour l'instant ?

Réponses de l'utilisateur (29/10/2026) :
1. Le PocketTerm a une **prise jack** accessible : sortie analogique (PWM) à prendre en charge.
2. L'écran HDMI du PocketTerm sort le son : sous Onyx, le son sort quand on choisit HDMI.
3. `hdmi_drive=2` dans `config.txt` : accepté.
4. **Micro I2S** : à la demande (`i2smic` commenté dans `services.txt`, l'applet Son l'active).
5. **Sortie principale**, par ordre de priorité, la première présente : **préférée** enregistrée,
   puis **USB**, puis **HDMI**, puis le jack (**PWM**). Recalculée à chaque branchement ou
   retrait : une sortie qui arrive prend la main si elle passe avant la sortie actuelle (préférée
   absente, son en HDMI, casque USB branché : le son passe sur le casque ; casque retiré : retour
   en HDMI).
6. **Volumes par application et routage** : mémorisés tous les deux (par nom de programme). Une
   application routée vers une sortie absente passe sur la sortie par défaut.
7. **Encodage MP3 et OGG** : après les décodeurs ; 128 kb/s CBR suffit.
8. **SoundFont** : GeneralUser GS, reprise d'Onyx.
9. **Latence** : ~100 ms convient pour l'instant.
10. **USB Audio** : classe 1 (UAC1) d'abord ; casque de l'utilisateur : Poly Blackwire (USB).

## 9. Réalisation (état des étapes)

| # | État | Essai |
|---|---|---|
| 1 | **fait** : `src/kits/audiokit` (21 fonctions), `src/inc/sound.inc` ; piano, recorder, sontest passent par AudioKit (`sontest <fréquence>` : flux mono, conversion et rééchantillonnage) | QEMU : `sontest` 202 860 trames à ~44 300 trames/s ; `recorder --test` (boucle) pendant `sontest` : crête 9 999 |
| 2 | **fait** : `src/svc/sound` (sound.asm, mix.inc), sortie virtuelle, AudioKit préfère le service ; le son du noyau ne démarre plus qu'avec `sound=kernel` (appels 28–35 → `E_NOSYS` sinon), et sound s'arrête alors (code 0) | QEMU : deux `sontest` (stéréo 44,1 kHz et mono 22 050 Hz) + `recorder --test` en boucle : crête 19 997 (les deux flux mélangés) ; `sound=kernel` : chemin 2, crête 9 999 |
| 3 | **fait** : `src/svc/pwmaudio` (PWM1, GPIO 40/41, DMA cyclique sur le canal donné par sound ; services.txt ; sort aussitôt sous QEMU ou avec `no_pwmaudio`) ; canaux DMA « lite » 7–14 distribués par sound (masque du firmware, au journal ; un pilote qui se réinscrit redemande le sien) ; `sontest driver [fréq] [s]` : faux pilote pour essayer `sndif.inc` sous QEMU | QEMU : `pwmaudio : pas de PWM (QEMU)` (code 0) ; faux pilote USB à 48 kHz devenu principal : 47 982 trames/s lues, crête 9 999 du `sontest` rééchantillonné ; second faux pilote : non principal (crête 0) ; à son départ la principale revient à la virtuelle ; `kill` de sound : init2 le relance, le faux pilote se réinscrit (~1,9 s de silence). Masque DMA de QEMU : 0x3C (aucun canal lite : canal -1, sans effet pour le faux pilote) |
| 4 | **fait** : sound : ops 5–8 (volume d'une sortie, d'une application, routage par process, sortie préférée), `SD0:/etc/sound.txt` (`main =`, `output = volume, muet, nom`, `app = programme, volume, muet, sortie` ; écrit une seconde après un changement ; une application routée vers une sortie absente joue sur la principale et y revient), avis à notifyd (HDMI et USB, après les 15 premières secondes) ; AudioKit 26 (`ak_state`, `ak_out_volume`, `ak_client_volume`, `ak_route`, `ak_main`) ; `soundctl` (le mixer en ligne de commande) ; barre de menus : haut-parleur (barré si muet), clic → volet `soundmenu` (volume, muet, choix de la principale, « Mixer… »), clic droit → « Avancé… » ; application **Mixer** (`src/apps/mixer`) ; applet **Son** du panneau (préférée, volume, micro I2S dans services.txt, son d'essai) ; `sound_virtual=2` | QEMU (`sound_virtual=2`) : `soundctl route sontest 2` → la boucle de la sortie 2 (`recorder --test 258`) : crête 9 999 ; route retenue au lancement suivant ; `soundctl app sontest 50` → crête 2 500 (courbe au carré) ; sortie muette → crête de la sortie 0 ; `soundctl main 2` → principale 2, retenue ; volet, menu « Avancé… », Mixer (curseur glissé à 49 %, routage par clic) et applet vérifiés par captures |
| 5 | **fait (non testable sous QEMU)** : `src/svc/hdmiaudio` (MAI HDMI0, ACR N = 6 272 et CTS d'après l'horloge pixel du firmware, InfoFrame audio, IEC958, DMA cyclique DREQ 10 sur le canal donné par sound ; nom « HDMI : <nom EDID> ») ; hwmanager : au démarrage sur un Pi 4, EDID par le firmware (bloc 0, extension CEA du bloc 1 : audio de base ou bloc Audio Data) → `hdmi0:audio = SD0:/sys/svc/hdmiaudio 0` (drivers.txt) ; toutes les 2 s, registre HOTPLUG de HDMI0 (`0xFEF00700 + 0x1A8`, bit 0, d'après Linux : **à confirmer**) : débranché → `kill` du pilote, rebranché → EDID relu ; option `no_hdmiaudio` ; `hdmi_drive=2` dans config.txt | QEMU : hwmanager ne surveille rien (émulateur) ; `hdmiaudio 0` lancé à la main sort aussitôt (code 0) |
| 6 | **fait (micro I2S non testable sous QEMU)** : `src/svc/i2smic` (bloc PCM maître 2,8224 MHz, DMA DREQ 3 sur le canal donné par sound, gauche dupliqué en s16 stéréo) ; essai de présence au départ (ligne muette : il sort, code 0) ; matériel allumé seulement quand une capture l'écoute (`SR_WANTED`), éteint 1 s après ; ligne `# SD0:/sys/svc/i2smic` en commentaire dans services.txt (l'applet Son la met ou l'enlève) ; capture côté sound : micro = l'entrée USB s'il y en a une, sinon la première, rééchantillonnée à 44,1 kHz ; boucle par sortie (`0x100 + n`, étape 4) ; `sontest mic [fréq] [s]` : faux micro | QEMU : faux micro à 48 kHz + `recorder --test 0` (micro) : crête 8 192 (le triangle du faux micro), 2,5 s enregistrées ; `i2smic` sort aussitôt (code 0) |
| 7 | **fait (xHCI non testé)** : usbd `iso.inc` : ops 8 `ISO_OPEN` / 9 `ISO_CLOSE`, anneau partagé `UI_*` ; DWC2 : second thread (canaux 3 OUT et 4 IN, un paquet par ms, 4 ms d'avance) ; xHCI : TRB Isoch (SIA, IOC) gardés 12 ms en file, rechargés à chaque événement (CErr 0 et ESIT = MPS dans le contexte d'extrémité) ; `src/svc/usbaudio` (UAC1 : AudioStreaming PCM 16 bits, 44,1 kHz sinon 48 kHz, `SET_INTERFACE`, `SET_CUR`, nom « USB : <iProduct> », sortie de sound, micro ouvert à la première capture, retrait → sortie 0) ; drivers.txt `usb:01/01 = SD0:/sys/svc/usbaudio` | QEMU (`usb-audio` sur le DWC2, 48 kHz) : « USB : QEMU USB Audio » devient la principale ; `sontest` 202 860 trames à la cadence normale ; `build/out.wav` (sortie de QEMU) : la gamme 261 → 523 Hz (300 ms par note) puis la 440 Hz pendant 1,9 s, crête 9 411, aucun trou ; capture USB non testable (QEMU n'a pas de micro USB) |
| 8 | **fait** : branche de retour `son-noyau` (le point de départ, 9195476) ; `audio.asm`, `audio_hdmi.asm`, `audio_i2s.asm` retirés (`mbox_get_fw_rev` passe dans `arch/common/mailbox.asm`), appels 28–35 → `E_NOSYS` (numéros réservés), `snd_proc_exit` et les stubs du Pi 5 retirés, `inc/sndlib.inc` retiré ; AudioKit n'a plus de chemin noyau (`ak_backend` : 1 ou 0), l'option `sound=kernel` n'existe plus. Blocs de **1024** trames gardés : la tenue de la scrutation à 512 n'est mesurable que sur le PocketTerm | QEMU : tout l'existant passe (sontest, sontest 22050, recorder en boucle : crête 19 998 avec deux flux, carte USB principale) ; fstest2, vfstest, pipetest, kittest (« ancien open du noyau : E_NOSYS »), ping, rngtest |
| 9 | **fait (formats audio, 29/10/2026 ; MIDI et FM : 30/10/2026, ci-dessous)** : dans AudioKit, en assembleur, d'après les normes (les décodeurs C d'Onyx ne servent que de référence d'essai sur le PC) ; code commun au kit et au banc du PC dans `src/kits/audiokit/ak_*.inc` (`ak_codecs.inc`). **Décodage** : WAV (8/16/24/32 bits, flottant), FLAC (RFC 9639), MP3 (MPEG-1/2/2.5 couche III, Xing, ID3), Ogg Vorbis (Vorbis I, plancher 1), Ogg Opus (RFC 6716 : CELT, SILK, hybride ; RFC 7845 : familles 0 et 1) ; FFT radix 2/3/5 et IMDCT communes (Vorbis, Opus), NEON pour l'IMDCT et la synthèse MP3 ; recherche (exacte sauf MP3 à débit variable), durée, étiquettes. **Encodage** : WAV, MP3 (CBR), Ogg Opus (CELT, CBR). **API** : `ak_dec_*`, `ak_play*` (thread du process), `ak_codec_*` (paquets, pour MediaKit), `ak_enc_*` (docs/KITS.md, version 53). Programmes : `codectest`, `play`, application **Musique** ; le magnétophone enregistre en WAV, MP3 ou Opus | PC (`tools/audiotest/run.py`, QEMU virt + semihosting, références minimp3, stb_vorbis, dr_flac, libopus) : FLAC et SILK au bit près, WAV exact, MP3 / Vorbis / CELT / hybride à 1 LSB (RSB > 100 dB) ; trames MP3 aléatoires valides (`mp3fuzz.py` : blocs courts/mixtes, M/S, intensité, LSF) à 1 LSB en suites de blocs permises ; API par paquets identique (`run.py pkt`). QEMU raspi4b (`codectest`, fois le temps réel, TCG) : FLAC 44,1 kHz stéréo 83, MP3 128 kbit/s stéréo 16, MP3 16 kHz mono 53, Vorbis mono 36, Opus CELT 64 kbit/s stéréo 17, Opus SILK 69 ; sorties extraites de la carte identiques au PC. Encodeurs (signal d'essai à bouffées de bruit) : MP3 128 kbit/s RSB 16 dB, 192 : 20 dB ; Opus 96 kbit/s : 12 dB (libopus en CELT : 12,1) |
| 9 bis | **fait (30/10/2026)** : `ak_fm.inc` (d'après `fmsynth.h` d'Onyx), `ak_synth.inc` (MIDI : SoundFont 2 d'après la spécification 2.04, banque FM `ak_gmbank.inc`), `ak_midi.inc` (.mid, .fms par `ak_dec_*`) ; API : docs/KITS.md, version 81 ; `synthtest` (auto-hébergé) ; Musique, `play`, `codectest` prennent MIDI et FM Song ; SoundFont GeneralUser GS copiée par `build.ps1` depuis Onyx (32 Mo, hors de git) | QEMU `synthtest` : PASS ; FM : CRC du PCM égal à `fmsynth.h` compilé sur le PC (`tools/audiotest/fmref.cpp`), ondes simples et FM ; SoundFont : `outer_wilds.mid` comparé à MeltySynth d'Onyx (`midiref.cpp`, `midicmp.py`) : niveau -0,3 dB, enveloppes (50 ms) corrélées à 0,997, spectre par tiers d'octave à ± 1,0 dB ; vitesse (QEMU TCG) : MIDI SoundFont 13 fois le temps réel, FM Song 33 fois ; lecture de la SoundFont 40 s sous QEMU (service de fichiers émulé) |

### Ce qui ne se vérifie que sur le PocketTerm

| Quoi | Comment le vérifier (journal : `SD0:/bin/journal` dans startup.txt → `SD0:/journal.txt`, ou telnet) |
|---|---|
| Prise jack (PWM) | journal : `pwmaudio : PWM1 (prise jack), 44,1 kHz, canal DMA N, sortie M` et `sound : sortie M « Prise jack » (pilote P), 44100 Hz, canal DMA N` ; la ligne `sound : en service (..., canaux DMA du firmware 0x....)` donne le masque ; `sontest` au casque branché sur le jack ; `soundctl` : crête de la sortie non nulle pendant la lecture, manques (`sontest`) |
| HDMI | journal : `hwmanager : HDMI0, hot plug 1 ; son de l'écran : oui (EDID) -> SD0:/sys/svc/hdmiaudio 0`, puis `hdmiaudio : HDMI0 (IEC958), 44,1 kHz, canal DMA N, sortie M (audio allumé par le firmware : 0/1)` ; débrancher / rebrancher : `hwmanager : HDMI0 débranché : pilote du son arrêté` puis `branché` (si le registre HOTPLUG 0x1A8 est le bon : sinon rien ne change au débranchement, à noter) ; `no_hdmiaudio` pour s'en passer |
| Micro I2S | applet Son → « Micro I2S » → redémarrer ; journal : `i2smic : micro I2S (INMP441)...` ou `pas de micro I2S (ligne muette)` ; `SD0:/apps/recorder.app/main --test 0` (micro) : crête non nulle en parlant |
| Casque USB (Poly Blackwire, USB-A, xHCI) | journal : `hwmanager : usb N.0 (01/01/00) -> SD0:/sys/svc/usbaudio`, `usbaudio : USB : <produit>`, `usbaudio : lecture, 48000 Hz` (ou 44100), `sound : sortie principale : ... « USB : ... »` ; `sontest` au casque ; `soundctl` ; micro du casque : `recorder --test 0`, journal `usbaudio : capture ouverte` ; retirer le casque : `usbaudio : carte retirée`, `sound : sortie N partie`, la principale revient au HDMI ou au jack (avis « Son : ... retiré, son sur ... ») |
| Latence, craquements | `sontest` (cadence, manques), écoute du piano |
| Synthèse MIDI | `codectest <fichier>.mid` : temps de lecture de la SoundFont et vitesse ; `play <fichier>.mid` à l'écoute |
| Formats (étape 9) | `codectest SD0:/res/audiotest/<fichier>` : vitesse réelle (attendu : bien plus que sous QEMU, au moins 50 fois le temps réel en MP3 / Opus stéréo) ; `play` d'un long MP3 / FLAC de `SD1:` (lecture continue sans trou pendant que le fichier est lu par blocs de 256 Ko) ; application Musique au tactile (liste, glissières) ; magnétophone au micro en MP3 et Opus, fichiers relus par `play` |

### Risques

* **xHCI isochrone** jamais exécuté (QEMU n'a pas de PCIe) : contexte d'extrémité (CErr 0, ESIT),
  TRB Isoch avec SIA, rechargement par événement ; la boucle d'usbd bloquée par un long transfert
  bulk (clé USB) vide la file de 12 ms : craquements.
* **Configuration USB tronquée à 464 octets** (op 1 d'usbd) : un casque à configuration longue
  (HID + plusieurs interfaces audio) peut perdre ses interfaces AudioStreaming : journal
  `usbaudio : pas de flux PCM 16 bits`.
* **HOTPLUG** à `0xFEF00700 + 0x1A8` d'après Linux : à confirmer ; s'il est faux, seule la
  détection au démarrage marche.
* **`DMA_ENABLE` partagé** (lecture-modification-écriture par chaque pilote au départ) ; pilote
  tué (`kill` de hdmiaudio) : son DMA cyclique continue de lire ses deux tampons (son répété
  jusqu'à la coupure du bloc) ; pour le HDMI, l'écran est déjà parti.
* **Scrutation** : pilotes et mélange toutes les 5 ms, DWC2 isochrone toutes les ms.
* Une session telnet d'essai a une fois attendu son `echo @FIN` (trois sessions en parallèle) :
  sans lien apparent avec le son, à surveiller.

### Revenir en arrière

* Tout : la branche `son-noyau` (le point de départ, son du noyau compris), ou annuler les
  commits « Son, étape N » un à un, dans l'ordre inverse.
* Sur la carte : commenter `SD0:/sys/svc/pwmaudio` (ou `no_pwmaudio`), `no_hdmiaudio`, retirer
  la ligne `usb:01/01` de `drivers.txt`, laisser `i2smic` en commentaire.
