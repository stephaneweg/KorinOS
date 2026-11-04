# PocketQI

**PocketQI** est l'assistante de Korin OS. On lui pose **par écrit** une question sur le système ou sur une application (« Comment changer le thème du bureau ? », « Comment écrire une lettre ? ») ; elle répond dans une bulle et peut ouvrir l'application dont il est question. Elle n'écoute pas : ni micro, ni mot d'éveil. Ses réponses viennent d'un service d'intelligence artificielle choisi dans ses réglages (Mistral, Groq ou un serveur à soi), ou, hors ligne, des fiches des applications installées.

![La bulle de PocketQI](help/main.png)

## Ouvrir PocketQI

- **La bulle** : un clic sur l'icône de PocketQI dans la barre de menus, en haut à droite (une petite bulle avec deux yeux, à gauche du haut-parleur). La bulle s'ouvre sous l'icône, par-dessus les fenêtres. Un second clic sur l'icône la ferme, comme la touche Échap.
- **La fenêtre** : par le menu Korin OS, catégorie **Accessoires**, **PocketQI**. C'est une fenêtre ordinaire, qu'on peut déplacer et agrandir.

## La fenêtre

- **En haut**, le visage de Pocket, ses deux yeux aux couleurs du thème, et l'état : le service qui répond (« Mistral », « Groq », « Serveur local », ou « Hors ligne : réponses tirées des manifestes »), puis « Je réfléchis... » pendant qu'elle prépare sa réponse. Dans la bulle, le bouton **Réglages** est à côté.
- **Au milieu**, la conversation : vos questions et ses réponses.
- **En bas**, le champ « Écris ta question... » et le bouton **Envoyer**.

Tant que la conversation est vide, PocketQI dit bonjour et propose trois questions à essayer : un clic sur l'une la pose.

## Poser une question

1. Tapez la question dans le champ du bas.
2. Entrée, ou **Envoyer**.
3. PocketQI réfléchit, puis répond dans la conversation.

Elle se souvient des derniers échanges : on peut enchaîner (« et pour l'enregistrer en PDF ? »). Après dix minutes sans question, elle repart de zéro. **PocketQI > Nouvelle conversation** (Ctrl+N) efface la conversation tout de suite.

## Lui faire ouvrir une application

Demandez-le simplement : « Ouvre le tableur », « Lance le terminal ». Quand la question porte sur une application, PocketQI peut l'ouvrir pour vous, parfois sur le bon volet ou le bon onglet (« change le thème » ouvre [Réglages](help:control) sur le volet **Thème**).

Elle ne lance que des applications installées, et seulement avec les actions que leur fiche déclare.

## Ce qu'elle fait elle-même

PocketQI ne fait pas que décrire : quatre choses, elle les fait tout de suite, et sa réponse s'affiche dans la conversation. Une phrase ordinaire suffit, avec un service ou hors ligne.

| Quoi | Exemples de phrases |
|---|---|
| **La météo** (actuelle, un jour, la semaine) | « Quelle est la météo à Lyon ? », « Va-t-il pleuvoir demain ? », « Météo de la semaine », « J'habite à Namur » |
| **Les notes** de **Notes** | « Note : appeler le plombier lundi », « Lis mes notes », « Lis la note sur les vacances », « Imprime mes notes » |
| **L'agenda** du **Calendrier** | « Ajoute un rendez-vous chez le dentiste demain à 14h », « Qu'est-ce que j'ai cette semaine ? », « Imprime mon agenda de la semaine prochaine » |
| **La liste de courses** | « Ajoute du lait et des œufs à la liste de courses », « Retire le pain », « Coche le lait », « Lis ma liste de courses », « Vide la liste », « Imprime la liste de courses » |

- **La météo** vient d'Open-Meteo, un service gratuit sans compte. Sans ville dans la phrase, PocketQI prend la sienne : dites-lui où vous habitez, ou réglez **Ville (météo)** dans ses réglages. Les prévisions vont d'aujourd'hui à six jours plus tard.
- **Les notes** sont celles de l'application Notes, dans `SD0:/Notes` : une note ajoutée par PocketQI apparaît dans Notes (même ouverte), et PocketQI lit celles que vous avez écrites. « Lis mes notes » montre les cinq plus récentes en entier ; « Quelles sont mes notes ? » leurs titres.
- **Les rendez-vous** vont dans le Calendrier et dans l'encadré **Prochains rendez-vous** du bureau. Le jour se dit comme on le pense : « demain », « vendredi », « le 12 décembre », « lundi prochain » ; l'heure aussi : « 14h », « 9h30 », « midi », « de 9h30 à 11h ». Sans jour, PocketQI le demande.
- **La liste de courses** est une note de Notes intitulée « Liste de courses », un article par ligne : `[ ] lait (2 litres)`, `[x] pain` quand il est coché. On peut aussi la modifier dans Notes.
- **Imprimer** ouvre la fenêtre d'impression du système : on choisit l'imprimante (ou **PDF**), puis **Imprimer**. Rien n'est imprimé sans vous.

## Choisir qui répond

**PocketQI > Réglages...** (ou le bouton **Réglages** de la bulle) ouvre la boîte **Réglages de PocketQI**.

Sous **Qui répond aux questions :**, choisissez :

| Choix | Ce qu'il faut |
|---|---|
| **Mistral (clé d'API)** | une clé Mistral, dans **Clé Mistral** |
| **Groq (clé d'API)** | une clé Groq, dans **Clé Groq** ; Groq sert aussi de secours à Mistral |
| **Un serveur compatible OpenAI (adresse)** | l'adresse dans **Adresse du serveur** (`https://.../v1/chat/completions`), et **Clé du serveur** s'il en faut une |
| **Personne : seulement les manifestes, hors ligne** | rien |

**Modèle (facultatif)** choisit un autre modèle que celui par défaut du service. **Ville (météo)** est la ville de la météo quand la phrase n'en nomme pas. **OK** enregistre, **Annuler** abandonne.

Les clés sont tapées dans des champs masqués et gardées sur la carte, dans `SD0:/home/pocketqi/config.ini`. Elles ne partent que vers leur service, en HTTPS.

## Hors ligne

Sans service réglé, avec **Personne**, ou quand le service ne répond pas, PocketQI répond quand même : elle cherche l'application la plus proche de la question et donne sa description et ses conseils. « Ouvre » ou « lance » dans la question l'ouvre.

Hors ligne, la météo, les notes, l'agenda et la liste de courses marchent aussi, pour les phrases simples comme celles du tableau plus haut (en français ou en anglais). Les tournures plus libres demandent un service.

## Ce qui sort de l'appareil

Avec un service en ligne, PocketQI lui envoie la question, les derniers échanges (avec ce que ses outils ont répondu : une liste de courses lue, des rendez-vous) et la liste des applications installées. Rien d'autre : ni vos fichiers, ni le document ouvert. Pour la météo, seule la ville part chez Open-Meteo. Hors ligne, rien ne sort, sauf cette demande de météo.

## Menus et raccourcis

| Menu | Commande | Raccourci |
|---|---|---|
| PocketQI | Nouvelle conversation | Ctrl+N |
| PocketQI | Réglages... | |
| PocketQI | Quitter (Fermer dans la bulle) | |

| Touche | Effet |
|---|---|
| Entrée | envoie la question |
| Page préc., Page suiv. | font défiler la conversation |
| Échap | ferme la bulle |

## Astuces

- Les meilleures questions nomment l'application ou la tâche : « Comment insérer une image dans Letters ? ».
- Ce que PocketQI sait d'une application vient de sa fiche (`app.txt`) : description, mots-clés et conseils. Elle ne voit pas ce qui est affiché à l'écran et n'agit pas dans une application ouverte, sauf pour ses quatre outils.
- Pour une aide complète et détaillée, le menu **Aide** de chaque application ouvre sa page, comme celle-ci.
