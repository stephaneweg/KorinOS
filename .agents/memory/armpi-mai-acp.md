---
name: armpi-mai-acp
description: "19/11 : mai-acp, agent ACP générique compatible OpenAI (oai_acp, reprend claude_acp), profil MAI de Microsoft Foundry ; app MAI, agent --mai ; MAI-Code-1.1-Flash retiré le 9/12"
metadata:
  type: project
---

Le 19/11/2026, à la demande de l'utilisateur, un agent de code pour les modèles MAI de Microsoft a été fait. Tout est
dans main (commits cef2a945..1e756fcd), décrit dans docs/MAI-ACP.md.

- L'utilisateur a choisi un agent **générique** compatible OpenAI. Le point d'accès, la clé et le modèle sont dans
  config.toml ou .env, et MAI est le profil par défaut. Le code est dans tools/oai/oai_acp : il reprend claude_acp
  (Session et Server en sous-classes) et passe par urllib, sans SDK.
- **MAI-Code-1.1-Flash est retiré** de Foundry le 9/12/2026 (date de l'API Azure Models), et de Copilot le 18/12.
  MAI Code 2 Flash est déjà dans la liste (en préversion à accès limité, sans prix publié) : passer à lui quand il
  sera accessible.
- Rien n'a été essayé contre le vrai Foundry, faute de déploiement. Le chemin /mai/v1 et l'en-tête api-key viennent de
  la documentation de MAI-Thinking-1.
- Le 19/11 sous QEMU PC : 44/44 contre la fausse API, et l'app s'ouvre. Mais « python3 fichier.py » plantait
  (Segmentation fault), ce qui touche aussi /bin/claude-acp et /bin/mai-acp. Ce bug n'est pas réglé.
- Rien n'est publié. Les versions de versions.ini sont remises par mkrepo à chaque publication, donc on ne les monte
  pas à la main.

**Why:** pour reprendre le chantier sans refaire la recherche. **How to apply:** voir [[armpi-claude-acp]] pour la
base ; vérifier le bug de python3 avant de dire que claude-acp ou mai-acp marchent sur le PC.
