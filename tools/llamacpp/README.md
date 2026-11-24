# llama.cpp sur Korin PC

[llama.cpp](https://github.com/ggml-org/llama.cpp) (MIT) fait tourner des modèles de langage au format GGUF sur le
processeur. Il est construit **dans Korin**, par le GCC 14, le cmake et le ninja de `/devtools/gcc`, sans linuxhost
et sans modifier ses sources. Paquet **`llamacpp`** (`tools/pkg/packages-pc.ini`) :

| Fichier | Rôle |
|---|---|
| `/bin/llama-server` | le serveur : API compatible OpenAI (`/v1/chat/completions`, `/v1/models`…), utilisable par OpenJarvis (moteur `llamacpp`), Vibe (`vibe-account add llamacpp`)… |
| `/bin/llama-cli` | le chat au terminal |

```bash
llama-server -m ~/modeles/modele.gguf --port 8080 -t 8 -c 32768 &
llama-cli -m ~/modeles/modele.gguf -t 8 -st -p "Bonjour"
```

Le port par défaut de llama-server est 9931 : OpenJarvis et Vibe attendent 8080. Les modèles ne sont pas fournis.

## Construire

Sous Korin PC, depuis la racine du dépôt :

```bash
bash tools/llamacpp/build.sh          # ~/.cache/korin-llamacpp, puis build/pc/llamacpp/tree/bin/
python3 tools/pkg/pub.py llamacpp --new --tree build/pc/llamacpp/tree   # première publication
```

`build.sh` prend llama.cpp au commit `LLAMA_COMMIT` (variable ; `af5c583` du 23/11/2026, celui qui a été essayé).
La construction croisée depuis WSL (`x86_64-onyx-elf`, comme `tools/rg`) n'est pas faite.

## Ce qui est propre à Korin

- **`toolchain-korin.cmake`** : `uname -m` répond « armpi ? », et ggml se croyait sur ARM (il compilait ses noyaux
  NEON). Le fichier nomme le système (ARMPI) et fixe le processeur à x86_64. Il ajoute `-D_GNU_SOURCE` : newlib ne
  déclare `wcwidth` (et d'autres) qu'avec une macro de fonctions.
- **Les options** : statique (`BUILD_SHARED_LIBS`, `GGML_BACKEND_DL` coupés : pas de `dlopen`), AVX2, FMA, F16C et
  BMI2 (le processeur du ZBook ; pas d'AVX-512), sans OpenMP (le GCC de Korin n'a ni `libgomp` ni `-pthread` :
  ggml prend son propre pool de threads), sans OpenSSL (pas de téléchargement de modèles par `-hf`), sans la page de
  chat web (`LLAMA_BUILD_UI` demande Node.js), sans le multimodal (`mtmd`), sans les sous-process
  (`LLAMA_SUBPROCESS`).

## Mesures (ZBook, i7 Tiger Lake, 8 threads)

Qwen3.5-2B Q4_K_M : lecture du texte 68 à 90 mots/s, génération 11 mots/s ; 1,7 Go de mémoire avec un contexte de
8192. Qwen3.5 est hybride (une couche d'attention sur quatre) : 12 Kio par mot de contexte, 384 Mio pour 32k.

## Défauts connus

- **llama-server et llama-cli ne se terminent pas** : après « Exiting... » ou un SIGTERM, leurs threads de calcul
  restent bloqués (25 threads en attente, aucun appel système) et llama-server garde son port. SIGKILL les arrête.
  À chercher dans le pool de threads de ggml (`ggml-cpu`) ou dans les conditions de libposix.
- **ninja au-delà de 3 tâches** : « File descriptor value too large » à l'ouverture des en-têtes de GCC
  (`immintrin.h`), jusque dans d'autres shells : une limite commune à tout le système, pas celle d'un process (un
  enfant de ninja démarre avec 3 descripteurs). `JOBS=3` par défaut.
- **`/tmp` refuse les noms de fichiers longs** (Errno 22 vers 90 caractères) : `TMPDIR` sur le disque.
- Pas de GPU : le backend Vulkan demanderait les en-têtes de Vulkan, une couche `vk*` sur VkKit
  (`vk_icdGetInstanceProcAddr`) et `glslc` pour les shaders ; le GPU Intel (UHD GT1) gagnerait peu, le NVIDIA T1200
  davantage quand il aura un pilote.

`gguf_meta.py` affiche les métadonnées d'un fichier GGUF sans numpy (architecture, contexte, couches, têtes) :
`python3 tools/llamacpp/gguf_meta.py modele.gguf context_length head_count`.
