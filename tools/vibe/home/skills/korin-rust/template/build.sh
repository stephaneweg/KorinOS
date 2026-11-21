#!/bin/bash
# build.sh -- construit l'app avec cargo et l'installe dans /apps/<nom>.app (pibuild ne connaît pas Rust).
#   bash build.sh          construire et installer        bash build.sh test     les essais (cargo test)
set -e
cd "$(dirname "$0")"
export PATH=$PATH:/devtools/rust/bin
NAME=$(sed -n 's/^name *= *"\(.*\)"/\1/p' Cargo.toml | head -1)
if [ "$1" = test ]; then exec cargo test --release; fi
cargo build --release
mkdir -p "/apps/$NAME.app"
cp "target/release/$NAME" "/apps/$NAME.app/main"
cp app.txt "/apps/$NAME.app/"
[ ! -d data ] || cp -r data/. "/apps/$NAME.app/"
echo "Installé : /apps/$NAME.app/main"
