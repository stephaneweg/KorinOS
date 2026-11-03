# 06 — Le GPU et l'affichage dans Onyx : inventaire et transposition en asm pour ARMPI

> Étude en lecture seule de `le depot d'Onyx` (OS C/C++ sur Circle, Pi 4, 2 110 commits), fin oct. 2026.
> Objectif : ce qu'Onyx a **réellement validé sur matériel** côté affichage/GPU, et comment le
> transposer en assembleur FASMARM procédural (sans POO) pour ARMPI.
>
> **Sources de Circle.** Dans `$ONYX/circle`, le sous-module est **vide** (non extrait).
> Les sources du fork (`stephaneweg/circle`, branche `onyx`) ont été lues dans la copie extraite
> `le depot d'Onyx-PocketTerm\circle` (son historique a les commits « Onyx: … »). L'upstream de
> comparaison est `./ref\circle` (Step51.1.1). Les patchs eux-mêmes sont décrits dans
> `Onyx\docs\05-CIRCLE-CHANGES.md` (§6, §12, patchs 31/32 Pi 5) et `Onyx\docs\circle-upstream\0004-*.patch`.

---

## 0. Résumé exécutif

| Domaine | Ce qu'Onyx fait réellement | Validé sur Pi 4 ? |
|---|---|---|
| Framebuffer HDMI | Mailbox (Circle `CBcmFrameBuffer`), 32 bpp, résolution au boot (`cmdline width/height`) et **à chaud** (réallocation complète) | Oui |
| Double buffer / page flip | **Désactivé** (`C2DGraphics(…, FALSE)` : VSync off). Un tampon fantôme en RAM cachée + copie **DMA 2D** des rectangles sales vers le FB. Pas de vsync, pas d'IRQ vsync | Oui (choix délibéré dès le 2026-06-22) |
| HVS (plans, display lists, curseur matériel) | **Rien.** Aucun accès au HVS ; curseur logiciel composé par le serveur graphique. Les tags mailbox curseur existent dans Circle mais ne sont pas utilisés | — |
| DMA | DMA legacy 2D avec **stride source** (patch Circle 12), démarré puis **sondé** (pas d'IRQ : perdue sous forte charge GPU) | Oui |
| Compositeur / WM | Logiciel, en espace utilisateur (Elegant), dirty-rects ≤ 16, occlusion opaque, alpha par pixel, **sans NEON** (C scalaire + `memcpy`) | Oui |
| V3D 4.2 | **Pilote noyau maison au niveau registre** (pas de Mesa runtime, pas de mailbox mem_alloc, pas de MMU V3D) : binner + renderer CL, shaders QPU écrits à la main (assembleur maison) ou générés, textures tuilées, IRQ SPI 74, mémoire ARM physique < 1 Go | Oui (teapot 1,0 ms, gpudemo 0,6 ms, composite 0,65 ms, OoT 50,9 i/s, Wind Waker) |
| Compute (CSD) | **Non utilisé** | — |

**Conclusion pour ARMPI.** Onyx fournit une recette V3D complète, sans dépendance firmware autre que
l'alimentation, déjà en « style registre » (écritures `write32` + paquets binaires + mots QPU
précompilés) : c'est l'élément **le plus directement transposable** en asm. À l'inverse, Onyx n'a
**rien** validé sur le HVS ni sur le page flip / vsync : le palier 2 de la feuille de route sera
un terrain neuf, sans filet Onyx.

---

## 1. Framebuffer HDMI

### 1.1 Fichiers

| Fichier | Lignes (wc -l) | Rôle |
|---|---|---|
| `Onyx-PocketTerm\circle\lib\bcmframebuffer.cpp` (fork) | 541 (upstream 464) | Tags mailbox, `SetArea*` (DMA), `SetVirtualOffset`, `WaitForVerticalSync` |
| `…\circle\include\circle\bcmframebuffer.h` | 130 | |
| `…\circle\lib\2dgraphics.cpp` (fork) | 683 | `C2DGraphics` : tampon fantôme, `UpdateDisplay*`, `Resize` |
| `Onyx\kernel\kernel.cpp` | 2 447 | l. 1705-1730 (construction, `OPT_W/OPT_H`), l. 2209-2246 (options cmdline, `Initialize`), l. 576-745 (tâche d'affichage, présentation, redimensionnement) |
| `Onyx\kernel\sys\wsrv.cpp` | 1 007 | l. 371-398 `Present` : rectangle du serveur → tampon fantôme → DMA |
| `Onyx\kernel\sys\kapi.cpp` | 2 576 | l. 455-478 `MapScreen`, l. 2192-2225 `kapi_fullscreen_direct` / `present_fb` |
| `Onyx\kernel\include\kern\gui\window.h` | | l. 31-43 : défaut 1024×768, min 640×480, max 2560×1600 |
| `Onyx\sdcard\config.txt`, `cmdline.txt` | | `hdmi_force_hotplug=1`, `max_framebuffers=2`, `width=1920 height=1080` |

### 1.2 Séquence d'init (mailbox, canal 8 propriétés, `0xFE00B880`)

Un seul message de tags (`CBcmFrameBuffer::s_InitTags`, upstream l. 28-35) :

| Tag | Code | Valeur |
|---|---|---|
| SET_PHYS_WIDTH_HEIGHT | `0x00048003` | w, h |
| SET_VIRT_WIDTH_HEIGHT | `0x00048004` | w, **2·h sur Pi 4** (Circle réserve toujours 2 pages, même VSync off — `2dgraphics.cpp` l. 178) |
| SET_DEPTH | `0x00048005` | 32 |
| SET_VIRTUAL_OFFSET | `0x00048009` | 0, 0 |
| ALLOCATE_BUFFER | `0x00040001` | alignement → renvoie adresse bus + taille |
| GET_PITCH | `0x00040008` | pitch en octets |

Puis : adresse ARM = `adresse_bus & 0x3FFFFFFF` (`bcmframebuffer.cpp` fork l. ~176). Avant le
message, sur Pi 4 : `GET_NUM_DISPLAYS` (`0x00040013`) et `SET_DISPLAY_NUM` (`0x00048013`) si
plusieurs écrans. Taille auto si w=h=0 : `GET_DISPLAY_DIMENSIONS` (`0x00040003`), bornée à 640×480..4096×2160.

Autres tags utiles (définis dans `ref\circle\include\circle\bcmpropertytags.h`) :
`WAIT_FOR_VSYNC 0x0004800E`, `SET_PALETTE 0x0004800B`, **`SET_CURSOR_INFO 0x00008010`,
`SET_CURSOR_STATE 0x00008011`** (curseur matériel géré par le firmware — **jamais utilisé par Onyx**).

Le tampon fantôme `m_pBuffer8 = new u8[w*h*4]` est en RAM **cachée** (tas Circle) ; le FB lui-même
est mappé par Circle en mémoire **Device** côté noyau.

### 1.3 Résolutions et changement de mode à chaud

- Boot : `cmdline.txt width= height=` → `OPT_W/OPT_H` (kernel.cpp l. 1706-1714), sinon 1024×768.
- À chaud (kapi v66 `screen_set`, applet Display) : `ScreenResizeRequest` (kernel.cpp l. 722-745)
  vérifie min/max et **largeur paire**, poste la demande ; la tâche d'affichage `CCompositorTask`
  l'exécute **entre deux trames**, seulement si aucune DMA d'affichage n'est en cours et aucune app
  plein écran (l. 673). `C2DGraphics::Resize` (2dgraphics.cpp l. 230-243) = `delete` du FB + du
  tampon fantôme puis `Initialize()` complet (nouveau message mailbox). Si le firmware refuse, Onyx
  **remet l'ancienne résolution** (l. 631-635). Puis `WsScreenResized` prévient le serveur graphique,
  qui réalloue son propre écran et renvoie `GUI_EVENT_DISPLAY_RESIZE` aux apps.
- Piège : le tampon plein écran (v41) gardait sa première taille → débordement sur le tas noyau après
  passage à une résolution plus grande (corrigé : réalloué plus grand ; docs/02 §8 v41).

### 1.4 Double buffering, page flip, vsync : ce qui existe et ce qui est utilisé

- **Circle sait faire le page flip** (2dgraphics.cpp l. 654-667) : si `m_bVSync`, `UpdateDisplay`
  = `WaitForVerticalSync()` (tag `0x4800E`, **bloquant, sans IRQ**) → copie de la page cachée par
  `SetArea` → `SetVirtualOffset(0, 0|h)` → bascule.
- **Onyx l'a coupé** dès le bring-up (commit `e9d5005e1`, 2026-06-22 : « VSync off ») :
  `m_2DGraphics (W, H, FALSE)` (kernel.cpp l. 1714, commentaire « avoid page-flip present »). Raison :
  le flip impose une copie plein écran + une attente vsync bloquante (~16 ms) par trame, incompatible
  avec un compositeur à rectangles sales sur un noyau non préemptif. Le patch « partial update »
  retombe d'ailleurs sur le plein écran si VSync est actif (fork 2dgraphics.cpp l. ~558).
- Présentation réelle : **simple tampon** visible + tampon fantôme ; seuls les rectangles modifiés
  sont copiés par DMA 2D. **Déchirure possible**, acceptée. Aucune interruption vsync n'est
  connectée nulle part (ni HVS, ni PixelValve).
- Mode **`fullscreen_direct`** (kapi v55) : le FB affiché est mappé dans l'app en mémoire **Normale
  non cachée** (`KPAGE_ATTR_APP_SCREEN`, kapi.cpp l. 455-478) ; le V3D y rend directement (FB < 1 Go,
  contigu) → zéro copie (OoT plein écran). Pas de double buffer : déchirure possible.

### 1.5 Chaîne de présentation complète (desktop)

```
apps (canevas 64 Ko-alignés, contigus, mappés à 12 Go) ──present──▶ Elegant (EL0)
Elegant : el_core_compose() → son écran w×h en RAM user (cachée)           [copie 1 : composition]
kws_present(rect) ─syscall─▶ wsrv.cpp Present : memcpy ligne à ligne → tampon fantôme C2DGraphics [copie 2]
DisplayPresentRect → UpdateDisplayStart (DMA 2D, stride src = largeur écran) → FB   [copie 3 : DMA]
                     puis UpdateDisplayPoll() entre deux Yield()
```
Cadence : ≤ 1 trame / 16 ms (serve.cpp l. 316-323), rectangles fusionnés en un seul si l'englobant
≤ 1,5× la somme des aires (l. 330-347). Coût mesuré du DMA plein écran 1080p : **2-4 ms**.
*Leçon ARMPI : la copie 2 n'existe que parce que le serveur est en EL0 et le FB au noyau ; en asm
mono-espace on peut composer directement dans le tampon que la DMA lit.*

### 1.6 Pièges framebuffer documentés

1. **`core_freq=300` → écran noir HDMI** (config.txt commentaire, 2026-10-06→14) : sur Pi 4 les horloges
   HDMI dérivent du core clock (500 MHz). Ne pas toucher `core_freq`.
2. **Pas d'écran = pas de FB** : sans `hdmi_force_hotplug=1`, le firmware n'alloue rien (commit `dd53c1da5`).
3. **FB = mémoire Device** : un `memcpy`/`memset` à stores non alignés fautent (commit `a24cfc2a6` :
   blob EL0 réécrit pour aligner la destination sur 8 puis stores alignés 8/16 o). Lire le FB par le
   mapping Device est lent → mapping **Normal non caché** pour VNC (`cdb2555e1`).
4. **Panique écran noir** : l'entrée d'exception masque FIQ, or la section critique DMA de Circle
   asserte FIQ actif → la page de panique ne s'affichait pas (commit `6f3718f1c`).
5. **Course DMA d'affichage** : une app plein écran qui présentait pendant que la DMA du compositeur
   tournait bloquait core 0 dans `SetArea` (boucle sans `Yield`) → watchdog. Fix : `DisplayPresentIdle`
   (kernel.cpp l. 692-701) avant tout `UpdateDisplay` hors tâche d'affichage.
6. **Pi 5** : l'affichage prend l'octet alpha ; les programmes écrivent `0x00RRGGBB` → pixels
   invisibles. Fork patch 31 : passe `|= 0xFF000000` avant chaque envoi (`C2DGraphics::Opaque`) ;
   `cmdline opaque=0` si `framebuffer_ignore_alpha` est honoré. *(Hors Pi 4, mais à retenir : en
   ARMPI écrire **`0xFFRRGGBB`** dès le départ.)*

**Transposable en asm ?** Oui, trivialement. Mailbox + 6 tags + masque d'adresse : ~150 lignes
FASMARM. Changement de mode : réémettre le message (+ libérer : `RELEASE_BUFFER 0x00048001`).
Effort **faible**, priorité **P0**.

---

## 2. DMA 2D pour l'affichage (patchs Circle 6 et 12)

| Fichier (fork) | Lignes Onyx | Contenu |
|---|---|---|
| `circle\lib\dmachannel.cpp` | l. 371-424, 493-513 | `SetupMemCopy2D(…, nSourceStride)` legacy, `Poll()` |
| `circle\lib\dma4channel.cpp` | l. 363-412, 542-555 | idem DMA4 (`SRCI` bits 16-31) |
| `circle\lib\bcmframebuffer.cpp` | l. 220-296 | `SetAreaPitchStart`, `SetAreaPoll`, `SetAreaPitch` |
| `circle\lib\2dgraphics.cpp` | l. 540-652 | `UpdateDisplay(x,y,w,h)`, `…Async`, `…Start`, `…Poll` |
| `Onyx\docs\circle-upstream\0004-2dgraphics-Partial-display-update-2D-DMA-with-a-sour.patch` | 371 | le patch complet |

- Canal : `DMA_CHANNEL_NORMAL` = **DMA legacy** (base `0xFE007000`, canal n à `+0x100·n`).
- Bloc de contrôle (32 o, aligné 32) : `TI = BURST(2)<<12 | SRC_WIDTH | SRC_INC | DEST_WIDTH | DEST_INC | TDMODE`,
  `SOURCE_AD = BUS_ADDRESS(src)` (= `(phys & ~0xC0000000) | 0xC0000000`), `DEST_AD` (FB, déjà bus),
  `TXFR_LEN = (rows-1)<<16 | bytes_par_ligne`, `STRIDE = dest_stride<<16 | src_stride` (en octets,
  15 bits signés), `NEXTCONBK = 0`. Limites legacy : 65 535 o/ligne, 16 383 lignes.
- Cohérence : `CleanDataCacheRange` **ligne par ligne** sur la source seulement (le FB est non caché).
- Fin : bit `CS.ACTIVE` sondé (`Poll`), **pas l'IRQ** : sous forte charge GPU (Ocarina of Time), une
  interruption de fin DMA était parfois perdue → compositeur bloqué à vie (docs/05 §12). Onyx a donc
  remplacé `UpdateDisplayAsync` (IRQ) par Start + Poll entre `Yield()`. Option `cmdline dispdma=0` =
  copie synchrone.
- Rien d'autre n'utilise la DMA pour les blits (fenêtres → bureau en CPU). IDEAS.md l. 47 le liste
  comme piste non faite.

**Transposable ?** Oui : ~120 lignes asm (un CB statique, 6 `str`, poll de `CS`). Effort **faible**,
priorité **P0** (dès le palier 1). Attention : la DMA legacy ne voit que les 1ers Go (adresses bus
32 bits) ; le tampon source doit être < 1 Go (ou utiliser DMA4, canaux 11-14, adresses 40 bits).

---

## 3. HVS (Hardware Video Scaler) : absent d'Onyx

Recherche exhaustive (`hvs`, `0xFE400000`, `SCALER_`, `PIXELVALVE`, curseur matériel, `0x8010`) dans
`kernel/`, `user/Servers/`, `docs/` : **aucune occurrence fonctionnelle**. Onyx :
- laisse le firmware programmer le HVS (une seule couche plein écran = le FB mailbox) ;
- **compose toutes les couches sur le CPU** (fenêtres, fond, curseur, badge DnD) ;
- dessine un **curseur logiciel** (`CWindowManager::Composite`, `user\Servers\common\wm\window.cpp`
  l. 1146-1176 ; formes v81 dans `wm\cursors.inc`, 232 lignes) — chaque mouvement de souris salit
  deux rectangles et relance composition + DMA ;
- docs/PI5-PORT.md l. 363-365 : « draws its own cursor (no hardware cursor on the Pi 5) ».

Implications pour ARMPI :
1. **Option bon marché, non validée par Onyx** : tags firmware `SET_CURSOR_INFO` (`0x8010` : w, h,
   pointeur ARGB 32 bpp, hotspot) et `SET_CURSOR_STATE` (`0x8011` : visible, x, y, flags) — le
   firmware crée lui-même un plan HVS curseur. Statut sur Pi 4 firmware récent sans KMS : **à tester**.
2. **Option directe** : écrire ses propres display lists HVS. Registres BCM2711 (HVS5, d'après Linux
   `drivers/gpu/drm/vc4/vc4_regs.h` / `vc4_hvs.c` — **à vérifier**, Onyx n'en dit rien) : base
   `0xFE400000`, `SCALER_DISPCTRL +0x00`, `SCALER_DISPSTAT +0x04`, `SCALER_DISPLIST0..2 +0x20/24/28`
   (offset en mots dans la mémoire de dlist), mémoire de dlist `SCALER5_DLIST_START = +0x4000`.
   Le firmware possède déjà une dlist active : la stratégie usuelle bare-metal est d'écrire une
   nouvelle dlist dans une zone libre de la dlist RAM puis de basculer `SCALER_DISPLISTn` du canal
   HDMI0 (**atomique, au vsync** → c'est aussi le vrai mécanisme de page flip sans copie et la source
   d'IRQ vsync via PixelValve/HVS `DISPSTAT`). Risque : conflit avec le firmware (qui peut réécrire
   la dlist lors d'un changement de mode).
3. Le format des entrées de plan (CTL0 avec `VALID`, `PIXEL_FORMAT`, `ORDER`, `UNITY`, POS0 x/y/alpha,
   POS2 w/h, pointeur image, pitch, `END`) est documenté dans Linux `vc4_plane.c` ; rien de cela
   n'est dans Onyx.

**Transposable ?** Rien à transposer depuis Onyx. Effort **moyen à élevé**, risque **élevé**
(rétro-ingénierie Linux + cohabitation firmware). Priorité **P2**.

---

## 4. Compositeur et gestionnaire de fenêtres (logiciel)

Depuis 2026-10-05 le WM/compositeur est **sorti du noyau** vers le serveur graphique **Elegant**
(processus EL0). Code commun à Elegant et PocketUI : `$ONYX/user\Servers\common\`.

| Fichier | Lignes | Rôle |
|---|---|---|
| `user\Servers\common\serve.cpp` | 358 | boucle : entrées, requêtes IPC, cadence 16 ms, composition, présentation (l. 205-358) |
| `user\Servers\common\core.cpp` | 464 | `el_core_compose` (l. 437-464) : dommages → `Composite` clipé par rectangle |
| `user\Servers\common\wm\window.cpp` | 2 164 | `CWindow::DrawTo` (l. 296+), `BlendAlphaRect` (l. 209-235), dommages (l. 560-621), `Composite` (l. 1056-1180) |
| `user\Servers\common\wm\kern\gui\window.h` | 767 | `TScreenDamage`, `SCREEN_DAMAGE_MAX 16`, drapeaux fenêtres |
| `user\Servers\common\ops.cpp` / `route.cpp` / `policy.h` | 794 / 77 / 95 | protocole, routage clavier, politique (Elegant vs PocketUI) |
| `kernel\gui\gimage.cpp` | 375 | `GImage` : clip, `PutOtherRaw` (`memcpy` par ligne, l. 199-227), clé magenta, police bitmap |
| `kernel\gui\kwin.cpp` / `surface.cpp` | 165 / 177 | ce que garde le noyau : file d'événements par fenêtre ; surfaces partagées 64 Ko-alignées contiguës |
| `user\Servers\pocketui\wm.cpp` | 1 624 | politique « une app plein écran » (pocket/console) |

**Structure.**
- Fenêtre = canevas `0x00RRGGBB` (octet haut = transparence si `WIN_FLAG_ALPHA`), **64 Ko-aligné,
  physiquement contigu**, mappé dans l'app à 12 Go (l'app dessine sans appel système ; le V3D peut y
  rendre directement). Cadre (titre 28 px, bord 4 px, coins rayon 8) dessiné **par l'app** en deux
  copies (actif/inactif) ; le compositeur blitte les bandes du cadre + mélange les coins.
- Liste Z en trois bandes (backmost / normal / topmost), `WM_MAX_WINDOWS`, spinlock ; `Composite`
  prend un **instantané** sous verrou puis blitte hors verrou.
- **Occlusion** : on cherche la fenêtre la plus haute qui couvre opaquement le rectangle de clip
  (`CoversOpaque`, coins arrondis exclus) ; rien en dessous n'est dessiné (fast-path émulateurs).
- **Damage tracking** (window.cpp l. 560-621) : ≤ 16 rectangles, fusion itérative de ceux qui se
  chevauchent/touchent ; liste pleine ou aire > 2/3 de l'écran → « tout l'écran ». Une app qui
  présente ne salit que **sa zone client** (`PresentDamage`), sauf si son cadre a changé.
- **Alpha** : `BlendAlphaRect` = boucle C scalaire, 3 multiplications + **3 divisions par 255** par
  pixel ; clé magenta pour `TRANSPARENT`. Fondus de fenêtre par opacité globale.
- **Mémoire retirée** : canevas agrandi → ancien libéré 3 trames plus tard (`FreeRetired`).
- **NEON : aucun** dans le compositeur (seuls `uikit/canvas.cpp`, `av_yuv.c`, `stb_image.h` en
  utilisent). Les blits opaques reposent sur `memcpy` (blob EL0 `kernel\arch\aarch64\el0blob.S`,
  285 lignes, stores alignés 8/16 o).
- Résilience : le noyau relance le serveur (5 fois max), reprend l'affichage s'il se tait
  (`WsWatch`), sinon console de debug ; la présentation renvoie `-EPERM` si le noyau a repris l'écran.

**Transposable ?** Oui, c'est du procédural déguisé. En asm : liste de fenêtres en tableau de
structures, damage list de 16 rects (≈ 150 lignes), compositeur back-to-front avec occlusion
(≈ 400 lignes), blits **NEON** (`ld1/st1` 64 o/itér. pour l'opaque ; mélange alpha via
`umull/umlal` + `rshrn #8` avec l'approximation `x/255 ≈ (x + (x>>8) + 1) >> 8`, 8 pixels/itér.)
≈ 300 lignes. C'est **là qu'ARMPI peut battre Onyx** (Onyx = C scalaire + 3 copies). Effort
**moyen**, priorité **P0/P1**.

---

## 5. V3D (VideoCore VI, V3D 4.2) — le pilote d'Onyx

### 5.1 Fichiers et tailles

| Fichier | Lignes | Rôle |
|---|---|---|
| `kernel\sys\v3d.cpp` | **2 067** | tout le pilote (Pi 4 + Pi 5 sous `#if RASPPI >= 5`) |
| `kernel\include\kern\v3d_cl.h` | 957 | paquets CL V3D 4.2 écrits à la main (bit-fields), `V3dLoadTarget/StoreTarget` l. 945-955 |
| `kernel\include\kern\v3d_pack42.h` / `v3d_pack71.h` | 2 663 / 2 817 | paquets générés depuis `v3d_packet.xml` de Mesa (`tools\v3d\genpackets.py`) |
| `kernel\include\kern\v3d_tiling.h` | 87 | dispositions de texture LT / UBLINEAR / UIF / XOR |
| `kernel\include\kern\v3d_clip.h` | 148 | clipping triangle (plans, bande de garde) |
| `kernel\sys\v3d_shaders.qasm` → `.inc` | 288 → 267 | 6 shaders QPU 4.2 écrits à la main (VS_CLIP, CS_CLIP, FS_COLOR, FS_TEX, FS_*_AT) |
| `kernel\sys\v3d_shaders71.qasm` | 296 | les mêmes pour V3D 7.1 (Pi 5, non testé sur matériel) |
| `kernel\sys\v3d.cpp` l. 129-150 | | 3 shaders Mesa précompilés (v52, de *rpi4-gpu-bare-metal-examples*, MIT) en `u64[]` |
| `user\Libs\v3d\` (qpu, shaders, gxtev, picatev, picavs) | ~3 000 | générateurs de shaders à l'exécution (TEV GameCube, combineur/VS PICA200 3DS) |
| `user\Libs\gpucomp\gpucomp.{c,h}` | 967 + 163 | service de composition de couches sur le V3D (et repli CPU) |
| `tools\qpu\` (qpuasm.c, qpulib.c, qpusim.cpp, mesa/) | ~1 150 + Mesa vendored | assembleur/désassembleur/validateur QPU + simulateur QPU hôte |
| Doc : `docs\02-KERNEL-INTERNALS.md` §15 (l. 2771-3011) | | la référence |

### 5.2 Registres et adresses (Pi 4, `ARM_IO_BASE = 0xFE000000`)

| Nom | Adresse | Usage |
|---|---|---|
| `V3D_HUB` | `0xFEC00000` | `HUB_IDENT0..2` +0x08/0C/10 ; `HUB_INT_MSK_SET` +0x60 (tout masqué) |
| `V3D_CORE0` | `0xFEC04000` | `CTL_IDENT0` +0x000, `SLCACTL` +0x024, `L2TCACTL` +0x030, `L2TFLSTA/END` +0x034/038, `INT_STS` +0x050, `INT_CLR` +0x058, `INT_MSK_SET/CLR` +0x060/064 |
| CLE (core0) | +0x100.. | `CT0CS` +0x100, `CT1CS` +0x104, `CT0CA/CT1CA` +0x110/114, `BFC` +0x134, `RFC` +0x138, `CT0QTS` +0x15C, `CT0QBA/CT1QBA` +0x160/164, `CT0QEA/CT1QEA` +0x168/16C, `CT0QMA/QMS` +0x170/174 |
| PTB | `PTB_BPOA` +0x308, `PTB_BPOS` +0x30C | pool de débordement du binner |
| `PM_GRAFX` | `0xFE10010C` | bit `V3DRSTN` (1<<6), mot de passe `0x5A000000` |
| `ASB_V3D_S_CTRL` / `M_CTRL` | `0xFEC11008` / `0xFEC1100C` | ponts AXI : effacer `REQ_STOP` (bit 0), attendre `ACK` (bit 1) à 0 |
| IRQ | GIC **SPI 74** | core et hub partagés ; bits `FRDONE` 1<<0, `FLDONE` 1<<1, `OUTOMEM` 1<<2 |

### 5.3 Séquence d'init (`PowerOn` l. 263-324, `Up` l. 328-372, `IrqOn` l. 410-416) — validée

1. Mailbox `SET_DOMAIN_STATE` (`0x38030`) domaine **10** ON.
2. Mailbox `SET_CLOCK_STATE` (`0x38001`) horloge **5** ON ; `GET_MAX_CLOCK_RATE` (`0x30004`) puis
   `SET_CLOCK_RATE` (`0x38002`) au max.
3. `PM_GRAFX |= V3DRSTN` (avec mot de passe, en conservant les 24 bits bas).
4. `ASB_V3D_M_CTRL` puis `ASB_V3D_S_CTRL` : `REQ_STOP` à 0, attendre `ACK`=0 (timeout 100 ms).
5. Lire `HUB_IDENT1` : bits 3:0 doivent valoir **4** (sinon GPU laissé éteint, message dans `gpu_info`).
6. Allouer < 1 Go : BCL 16 Ko, RCL 64 Ko, liste indirecte 16 Ko, état 16 Ko (attributs par défaut +
   shaders), pool de débordement 16 × 256 Ko.
7. Copier shaders + attributs par défaut, `CleanDataCacheRange`.
8. IRQ : masquer tout le hub ; masquer tout sauf `FRDONE|FLDONE|OUTOMEM` au core ; effacer ;
   `ConnectIRQ(SPI 74)` ; démasquer.
- L'init est **paresseuse** (premier `gpu_info`/`gpu_draw`), jamais au boot.

### 5.4 Mémoire GPU — choix d'Onyx

- **Pas de mailbox `mem_alloc/lock`**, **pas de MMU V3D** : toutes les adresses passées au GPU sont
  des **adresses physiques ARM brutes** (pas d'alias bus `0xC0000000` !), les buffers venant du tas
  bas de Circle (`HEAP_LOW`, < 1 Go, alignés page ; `Alloc` l. 173-185 rejette tout ce qui dépasse
  `0x40000000`). Les buffers ne font que **grossir** (réutilisés de trame en trame).
- Tile alloc = `tuiles × 64 o + 2 Mo` ; tile state = `256 o / tuile` ; pool OOM 4 Mo distribué par
  l'IRQ (`GiveChunk` l. 387-399 : `PTB_BPOA/BPOS`, masque `OUTOMEM` quand épuisé).
- Cible « directe » : si le tampon de l'appelant est physiquement contigu < 1 Go (canevas de
  fenêtre, FB), les tuiles y sont stockées directement (raster RGBA8, **R/B swap** → `0x00RRGGBB`,
  masque d'écriture alpha 0x8). Sinon passage par `s_Target` + copie avec swizzle. Traduction VA→PA
  page par page par `AT S1E1R` / `PAR_EL1` (l. 456-464).
- Textures : `TEXTURE_SHADER_STATE` 32 o + texels dans la disposition imposée par la taille (LT /
  UBLINEAR / UIF+XOR, `v3d_tiling.h`) — **pas de texture raster possible en V3D 4.2** pour les
  images 2D → toute texture est retuilée au chargement (d'où `gpu_texture_rect` partiel).

### 5.5 Cohérence de cache

- CPU → GPU : `CleanDataCacheRange` sur BCL/RCL/indirect/sommets/uniforms ; `DataSyncBarrier`.
- Cible : `CleanAndInvalidate` avant et après (directe) ; `Invalidate` avant lecture CPU (indirecte).
- Caches GPU avant **chaque** job (bin et render) : `InvalidateGpuCaches` (l. 578-584) =
  `L2TFLSTA=0, L2TFLEND=~0, L2TCACTL=1` (flush L2T) puis `SLCACTL=~0`.

### 5.6 Soumission d'un job (`Run` l. 666-698) et synchronisation

```
clean caches CL ; DSB
InvalidateGpuCaches ; INT_CLR=INTS ; INT_MSK_CLR=INTS ; events.clear
bfc = BFC & 0xFF
CT0QMA = tile_alloc ; CT0QMS = taille ; CT0QTS = tile_state | 2
CT0QBA = BCL début ; CT0QEA = BCL fin          ← lance le binner
attendre BFC != bfc  (événement IRQ FLDONE, tranches de 2 ms, timeout 0,5 s)
InvalidateGpuCaches ; rfc = RFC & 0xFF
CT1QBA = RCL début ; CT1QEA = RCL fin          ← lance le renderer
attendre RFC != rfc  (événement FRDONE)
```
- Le compteur de trames (`BFC`/`RFC`) est la **vérité** ; l'IRQ n'est qu'un réveil. Sans IRQ, repli
  en sondage 2 ms (et l'OOM du binner est servi par la tâche). Timeout → état des CLE logué, **GPU
  laissé éteint jusqu'au reboot** (`s_nState = -1`).
- **Un seul job à la fois**, entier (`s_bBusy` + noyau non préemptif sur core 0) ; partage entre
  processus par file implicite ; ressources (textures ≤ 1024 / 512 par programme, vbufs 32 / 8,
  programmes ≤ 256) marquées par espace d'adressage et libérées à sa mort (`V3DReleaseAS`).
- Pas de trames multiples en vol, pas de CSD (compute), pas de TFU, pas de MSAA, pas de mipmaps.

### 5.7 Les listes de commandes (recette validée)

- **BCL** (Draw l. 704-845) : `TILE_BINNING_MODE_CFG` (w, h) → `FLUSH_VCD_CACHE` →
  `OCCLUSION_QUERY_COUNTER(0)` → `START_TILE_BINNING` → `CLIP_WINDOW` → `CFG_BITS` (depth LESS,
  z-writes, culling) → `POINT_SIZE` / `LINE_WIDTH` → `CLIPPER_XY_SCALING` (w/2·256, −h/2·256 : unités
  1/256 px sur 4.2, 1/64 sur 7.1) → `CLIPPER_Z_SCALE_AND_OFFSET(0.5, 0.5)` → `Z_MIN_MAX(0,1)` →
  `VIEWPORT_OFFSET` → `COLOR_WRITE_MASKS` → `BLEND_CONSTANT_COLOR` → zero flat/noperspective/centroid
  flags → `TRANSFORM_FEEDBACK_SPECS(0)` → `SAMPLE_STATE(0xF, 1.0)` → `VCM_CACHE_SIZE(4,4)` →
  `GL_SHADER_STATE(rec, n_attr)` → `VERTEX_ARRAY_PRIMS(TRIANGLES, n, 0)` → `FLUSH`.
- **Shader state record** (`GLShaderStateRecord`) : adresses code ≫ 3, uniforms, tailles de segments
  VPM, 4-way threadable, `start_in_final_thread_section` (VS/CS oui, **FS non**), attributs (adresse,
  stride, type float/byte normalisé, vec_size).
- **RCL** (`BuildRCL` l. 597-663) : `TILE_RENDERING_MODE_CFG_COMMON` + `CLEAR_COLORS` + `CFG_COLOR`
  (RGBA8) + `ZS_CLEAR_VALUES(1.0)` → `TILE_LIST_INITIAL_BLOCK_SIZE` → `MULTICORE_RENDERING_TILE_LIST_SET_BASE`
  → `SUPERTILE_CFG` (supertuiles calculées pour < 256 frames) → **tuile (0,0) clear + store factice
  (contournement d'une course matérielle, recette Mesa)** ×2 → `FLUSH_VCD_CACHE` → liste générique
  par tuile dans la liste indirecte (`TILE_COORDINATES_IMPLICIT`, [load], `END_OF_LOADS`,
  `PRIM_LIST_FORMAT`, `BRANCH_TO_IMPLICIT_TILE_LIST`, `STORE_TILE_BUFFER_GENERAL` raster,
  `CLEAR_TILE_BUFFERS`, `END_OF_TILE_MARKER`, `RETURN_FROM_SUB_LIST`) →
  `START_ADDRESS_OF_GENERIC_TILE_LIST` → une `SUPERTILE_COORDINATES` par supertuile → `END_OF_RENDERING`.

### 5.8 Shaders

- v52 : 3 shaders **Mesa précompilés** (VS/CS/FS couleur) copiés tels quels (`u64[]`).
- v53+ : 6 shaders **écrits à la main** en assembleur QPU (syntaxe du désassembleur Mesa), assemblés
  hors-ligne par `tools\qpu\qpuasm` qui **revalide** chaque ligne (ré-encodage/désassemblage) et les
  contraintes de timing (r4 ≥ 3 instr. après SFU, r5 ≥ 2 après `ldvary`, une seule TMU/SFU/VPM/TLB par
  instruction, règles de `thrsw`, pas de TLB avant le wait scoreboard). `.inc` commité.
- v61+ : **shaders d'application** (`gpu_program`) générés à l'exécution par `user\Libs\v3d\qpu.h`
  (encodeur Mesa), placés à 256 o d'alignement suivis de NOPs (préchargement QPU), validés sur PC par
  `qpusim`.

### 5.9 Mesures sur Pi 4 (IDEAS.md l. 66, docs/02 §15, commits)

teapot 6 400 tri : **2,1 ms** (polling, copie) → **1,0 ms** (IRQ + rendu direct) contre 13,4 ms en
logiciel ; gpudemo 1,6 → 0,6 ms ; composition WebKit 0,65 ms ; Ocarina of Time 50,9 i/s plein écran
direct ; Wind Waker ~14 ms de GPU pour ~80 k sommets (le clipping CPU coûte 9,8 ms : le goulot est
côté CPU).

### 5.10 Bugs et pièges V3D documentés

| # | Piège | Source |
|---|---|---|
| 1 | **Bit-field mal paddé** dans `LOAD_TILE_BUFFER_GENERAL` (3 bits manquants après `r_b_swap`) → stride lu ÷ 8, rendu « par-dessus la cible » faux. Fix + test champ par champ contre `v3d_packet.xml` | commits `7487400eb`/`5d9c0afe3`, docs/02 l. ~2830 |
| 2 | **`P_FS_FINAL`** (FS démarré en section finale) **bloque le V3D** de temps en temps sur cibles ≥ 512 px de large (5/7 runs). Toujours finir par la paire `thrsw` de dernier segment avant les écritures TLB, comme Mesa | `14a7d20dc`, docs/02 l. ~2880 |
| 3 | Accumulateurs (r0-r5) **non préservés** à travers un `thrsw` : valeurs vivantes dans le register file | docs/02 §15 |
| 4 | **Événement IRQ juste après expiration d'un `WaitWithTimeout`** → tâche déjà Ready encore sur la liste d'attente → assertion → gel aléatoire « GPU freeze » | `3f48b93f9` |
| 5 | Interruption DMA d'affichage perdue sous forte charge GPU → sondage | docs/05 §12 |
| 6 | Pas de texture raster en 4.2 : toujours retuiler (UIF/XOR exact sinon texels faux) ; test hôte de chaque taille ≤ 300×300 contre Mesa | `run_v3d_tiling_test.sh` |
| 7 | Allocation de textures sur un cœur secondaire via `new` (heap du cœur 0) → aucune texture | `14a7d20dc` |
| 8 | Trame qui ne finit pas (shader infini) = GPU perdu pour tous jusqu'au reboot → les clients doivent avoir un repli CPU (`GPC_LOST`) | docs/02 §15 |
| 9 | Partage : un job long (14 ms) retarde les autres (pas de préemption GPU) | docs/02 §15 |
| 10 | Le compteur de paquets `GL_SHADER_STATE` n'a que 5 bits (≤ 16 tableaux d'attributs) | docs/02 §15 |

**Transposable ?** **Oui, et c'est le meilleur candidat** : aucune dépendance à une pile logicielle
(Mesa n'est utilisé qu'hors-ligne). En FASMARM : les paquets deviennent des **macros `db/dw/dd`**
(les champs non alignés au bit : macro `bits` qui accumule — cf. piège 1, à tester contre
`v3d_packet.xml`), les shaders se **réutilisent tels quels** en `dq` (convertir `v3d_shaders.inc`),
la séquence `Run` fait ~60 lignes asm. Estimation : bring-up + un triangle couleur ≈ 600-900 lignes
asm ; + textures (tiling UIF) ≈ +500 ; + clipping ≈ +400. Effort **moyen** (palier 3a) à **élevé**
(textures/programmes), priorité **P2/P3**.

---

## 6. Service de composition sur GPU (`gpucomp`)

`user\Libs\gpucomp\gpucomp.c` (967 l.) : couches ARGB prémultipliées (tuiles de 2048 avec bordure de
1 texel pour un bilinéaire sans couture), matrice affine 2D, clip, opacité, modes de fusion
(source-over `ONE, ONE_MINUS_SRC_ALPHA` ; multiply/subtract en 2 passes), une trame `gpu_render` par
composite, cible = canevas de fenêtre (zéro copie). Utilisé par **WebKit/NetSurf et des apps**, pas
par Elegant (qui reste CPU). Repli CPU complet, vérifié pixel à pixel contre le GPU sur PC (V3D
logiciel = `FS_TEX` exécuté dans `qpusim`). C'est le modèle d'un « HVS logiciel sur V3D » : utile
pour ARMPI si le HVS s'avère trop risqué (composer les fenêtres en texture sur le V3D).

---

## 7. Tableau de transposabilité

| Élément | Fichiers Onyx de référence | LOC Onyx | Asm ARMPI estimé | Effort | Priorité |
|---|---|---|---|---|---|
| Mailbox + FB 32 bpp | circle `bcmframebuffer.cpp`, `bcmpropertytags.h` | ~300 utiles | 150-250 | Faible | **P0** |
| Changement de mode | kernel.cpp l. 622-745, 2dgraphics.cpp l. 230-243 | ~120 | 80 | Faible | P1 |
| Page flip via `SET_VIRTUAL_OFFSET` + `WAIT_FOR_VSYNC` | 2dgraphics.cpp l. 654-667 (non utilisé par Onyx) | 15 | 60 | Faible (mais bloquant ~16 ms) | P1 |
| DMA 2D stride (Start/Poll) | dmachannel.cpp l. 371-424, bcmframebuffer.cpp l. 220-296 | ~150 | 120 | Faible | **P0** |
| Damage list 16 rects | window.cpp l. 560-621 | 60 | 150 | Faible | **P0** |
| Compositeur + occlusion + curseur logiciel | window.cpp l. 1056-1180, `DrawTo`, `CoversOpaque` | ~600 | 500 | Moyen | P1 |
| Blits / alpha | gimage.cpp, `BlendAlphaRect` | ~200 | 300 (NEON) | Moyen | P1 |
| Curseur firmware (tags 0x8010/11) | aucun (Circle les définit) | 0 | 60 | Faible, **non validé** | P1 (essai) |
| HVS display lists, plans, vsync IRQ | aucun | 0 | 600-1 000 | Élevé, **non validé** | P2 |
| V3D bring-up + IRQ + Run | v3d.cpp l. 56-125, 263-443, 578-698 | ~450 | 400 | Moyen | P2 |
| V3D BCL/RCL triangle couleur | v3d.cpp l. 597-845, v3d_cl.h | ~700 | 500 | Moyen | P2 |
| V3D textures (tiling) | v3d.cpp l. 847-958, v3d_tiling.h | ~200 | 500 | Élevé | P3 |
| V3D shaders | v3d_shaders.qasm/.inc, `tools\qpu\qpuasm` | 288 | 0 (réutiliser binaires) | Faible | P2 |
| V3D clipping / programmes app | v3d.cpp l. 1252-1906, v3d_clip.h | ~800 | 800+ | Élevé | P3 |

---

## 8. Feuille de route GPU ARMPI en 3 paliers

### Palier 1 — Framebuffer mailbox + double buffer (tout est validé par Onyx, sauf le flip)
1. Mailbox canal 8 (`0xFE00B880` : READ +0x00, STATUS +0x18, WRITE +0x20 ; tampon 16-aligné, adresse
   | 8, `clean` cache avant, `invalidate` après). Message unique des 6 tags (§1.2), virtuel **w × 2h**.
2. Adresse FB = `bus & 0x3FFFFFFF` ; mapper le FB en **Normal non caché** (pas Device : stores non
   alignés et lectures lentes, §1.6-3). Pixels **`0xFFRRGGBB`**.
3. **Deux stratégies à mesurer**, dans cet ordre :
   a. *Celle d'Onyx (validée)* : tampon fantôme en RAM cachée, damage list, DMA 2D legacy avec stride
      source, fin sondée sur `CS.ACTIVE` (pas d'IRQ). Pas de vsync, déchirure acceptée.
   b. *Page flip* : composer dans la page cachée (non affichée) puis `SET_VIRTUAL_OFFSET(0, h|0)` ;
      `WAIT_FOR_VSYNC` est un appel mailbox **bloquant** : à n'appeler que depuis une tâche dédiée,
      jamais en section critique. Inconvénient : il faut recopier les dommages dans les deux pages
      (ou tout redessiner).
4. Compositeur en asm : damage 16 rects (fusion), occlusion opaque, blits NEON, curseur logiciel.
5. `config.txt` : `hdmi_force_hotplug=1`, `max_framebuffers=2`, **pas de `core_freq`**,
   `disable_splash=1` ; **ne pas** charger `vc4-kms-v3d` (le firmware doit garder le HVS).

### Palier 2 — HVS : couches et curseur (rien de validé dans Onyx)
1. Essai à faible coût : curseur firmware (`0x8010`/`0x8011`). S'il marche, supprime le principal
   générateur de dommages (souris).
2. Lecture/validation (en lecture seule) des registres HVS `0xFE400000` et de la dlist active du
   firmware (`SCALER_DISPLIST1`?) pour comprendre sa structure avant toute écriture.
3. Écrire une dlist ARMPI (plans RGBA8888 : fond, fenêtres, curseur) dans une zone libre, bascule
   atomique de `SCALER_DISPLISTn` → vrai double buffer sans copie + IRQ vsync (HVS/PixelValve, à
   identifier sur GIC).
4. Repli si conflit firmware : composer sur le V3D (modèle `gpucomp`, §6) au palier 3.

### Palier 3 — V3D (recette Onyx validée)
- **3a** : bring-up exact §5.3 → `HUB_IDENT1 & 0xF == 4` → un triangle couleur avec les 3 shaders
  Mesa v52 (`v3d.cpp` l. 129-150) rendu dans un tampon < 1 Go puis copié. Sondage `BFC/RFC`, timeout.
- **3b** : IRQ SPI 74 (FLDONE/FRDONE/OUTOMEM + pool de débordement), rendu **direct** dans le FB ou
  la page cachée (R/B swap, masque alpha).
- **3c** : shaders v53 (`v3d_shaders.inc`), textures LT/UBLINEAR/UIF, blending → **compositeur de
  fenêtres sur GPU** (≈ 0,65 ms la trame chez Onyx).
- Outils à reprendre tels quels (C hôte, pas besoin de les porter) : `tools\qpu\qpuasm`
  (assembleur/validateur), `qpusim`, `tools\v3d\genpackets.py` (générer des macros FASMARM au lieu de
  structs C — supprime le piège n°1), tests `run_v3d_cl_test.sh`, `run_v3d_tiling_test.sh`.
- CSD/compute : non exploré par Onyx → hors feuille de route.

---

## 9. Autres éléments d'Onyx réutilisables comme référence

**Ordonnanceur préemptif et contexte.** `kernel\sched\scheduler.cpp` (1 098 l.), `task.cpp` (188),
`kernel\compat\circle\sched\scheduler.h` (328), `kernel\arch\aarch64\el0.S` (201), `vectors.S` (206),
`kernel\sys\el0.cpp` (694) ; doc `docs\02` §5-6. Tick 100 Hz, tranche 20 ms ; préemption **uniquement
depuis EL0** : `El0IrqEntry` construit sur la pile noyau de la tâche une trame complète x0-x30, SP_EL0,
ELR, SPSR **+ q0-q31 + FPSR/FPCR** (el0.S l. 55-72), puis `OnPreempt`+`Yield` sur cette pile (un seul
chemin de reprise). Le noyau est **non préemptif** ; les commutations volontaires (Circle
`taskswitch.S`) ne sauvent que les callee-saved x19-x30, **d8-d15** et FPCR/FPSR. Heuristique
anti-« hog » (série de préemptions ≥ 2 → tranche 1 tick, rafales de 60 ms sans eux), watchdog de
blocage (échantillonne PC/LR si pas de `Yield` > 100 ms). Directement transposable.

**ASID / pagination 64 Ko.** `kernel\mm\addrspace.cpp` (631 l.), `kernel\include\kern\addrspace.h`
(280), `layout.h` (202) ; doc `docs\02` §3-4. Granule **64 Ko** (Circle), table L2 (512 Mo/entrée) →
L3 (pages 64 Ko), T0SZ 64 Go ; une L2 par processus copiée du gabarit noyau (L3 noyau partagées) ;
**ASID 8 bits** 1..255 (0 = noyau), `TTBR0 = phys(L2) | asid<<48`, aucun flush TLB à la commutation,
`nG=1` pour l'utilisateur ; descripteurs écrits en **un seul store 64 bits** (un autre cœur peut
parcourir la table) ; matrice d'attributs (code EL0 `UXN=0/PXN=1`, écran = Normal non caché). Pièges :
`tlbi aside1is` après remappage (canevas agrandi), destruction d'AS (docs/02 §4 « an important pitfall »).

**exFAT.** FatFs de Circle (`circle\addon\fatfs\ff.c`, 7 303 l.) avec `FF_FS_EXFAT 1` (patch 13,
`docs\05` §13 : `FSIZE_t` 64 bits), cache secteurs 1 Mo write-through (patch 8), transferts
multi-clusters en une commande SD (patch 10), fast seek (patch 11), verrou de volume **dormant**
(`kernel\sys\fslock.cpp`) ; volumes `kernel\sys\volume.cpp` (757 l.). Pour ARMPI : FatFs est en C
portable — référence d'algorithme plutôt que code à transposer ; les gains (multi-cluster, cache)
sont les leçons à garder.

**Audio.** `kernel\sys\sound.cpp` (1 140 l.), `sound_resample.h` ; doc `docs\02` §12. Producteur sur
**core 1**, mixeur 8 canaux (gain 16.16, courbe quadratique), sorties jack (PWM + DMA 44,1 kHz),
HDMI (48 kHz, trames IEC958, blocs de 576), USB, conversion linéaire entière sans dérive ; entrée I2S
(v99). Les pilotes PWM/HDMI viennent de Circle (`CPWMSoundBaseDevice`, `CHDMISoundBaseDevice`) : à lire
dans `Onyx-PocketTerm\circle\lib\sound\`. Jack PWM + DMA = cible asm raisonnable ; HDMI audio plus délicat.

**WiFi.** Circle `addon\wlan\` (`ether4330.c` 3 070 l., `bcm4343.cpp`, `emmc.c`, pilote Plan 9 +
`wpa_supplicant`) + `kernel\sys\net.cpp` (2 125 l.) ; doc `docs\02` §11, patchs `docs\05` 23, 26
(`wsec` dérivé des chiffrements de l'IE, puce **sondée** plutôt qu'IRQ SDIO en retard de 2,5-5 ms,
scan 2 bandes, 5 GHz préféré, une trame par commande SDIO, bus 50 MHz, pas d'A-MPDU en émission),
pile sur **core 3** optionnelle (`netcore=1`). Le firmware Broadcom + WPA2 rend le portage asm très
lourd : à considérer **en dernier**, éventuellement en liant du C.
