# Mises à jour du système : emplacements A/B avec version de secours confirmée

Décisions de l'utilisateur (conversation du 25/11/2026). Nouveau chantier, rien n'est implémenté. Il remplace l'étape E55 du plan (constat B5 de l'audit : mise à jour du noyau ni atomique ni réversible).

## 1. Principe

- Deux emplacements pour les fichiers de démarrage (noyau, `korin.efi`, `initrd.tar`) : **A** et **B**. Les paquets et `/home` ne sont pas doublés.
- Un emplacement est la **version de secours confirmée**, l'autre la **version en essai**.
- Le chargeur garde un petit état : l'emplacement confirmé, l'emplacement en essai, et un compteur d'essais de démarrage pour celui-ci.
- Une version en essai qui ne démarre pas épuise son compteur, et le chargeur revient tout seul sur la version confirmée.
- **Un démarrage réussi ne confirme jamais rien.** Seul root confirme (voir §2).

## 2. Déroulement

1. On démarre sur A, la version confirmée.
2. Une mise à jour système s'écrit dans B, qui devient la version en essai.
3. Tant que root n'a pas confirmé, **les mises à jour suivantes réécrivent B**. A n'est jamais touché. Un bug découvert plus tard, même après plusieurs mises à jour, laisse donc toujours A disponible.
4. La confirmation se demande **au moment d'une nouvelle mise à jour système**, avant d'écrire :
   « Korin <version actuelle> fonctionne bien → la garder comme version de secours, pour revenir en arrière si la nouvelle version pose problème ? »
   - **Oui** : l'emplacement actuel (B) devient la version confirmée, et la mise à jour s'écrit dans l'autre (A), qui devient le nouvel essai. Les rôles alternent ainsi au fil des mises à jour.
   - **Non** : la version confirmée ne change pas, et la mise à jour réécrit l'emplacement en essai.
5. Revenir sur la version confirmée depuis le menu de démarrage doit demander la présence physique ou root. Un attaquant ne doit pas pouvoir forcer le démarrage sur une version ancienne et vulnérable.

## 3. Élévation et question de secours (variante simple retenue)

- **Mise à jour d'apps ou de paquets ordinaires seulement** : aucune question de secours. L'élévation habituelle, et le « retenir 5 min » reste valable.
- **Le lot contient au moins un paquet système** (`pc`, `armpi`, ou tout paquet qui touche les fichiers de démarrage) :
  - la carte d'élévation s'ouvre **toujours** : le « retenir 5 min » ne s'applique pas aux mises à jour système ;
  - elle contient la case « Garder la version actuelle comme version de secours » ;
  - la question est donc posée même si l'élévation était déjà retenue.
- **Une seule élévation pour tout le lot** :
  - pkgman (ou `pkg`) calcule d'abord le lot complet, dépendances comprises, puis ouvre **au plus une carte** ;
  - si le lot contient un paquet système, cette carte, avec la case de secours, couvre aussi toutes les apps du lot ;
  - aucune seconde demande en cours de route (`ui_elevate` laisse le process en uid 0 jusqu'à la fin) ;
  - en terminal, `sudo pkg update` : un seul sudo pour la commande, et pkg pose la question de secours au terminal si le lot contient un paquet système.
- **La question passe par authd** (carte dans la couche sûre, avec le repère), jamais par l'app de mise à jour : aucune app ne doit pouvoir répondre « oui » toute seule.
- **Sans personne devant** (pkgd automatique, terminal sans clavier) : réponse par défaut « non ». La version confirmée ne change pas, et pkgd ne confirme jamais seul.

## 4. Points à traiter dans la mise en œuvre

- Chargeur et menu de démarrage : les deux emplacements, l'état, le compteur d'essais, l'entrée « version de secours ».
- Écriture de l'emplacement en essai : fichiers `.new` puis renommage dans le volume, SHA-256 vérifié **au moment de la mise en place** (absent aujourd'hui).
- Paquets et ABI : revenir sur un noyau ancien avec des paquets récents peut poser des problèmes d'ABI (kits, appels système). Lier le paquet `pc`, et les kits si besoin, à son emplacement, ou permettre à pkg de revenir aux paquets de la version confirmée.
- pkglib, pkgman, pkgd, osinstall (installation initiale : A confirmé, B vide), installateur.
- Essais sur le ZBook : mise à jour, démarrage raté de B puis retour sur A, confirmation, alternance, menu de secours.
- La place sur l'ESP (FAT, pas d'instantanés) : deux jeux de fichiers de démarrage, quelques dizaines de Mo.

## 5. Autres décisions de l'utilisateur dans cette conversation

- **Publication de l'index (K2, E50, E51)** : l'utilisateur autorise à publier avec la clé de la variable d'environnement `ONYX_PKG_KEY`. À vérifier avant : les outils (`tools/pkg/pub.py`, `mkrepo.py`) lisent peut-être `OnyxPkgKey` ; adapter, et effacer tout fichier temporaire de la clé juste après la signature. Ordre :
  1. E50 : `serial` dans les outils de publication ;
  2. publier l'index signé avec ce numéro ;
  3. E51 : pkglib refuse un index de numéro plus petit que le dernier vu (absent = 0).