#!/bin/bash
# Essai de tools/git/git-credential-korin sous Linux, avec le vrai git (« git credential fill / approve / reject »)
# et des commandes oauth et secret simulées (un dossier d'état jetable) : coffre, GitHub par oauthd (compte
# connecté, code d'appareil, Client ID demandé sur le « terminal », GIT_TERMINAL_PROMPT=0), oubli au refus.
#
#   bash tools/git/test/credential.sh
set -u
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
W=$(mktemp -d /tmp/gitcred-test.XXXXXX)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/bin" "$W/st/vault" "$W/st/oauth" "$W/home"
cp "$ROOT/tools/git/git-credential-korin" "$W/bin/"
chmod +x "$W/bin/git-credential-korin"

# secret get|set|rm NOM (set : la valeur sur l'entrée)
cat > "$W/bin/secret" <<'S'
#!/bin/bash
f="$ST/vault/$(printf %s "$2" | tr / '#')"
case $1 in
get) [ -f "$f" ] && cat "$f" ;;
set) cat > "$f" ;;
rm) rm -f "$f" ;;
esac
S
# oauth status|info|setup|login|token|forget NOM : un seul compte ; client_id du « système » si $ST/sys_cid
cat > "$W/bin/oauth" <<'S'
#!/bin/bash
A="$ST/oauth/$2"
echo "$*" >> "$ST/oauth.log"
case $1 in
status) [ -f "$A.conf" ] || { echo "oauth : compte inconnu" >&2; exit 1; }
	[ -f "$A.tok" ] && { echo connecté; exit 0; }; echo "à connecter"; exit 3 ;;
info) [ -f "$A.conf" ] && cat "$A.conf" ;;
setup) shift 2; c=$(printf '%s\n' "$@")
	grep -q '^client_id=' <<< "$c" || [ -f "$ST/sys_cid" ] || { echo "oauth : il faut client_id" >&2; exit 1; }
	printf '%s\n' "$c" > "$A.conf"; echo configuré ;;
login) [ -f "$A.conf" ] || exit 1
	echo "Sur un téléphone ou un ordinateur, ouvrez :"; echo "  https://github.com/login/device"
	echo "et tapez le code :  WDJB-MJHT"
	[ -f "$ST/deny" ] && { echo "oauth : access_denied" >&2; exit 1; }
	echo "gho_$RANDOM$RANDOM" > "$A.tok"; echo "$2 : connecté." ;;
token) [ -f "$A.tok" ] || { echo "oauth : pas connecté" >&2; exit 1; }; cat "$A.tok" ;;
forget) rm -f "$A.tok" ;;
esac
S
chmod +x "$W/bin/secret" "$W/bin/oauth"

export ST="$W/st" HOME="$W/home" PATH="$W/bin:$PATH" GIT_CONFIG_NOSYSTEM=1 KORIN_CREDENTIAL_TTY="$W/tty"
unset GIT_TERMINAL_PROMPT GIT_ASKPASS SSH_ASKPASS
git config --global credential.helper korin
git config --global credential.interactive false

ok=0 ko=0
check() { # check DESCRIPTION CONDITION...
	local d=$1
	shift
	if "$@"; then ok=$((ok + 1)); else ko=$((ko + 1)); echo "ECHEC : $d"; fi
}
# fill : rend la sortie de git credential fill (sans demande de git : askpass refusé)
fill() { printf 'protocol=%s\nhost=%s\n%s\n' "$1" "$2" "${3:-}" | GIT_ASKPASS=false git credential fill 2> "$W/err"; }
has() { grep -q -- "$2" <<< "$1"; }

# 1. rien nulle part, hors de GitHub : rien
out=$(fill https example.com)
check "autre hôte : rien" [ -z "$(grep password <<< "$out")" ]
check "autre hôte : oauth pas appelé" [ ! -f "$ST/oauth.log" ]

# 2. coffre : rendu tel quel, pour GitHub aussi
printf 'protocol=https\nhost=example.com\nusername=bob\npassword=pw1\n\n' | git credential approve
out=$(fill https example.com)
check "coffre : nom" has "$out" "username=bob"
check "coffre : mot de passe" has "$out" "password=pw1"

# 3. GitHub, sans terminal ni client_id : rien, et un conseil
rm -f "$W/tty"
out=$(fill https github.com)
check "sans client_id ni terminal : rien" [ -z "$(grep password <<< "$out")" ]
check "sans terminal : conseil oauth setup" grep -q "oauth setup github" "$W/err"
check "sans terminal : pas de compte créé" [ ! -f "$ST/oauth/github.conf" ]

# 4. GitHub, Client ID demandé sur le terminal, puis code d'appareil (Jet absent : pas de question)
printf 'Ov23liAbCdEf123456\n' > "$W/tty"
out=$(fill https github.com)
check "connexion : nom x-access-token" has "$out" "username=x-access-token"
check "connexion : jeton gho_" has "$out" "password=gho_"
check "connexion : code affiché (sortie d'erreur)" grep -q "WDJB-MJHT" "$W/err"
check "connexion : page de l'OAuth App" grep -q "settings/applications/new" "$W/err"
check "connexion : client_id enregistré" grep -q "^client_id=Ov23liAbCdEf123456$" "$ST/oauth/github.conf"
check "connexion : portée repo workflow" grep -q "^scope=repo workflow$" "$ST/oauth/github.conf"
tok=$(cat "$ST/oauth/github.tok")
# git garde le jeton au coffre (store) après un succès
printf '%s\n' "$out" | git credential approve
check "store : au coffre" [ "$(secret get git/github.com/x-access-token)" = "$tok" ]

# 5. ensuite : sans question (le coffre)
rm -f "$W/tty"
: > "$ST/oauth.log"
out=$(fill https github.com)
check "ensuite : même jeton" has "$out" "password=$tok"
check "ensuite : pas de login" bash -c "! grep -q login '$ST/oauth.log'"

# 6. sans coffre (le Pi) : le jeton d'oauthd, sans question
mv "$W/bin/secret" "$W/secret.off"
out=$(fill https github.com)
check "sans coffre : jeton d'oauthd" has "$out" "password=$tok"
check "sans coffre : nom donné par l'adresse gardé" has "$(fill https github.com username=alice)" "username=alice"
mv "$W/secret.off" "$W/bin/secret"

# 7. refusé par GitHub (reject = erase) : jeton oublié chez oauthd et au coffre
printf 'protocol=https\nhost=github.com\nusername=x-access-token\npassword=%s\n\n' "$tok" | git credential reject
check "refus : oublié chez oauthd" [ ! -f "$ST/oauth/github.tok" ]
check "refus : retiré du coffre" [ -z "$(secret get git/github.com/x-access-token)" ]
# un autre mot de passe refusé ne fait pas oublier le compte d'oauthd
echo gho_keep > "$ST/oauth/github.tok"
printf 'protocol=https\nhost=github.com\nusername=other\npassword=nope\n\n' | git credential reject
check "refus d'un autre jeton : compte d'oauthd gardé" [ -f "$ST/oauth/github.tok" ]
rm -f "$ST/oauth/github.tok"

# 8. GIT_TERMINAL_PROMPT=0 : pas de connexion
printf '\n' > "$W/tty"
: > "$ST/oauth.log"
out=$(GIT_TERMINAL_PROMPT=0 fill https github.com)
check "GIT_TERMINAL_PROMPT=0 : rien" [ -z "$(grep password <<< "$out")" ]
check "GIT_TERMINAL_PROMPT=0 : pas de login" bash -c "! grep -q login '$ST/oauth.log'"

# 9. compte configuré, pas connecté : code d'appareil seul (pas de Client ID redemandé)
out=$(fill https github.com)
check "reconnexion : jeton" has "$out" "password=gho_"
check "reconnexion : pas de page d'OAuth App" bash -c "! grep -q settings/applications '$W/err'"

# 10. client_id livré par le système (providers.ini) : pas de question
rm -f "$ST/oauth/github."* "$W/tty"
touch "$ST/sys_cid"
out=$(fill https github.com)
check "client du système : jeton sans terminal" has "$out" "password=gho_"
check "client du système : pas de client_id dans le compte" bash -c "! grep -q client_id '$ST/oauth/github.conf'"

# 11. accès refusé sur GitHub : rien
rm -f "$ST/oauth/github.tok"
touch "$ST/deny"
out=$(fill https github.com)
check "accord refusé : rien" [ -z "$(grep password <<< "$out")" ]
rm -f "$ST/deny"

# 12. http:// (pas https) vers github.com : pas d'oauth
rm -f "$ST/oauth.log"
out=$(fill http github.com)
check "http : pas d'oauth" [ ! -f "$ST/oauth.log" ]

echo "$ok réussis, $ko échecs"
[ $ko -eq 0 ]
