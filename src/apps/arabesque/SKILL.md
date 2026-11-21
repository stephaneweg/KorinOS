---
name: korin-arabesque
description: Composer un morceau pour Arabesque, la composition algorithmique par fractales de Korin, en écrivant son préréglage .arb - la graine, la gamme, l'image calculée, le tracé IFS, les dix parties (instrument General MIDI, tessiture, rythme, nuances), la grille d'accords, la forme (A B A*). À charger dès qu'on demande une musique, un morceau, une ambiance ou « dans le style de... » pour Arabesque, ou qu'on veut lire ou changer un .arb.
user-invocable: true
---

# Composer pour Arabesque (le préréglage .arb)

Arabesque compose à partir d'une **image** : un **tracé** fractal (IFS) la parcourt, et la couleur de chaque point
visité donne la note suivante de chacune des **dix parties** General MIDI. On ne pose donc pas les notes. On écrit un
**préréglage `.arb`** : la gamme, le tempo, l'image, le tracé, l'orchestre et ses règles. Arabesque compose le morceau
quand l'utilisateur ouvre le fichier. Une même graine redonne toujours le même morceau.

## Où sont les choses

| Chemin | Contenu |
|---|---|
| `/home/public/arabesque/*.arb` | 19 préréglages d'exemple (sonatine, bossa, danse andalouse, nocturne...) : **partir de l'un d'eux** |
| `/apps/arabesque.app/styles/*.stl` | 7 styles (les parties seules, format d'Art Song : `[Pt_n]` puis `clé=valeur`) |
| `/apps/arabesque.app/traces/*.ifs` | 6 tracés (fougère, dragon, flocon...) : `Nom { a b c d e f p ... }` |
| `/apps/arabesque.app/default.sca` | les gammes : une ligne le nom, une ligne le masque |
| `HOME:/arabesque/` | où l'utilisateur range ses préréglages (`/home/arabesque` aujourd'hui) ; écrivez-y les vôtres |

Pour faire entendre le résultat, ouvrez-le dans Arabesque, sans bloquer le shell :

```sh
(/apps/arabesque.app/main /home/arabesque/vallee.arb >/dev/null 2>&1 &)
```

L'utilisateur l'écoute (F6 lit, F7 arrête) et l'exporte (menu Morceau : MIDI, WAV, MP3). F5 recompose avec la même
graine, F9 change l'image. Vous ne l'entendez pas : décrivez ce que vous avez voulu (gamme, instruments, rythme),
puis demandez-lui son avis et retouchez le fichier.

## Le fichier

Du texte UTF-8, `clé=valeur` sans espace autour de `=`. Les lignes `;` sont des commentaires. Les sections, dans cet
ordre :

```ini
; Arabesque : de quoi recomposer le morceau à l'identique
[Arabesque]
version=1
seed=1492              ; la graine (entier) : une autre graine, un autre morceau sur les mêmes règles
tempo=160              ; noires par minute, 20 à 300
copyright=
length=61440           ; la longueur à composer, en pulsations de 480 par noire (mesure 4/4 = 1920) ; 0 : rien
scale=Hijaz            ; le nom de la gamme (montré dans la fenêtre)
mask=1459              ; la gamme : 12 bits, le bit k = k demi-tons au-dessus de la tonique (bit 0 : la tonique)
root=4                 ; la tonique : 0 do, 1 do#, 2 ré... 9 la, 10 si bémol, 11 si

[Image]
gen=newton 3           ; l'image calculée : sorte et graine
size=512 512

[State]
at=0
image=0                ; le rang de l'[Image] (0 : la première)
chords=4 1             ; la grille d'accords (facultatif) : nombre de mesures, 1 si elle boucle
chord=1 1 0 0 0 0 0 0 0
trace=fougere          ; le tracé : un nom, son cadre, puis 2 à 20 transformations
frame=-2.2768 -0.1031 2.7516 10.1953
t=0 0 0 0.16 0 0 0.01
t=0.85 0.04 -0.04 0.85 0 1.6 0.85
t=0.2 -0.26 0.23 0.22 0 1.6 0.07
t=-0.15 0.28 0.26 0.24 0 0.44 0.07
style=vallee           ; le nom du style, puis ses parties [Pt_1] à [Pt_10]
[Pt_1]
On=1
...
```

Avec `length` à 0, ouvrir le fichier ne fait que poser les réglages : l'utilisateur doit appuyer sur F5. Mettez
plutôt une vraie longueur : 32 mesures, c'est `length=61440`. Les préréglages enregistrés par Arabesque ont ensuite
`[Scale]`, `[Style]` et `[Clear]` : le journal des changements faits pendant la composition. N'en écrivez pas ; un
seul `[State]` suffit.

### Les gammes (`mask`, la tonique au bit 0)

| Gamme | mask | | Gamme | mask |
|---|---|---|---|---|
| Majeure | 2741 | | Dorien | 1709 |
| Mineure naturelle | 1453 | | Phrygien | 1451 |
| Mineure harmonique | 2477 | | Lydien | 2773 |
| Mineure mélodique | 2733 | | Mixolydien | 1717 |
| Pentatonique majeure | 661 | | Hijaz | 1459 |
| Pentatonique mineure | 1193 | | Hirajoshi | 397 |
| Blues | 1257 | | Par tons | 1365 |
| Diminuée | 2925 | | | |

Une autre gamme se calcule : la somme de `2^k` pour chaque demi-ton `k` gardé. Hijaz (0 1 4 5 7 8 10) :
1 + 2 + 16 + 32 + 128 + 256 + 1024 = 1459.

### L'image

`gen=<sorte> <graine>` : `mandala`, `mandelbrot`, `julia`, `burningship`, `lyapunov`, `newton`, `rays`. La graine
choisit le cadrage et la palette. L'image fait le caractère du morceau. Une image contrastée et variée donne des
lignes qui bougent beaucoup. Une image unie donne des notes répétées. Une ligne `set=` facultative règle l'image à
la main. Toutes les sortes acceptent `x`, `y` et `zoom`. `rays` accepte aussi `rays` (6 à 120), `rings` (2 à 16),
`sat`, `light` (0 à 1) et `fade` (-1 à 1). Exemple : `set=rays=40 rings=6`.

### Le tracé (`trace`, `frame`, `t=`)

Chaque `t=a b c d e f p` est une transformation affine, `x' = a·x + b·y + e`, `y' = c·x + d·y + f`, tirée avec la
probabilité `p`. Il en faut au moins deux, toutes contractantes. **`frame=x0 y0 x1 y1`** est le rectangle du plan
que couvre l'image. Arabesque ne le recalcule pas en ouvrant un `.arb` : un cadre faux envoie le tracé hors de
l'image, et le morceau ne parle plus. Reprenez le tracé et le cadre d'un exemple, ou calculez le cadre (le script
plus bas). Le tracé ne change pas la gamme : il change **l'ordre** des couleurs lues, donc le dessin des mélodies.

### Les parties (`[Pt_1]` à `[Pt_10]`)

Une partie absente est éteinte. Les clés :

| Clé | Sens |
|---|---|
| `On` | 1 : la partie joue |
| `Name` | son nom, montré dans la fenêtre |
| `Chan` | canal MIDI 0 à 15 ; **9 : la batterie** (alors `Pat` ne compte pas, la note est l'instrument) |
| `Pat` | l'instrument General MIDI, de 0 à 127 (table plus bas) |
| `Low`, `High` | la tessiture en notes MIDI (60 = do du milieu). Pour une percussion, `Low = High` = la touche |
| `Start` | la première note (0 : au hasard dans la tessiture) |
| `Unit` | la durée de base, en pulsations **de 120 par noire** : 480 ronde, 240 blanche, 120 noire, 60 croche, 30 double |
| `DR` | le rythme : une suite de chiffres, chacun un multiple de `Unit`, joués en boucle (`DR=21122` avec `Unit=60` : noire, croche, croche, noire, noire). Vide : la durée suit la couleur |
| `Den` | la densité, en % de notes jouées (le reste : silences) |
| `Vct` | les notes ensemble, 1 à 4 (accords) ; `Chd` leur disposition : 0 serrée, 1 et 2 ouvertes |
| `Tie` | 1 : notes liées |
| `MP`, `MD`, `MV` | la composante de la couleur qui donne la hauteur, la durée, la nuance : 0 rouge, 1 vert, 2 bleu |
| `MA` | l'articulation : 0 fixe, 1 rouge, 2 vert, 3 bleu |
| `VR` | l'étendue des nuances, 10 à 80 (la vélocité va de 127 − VR à 127) |
| `Vol`, `Pan` | volume et panoramique, 0 à 127 (64 : au milieu) |
| `Pha` | le décalage, en pas du tracé, sur la partie d'avant : des parties qui ne jouent pas la même chose |
| `Har` | ce qu'elle fait de la grille d'accords : 0 l'accord aux temps et des passages entre, 1 l'accord seul, 2 sa basse aux temps, 3 libre |
| `Mute` | 1 : composée mais muette |

Mettez des parties de rôles différents : une basse grave et lente (`Har=2`, `Unit=120`), un accompagnement en
accords (`Vct=3`, `Har=1`), une mélodie plus rapide et liée dans l'aigu (`Har=0`, `Tie=1`), et une ou deux
percussions sur le canal 9. Écartez leurs tessitures et leurs `Pan`.

**Instruments** (`Pat`, de 0 à 127) : 0 piano, 4 piano électrique, 6 clavecin, 11 vibraphone, 12 marimba, 19 orgue
d'église, 21 accordéon, 24 guitare nylon, 25 guitare acier, 26 guitare jazz, 32 contrebasse, 33 basse, 40 violon,
41 alto, 42 violoncelle, 45 pizzicati, 46 harpe, 47 timbales, 48 cordes, 52 chœur, 56 trompette, 57 trombone,
60 cor, 65 sax alto, 68 hautbois, 70 basson, 71 clarinette, 73 flûte, 75 flûte de Pan, 77 shakuhachi, 79 ocarina,
88 à 95 nappes de synthé, 104 sitar, 105 banjo, 107 koto, 108 kalimba, 109 cornemuse, 110 violon populaire,
111 shehnai, 114 steel drum, 115 woodblock, 116 taiko.

**Batterie** (canal 9, `Low = High` = la touche) : 35 et 36 grosse caisse, 38 caisse claire, 39 claquements de
mains, 42 charleston fermé, 46 ouvert, 49 cymbale, 51 ride, 54 tambourin, 56 cloche, 60 et 61 bongos, 62 à
64 congas, 69 cabasa, 70 maracas, 75 claves, 76 et 77 woodblocks, 85 castagnettes.

### La grille d'accords (facultative)

`chords=<mesures> <boucle>`, puis une ligne par mesure qui change :
`chord=<mesure> <degré> <altération> <qualité> <sus> <septième> <ajouts> <basse> <open>`.

* degré : 1 à 7 dans la gamme, 8 sans accord. Une mesure absente prolonge l'accord d'avant.
* altération : -1 bémol, 0, 1 dièse.
* qualité : 0 celle de la gamme, 1 majeur, 2 mineur, 3 diminué, 4 augmenté, 5 quinte seule.
* sus : 0, 1 sus2, 2 sus4.
* septième : 0 rien, 1 celle de la gamme, 2 7, 3 maj7, 4 sixte, 5 septième diminuée.
* ajouts, en bits : 1 neuvième, 2 onzième, 4 treizième, 8 ♭9, 16 ♯9, 32 ♯11, 64 ♭13, 128 ♭5, 256 ♯5.
* basse : 0 fondamentale, 1 tierce, 2 quinte, 3 septième, 4 voice leading (le plus court chemin).
* open : 1 voix écartées.

Cadence andalouse en la mineur harmonique (i, VII, VI, V) : `chord=1 1 0 0 0 0 0 0 0`, `chord=2 7 -1 1 0 0 0 4 0`,
`chord=3 6 0 0 0 0 0 4 0`, `chord=4 5 0 0 0 2 0 4 0`.

### La forme (facultative)

Dans `[Arabesque]` : `form=A B A* C A` (une lettre par section ; `*` : une variation de la lettre, même rythme,
autres hauteurs), puis une ligne par lettre, `section=<lettre> <mesures> <u> <v> <zoom> <tonalité>`. `u` et `v`
(0 à 1) donnent le centre de sa région dans l'image ; `zoom` (1 : toute l'image) son grossissement. Tonalité :
0 celle du morceau, 1 la dominante, 2 la sous-dominante, 3 le relatif. Une lettre peut avoir son propre style :
`[Letter]`, `letter=B`, `style=<nom>`, puis ses `[Pt_n]`, avant `[Image]`. Avec une forme, mettez
`length` = la somme des mesures × 1920.

## Écrire un préréglage par un script

Ce script (`python3 make_arb.py`) écrit un `.arb` complet et calcule le cadre du tracé comme Arabesque. Changez
`PARTS`, la gamme et l'image.

```python
import os
import random

def frame(ts):
    """Le cadre du tracé, comme Arabesque (Ifs::limits) : 30000 pas, 2 % de marge."""
    tot = sum(max(t[6], 0) for t in ts)
    r = random.Random(0x5EED)
    x = y = 0.0
    pts = []
    for i in range(30064):
        u, c = r.random() * tot, 0.0
        for a, b, cc, d, e, f, p in ts:
            c += max(p, 0)
            if u < c:
                break
        x, y = a * x + b * y + e, cc * x + d * y + f
        if i >= 64:
            pts.append((x, y))
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    mx, my = (max(xs) - min(xs)) * 0.02 or 0.5, (max(ys) - min(ys)) * 0.02 or 0.5
    return min(xs) - mx, min(ys) - my, max(xs) + mx, max(ys) + my

TRACE = [(0.5, 0, 0, 0.5, 0, 0, 1), (0.5, 0, 0, 0.5, 0.5, 0, 1), (0.5, 0, 0, 0.5, 0.25, 0.43, 1)]  # triangle
DEF = dict(On=1, Name="", Chan=0, Pat=0, Start=0, Low=48, High=72, Vct=1, Chd=0, Tie=0, MP=0, MD=0, DR="", MV=0,
           VR=20, MA=0, Vol=90, Pan=64, Unit=120, Den=100, Pha=100, Mute=0, Har=0)
PARTS = [
    dict(Name="Contrebasse", Chan=0, Pat=32, Low=40, High=52, Unit=120, DR="2", Har=2, Pha=20),
    dict(Name="Guitare", Chan=1, Pat=24, Low=52, High=69, Vct=3, Unit=60, DR="332", Har=1, Pan=40, Pha=50),
    dict(Name="Hautbois", Chan=2, Pat=68, Low=64, High=84, Unit=30, DR="2211", Tie=1, Pan=84, Pha=70),
    dict(Name="Castagnettes", Chan=9, Low=85, High=85, Unit=30, DR="211", Den=70, Vol=70, Pan=96, Pha=10),
]
out = ["; Arabesque : de quoi recomposer le morceau à l'identique", "[Arabesque]", "version=1", "seed=1492",
       "tempo=150", "copyright=", "length=%d" % (32 * 1920), "scale=Hijaz", "mask=1459", "root=4", "",
       "[Image]", "gen=newton 3", "size=512 512", "", "[State]", "at=0", "image=0", "trace=triangle",
       "frame=%r %r %r %r" % frame(TRACE)]
out += ["t=" + " ".join(repr(float(v)) for v in t) for t in TRACE]
out.append("style=mon_style")
for i, p in enumerate(PARTS):
    q = dict(DEF, **p)
    out.append("[Pt_%d]" % (i + 1))
    out += ["%s=%s" % (k, q[k]) for k in DEF]
    out.append("")
os.makedirs("/home/arabesque", exist_ok=True)
open("/home/arabesque/mon_morceau.arb", "w", encoding="utf-8").write("\r\n".join(out) + "\r\n")
```

## Pour un style donné

Le style tient surtout à quatre choses : la **gamme** et la tonique, le **tempo**, les **instruments**, et les
**rythmes** (`Unit` et `DR` de chaque partie, et les percussions). Ensuite viennent la grille d'accords et la sorte
d'image. Par exemple :

* **Flamenco, désert, Orient** : Hijaz ou Phrygien, guitare nylon en accords rapides, castagnettes et mains,
  cadence andalouse.
* **Baroque** : Majeure ou Mineure harmonique, clavecin, cordes, basson ; croches régulières (`DR=1`, `Unit=60`).
* **Jazz léger** : Dorien ou Mixolydien, septièmes et neuvièmes dans la grille, contrebasse en noires, ride.
* **Japon** : Hirajoshi, koto, shakuhachi, taiko lent.
* **Ambiance** : pentatonique, nappes (88 à 95), `Unit` long, `Den` bas, `Tie=1`.

Lisez un exemple proche avant d'écrire (`danse_andalouse.arb`, `bossa.arb`, `nocturne.arb`...). Ne recopiez pas de
mélodie d'une œuvre existante : Arabesque compose les notes, et c'est le style qu'on décrit.

## Vérifier

* Ouvert, Arabesque montre « Ce fichier n'est pas un préréglage d'Arabesque » s'il manque `[State]` ou
  `[Image]`. Il montre « Le préréglage est abîmé » si un `[State]` n'a pas de style, ou a moins de deux `t=`, ou
  désigne une `image=` qui n'existe pas.
* La barre d'état donne la graine et le nombre de mesures composées.
* Des notes qui se répètent, ou une partie qui se tait, viennent souvent d'un cadre faux, d'une image trop unie,
  d'une tessiture trop étroite ou d'un `Den` trop bas.
