#!/bin/sh
# fetch.sh : la source de MuseScore Studio ($MS_TAG), clone superficiel ; les retouches de Korin s'y appliquent
# (build.sh), mkpatch.sh les relit par git diff.
set -e
. "$(dirname "$0")/env.sh"
if [ ! -d "$MSRC/.git" ]; then
	git clone -q --depth 1 --branch "$MS_TAG" "$MS_GIT" "$MSRC"
fi
(cd "$MSRC" && git describe --tags --always)
