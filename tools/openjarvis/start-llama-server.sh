#!/bin/bash
# start-llama-server.sh : llama-server (paquet llamacpp) là où OpenJarvis l'attend, http://127.0.0.1:8080/v1.
#     start-llama-server.sh -m MODELE.gguf [-a NOM] [autres options de llama-server]
# Variables : JARVIS_THREADS (8), JARVIS_CTX (32768), JARVIS_REASONING (off : plus rapide sur un petit modèle),
# JARVIS_PORT (8080 ; celui de llama-server par défaut est 9931).
LLAMA_SERVER=${LLAMA_SERVER:-$(command -v llama-server || echo /bin/llama-server)}
if [ ! -x "$LLAMA_SERVER" ]; then
	echo "llama-server introuvable : pkg add llamacpp" >&2
	exit 1
fi
exec "$LLAMA_SERVER" \
	--host 127.0.0.1 --port "${JARVIS_PORT:-8080}" \
	-t "${JARVIS_THREADS:-8}" \
	-c "${JARVIS_CTX:-32768}" \
	--reasoning "${JARVIS_REASONING:-off}" \
	--jinja \
	"$@"
