# Horloge

L'**Horloge** montre l'heure sur un cadran à aiguilles et en chiffres, et garde vos **alarmes**. Elle s'ouvre par le menu Korin OS, catégorie **Accessoires**. Deux onglets en haut à gauche : **Horloge** et **Alarmes**.

## L'onglet Horloge

Le cadran, l'heure en chiffres en dessous, et trois boutons pour régler l'heure à la main : **+1 h**, **+1 min** et **−1 min**. Le réglage est gardé dans `SD0:/etc/clock.cfg` ; avec le réseau, l'heure vient d'Internet (NTP) toute seule.

## L'onglet Alarmes

- En haut, la **prochaine alarme** : « Prochaine alarme : demain 07:00 — dans 9 h 12 min », ou « Reportée jusqu'à 07:10 » après un report.
- La liste : l'heure, le nom, la répétition et l'état (Active, Désactivée, Reportée, Manquée) de chaque alarme, par ordre d'heure.
- **Ajouter** ouvre le dialogue d'une nouvelle alarme (20 au plus).
- **Modifier**, ou un double-clic sur une ligne, ouvre l'alarme choisie.
- **Activer** / **Désactiver** l'allume ou l'éteint.
- **Supprimer**, ou la touche Suppr, l'enlève, sans question.

### Le dialogue d'une alarme

| Champ | Rôle |
|---|---|
| Heure | les heures et les minutes (boutons + et −) |
| Nom | 40 caractères au plus ; vide : « Alarme » |
| Répétition | Une fois, Tous les jours, En semaine, Le week-end, ou Jours choisis (les sept cases dessous) |
| Son | Carillon, Bips ou Marimba ; **Essayer** en joue un tour |
| Active | allumée ou éteinte |

Une alarme « une fois » sonne à la prochaine fois que vient son heure (aujourd'hui, sinon demain), puis s'éteint.

## Quand une alarme sonne

Le service **clockd** sonne les alarmes, même l'Horloge fermée : une notification en haut à droite, une carte au-dessus des fenêtres, et le son en boucle.

- **Répéter dans 10 min** (ou Entrée) : elle sonne encore 10 minutes plus tard.
- **Arrêter** (ou Échap) : elle se tait ; une alarme « une fois » s'éteint.
- Sans réponse en 2 minutes, le son s'arrête et « Alarme manquée » est notifiée ; la ligne dit **Manquée**.

Si le Pi était éteint à l'heure d'une alarme « une fois » (dans les 12 dernières heures), elle est notifiée manquée au démarrage, sans son. Les alarmes ne sonnent pas pendant la première minute après le démarrage, le temps que l'heure soit sûre.

## Le fichier

Les alarmes sont dans `SD0:/etc/alarms.txt`, au format d'Onyx : un bloc `[alarm]` par alarme, des lignes `clé = valeur`. La ligne `snooze = 10`, en tête, donne les minutes du report (1 à 30).
