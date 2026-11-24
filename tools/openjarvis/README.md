# OpenJarvis sur Korin PC

[OpenJarvis](https://github.com/open-jarvis/OpenJarvis) (Stanford, Apache 2.0), l'assistant d'IA local en Python,
installé sur le Python 3.14 de Korin, **en natif** : ni linuxhost, ni Ollama (en Go, absent de Korin). Le modèle est
servi par **llama-server** (paquet `llamacpp`, `tools/llamacpp`), qu'OpenJarvis connaît comme moteur `llamacpp`.

Pas de paquet : OpenJarvis et ses dépendances viennent de PyPI au moment de l'installation.

```bash
pkg add llamacpp
bash tools/openjarvis/install.sh ~/openjarvis
~/openjarvis/start-llama-server.sh -m ~/modeles/modele.gguf -a mon-modele &
~/openjarvis/jarvis init --engine llamacpp --host http://localhost:8080 --no-download --no-scan --no-analytics
# ~/.openjarvis/config.toml : default_model = "mon-modele"
~/openjarvis/jarvis ask "Bonjour"
```

Essayé le 24/11/2026 sur le ZBook avec Qwen3.5-2B Q4_K_M : `jarvis ask` en 12 s environ (démarrage de Python
compris), `jarvis doctor` vert pour le moteur et le modèle. Le modèle s'est montré trop faible pour l'utilisateur.

## Ce qui est écrit pour Korin

| Fichier | Pourquoi |
|---|---|
| `install.sh` | sources au commit `OJ_COMMIT`, patch, `pip --user` dans `DOSSIER/pyenv` (`PYTHONUSERBASE`) |
| `jarvis` | le lanceur : `PYTHONUSERBASE`, `PATH` sans rustc, `TMPDIR` sur le disque, pas de vérification de mise à jour |
| `start-llama-server.sh` | llama-server sur le port 8080 (le sien par défaut est 9931), `--reasoning off` |
| `jiter-shim/` | `jiter` (le parseur JSON en Rust que demande `openai`) se construit par maturin, qui ne se construit pas ici : un remplaçant en pur Python de `from_json`, mode partiel compris (le seul usage d'openai) |
| `sqlite-nowal/` | le mode WAL de SQLite partage son index par un `-shm` projeté en mémoire partagée : « disk I/O error » sous Korin. Les 13 bases d'OpenJarvis le demandent ; ce module, chargé par un `.pth` dans `pyenv` seulement, le change en `DELETE`. Pas de WAL exclusif : les verrous de fichiers sont sans effet, deux process écriraient sans se voir |
| `openjarvis-korin.patch` | `jarvis gui` rendu facultatif : il importe psutil (qui ne connaît pas Korin) et demande Node.js 22 (absent) ; sans eux, la commande explique pourquoi au lieu d'empêcher tout le CLI de démarrer |

## Ce qui manque

- `jarvis gui` (Node.js, psutil) ; `lxml` (libxslt), `datasets` (pyarrow, pandas), `psutil`, `ddgs` (recherche web) :
  les fonctions qui s'en servent échouent à l'appel. Voix, entraînement (torch), énergie, extension Rust : absents.
- Python 3.14 alors qu'OpenJarvis annonce `<3.14` (`--ignore-requires-python`) : rien n'a cassé à l'essai.
- `jarvis init` lit la mémoire dans `/proc/meminfo` : il voit 0 Go et ne choisit pas de modèle (`default_model` à
  écrire à la main).

## Pièges

- **pip et rustc** : pip lance `rustc --version` pour son User-Agent et le tue au bout de 0,5 s ; deux fois, ce
  SIGKILL pendant une lecture a fait tomber ext4fs, le service du volume système (corrigé dans pc 2026.11.98).
  Les scripts lancent pip sans `/devtools/rust/bin` dans le `PATH`.
- **Pas de venv** (pas de liens symboliques) ; `pip --target` ne voit pas les paquets du système et voudrait
  recompiler pydantic : `PYTHONUSERBASE` et `pip --user`.
- **`/tmp`** refuse les noms de fichiers longs : `TMPDIR` sur le disque.

## Vibe sur le même llama-server

L'app Vibe range sa configuration dans `~/.local/vibe` (pas `~/.vibe`) :

```bash
export VIBE_HOME=~/.local/vibe
vibe-account add llamacpp --api-base http://127.0.0.1:8080/v1
echo sans-cle | vibe-account set-key llamacpp       # llama-server n'en demande pas, Vibe en veut une
vibe-account add-model llamacpp mon-modele --alias local
vibe-account use local
```

Dans `~/.local/vibe/config.toml`, pour le modèle : `max_context_length` (celui de llama-server, sinon Vibe suppose
262 144), `max_output_tokens = 4096` et `auto_compact_threshold` (sinon Vibe réserve tout le contexte à la réponse et
compacte après quelques phrases) ; `system_prompt_id = "minimal"` envoie 72 mots d'instructions au lieu de 1 446.
Un modèle de 2B n'est pas à la hauteur d'un agent : il bouclait sur un appel d'outil.
