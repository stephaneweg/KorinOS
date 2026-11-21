"""oauthsrv.py -- un faux serveur d'autorisation OAuth 2.0 pour essayer oauthd (docs/OAUTH.md), en clair
(oauthd accepte http:// pour une adresse locale ou privée seulement).

  python -I tools/nettest/oauthsrv.py [PORT]        (8090 par défaut ; QEMU : http://10.0.2.2:8090)

  POST /device        code d'appareil (RFC 8628) : accordé à la 3e demande de jeton
  GET  /auth          code d'autorisation : répond 302 vers redirect_uri?code=...&state=...
  POST /token         device_code, authorization_code (PKCE S256 vérifié), refresh_token (nouveau à chaque fois)
  GET  /revoke        les jetons de rafraîchissement ne valent plus rien (invalid_grant)
  GET  /stats         compteurs (JSON)
  POST /gh/device     à la façon de GitHub (OAuth App, Device Flow) : réponses en formulaire sans
  POST /gh/token      « Accept: application/json », erreurs avec le statut 200, 1re demande en attente, 2e
                      slow_down (intervalle 2), 3e : jeton « gho_… » sans durée ni jeton de rafraîchissement ;
                      scope « deny » : access_denied

client_id attendu : « cid » ; client_secret : aucun, ou « sec ».
"""
import base64
import hashlib
import json
import secrets
import sys
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

STATE = {"device": {}, "codes": {}, "refresh": set(), "n": 0, "stats": {}}


def tok():
    STATE["n"] += 1
    return STATE["n"]


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        sys.stderr.write("oauthsrv: " + fmt % a + "\n")

    def send_json(self, code, obj, chunked=False, form=False):
        if form:  # (GitHub sans « Accept: application/json »)
            body = urllib.parse.urlencode(obj).encode()
        else:
            body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/x-www-form-urlencoded" if form else "application/json")
        if chunked:  # (pour essayer le décodage des morceaux)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(0, len(body), 7):
                part = body[i:i + 7]
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    def count(self, k):
        STATE["stats"][k] = STATE["stats"].get(k, 0) + 1

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        if u.path == "/auth":
            self.count("auth")
            if q.get("client_id") != "cid" or q.get("code_challenge_method") != "S256" or q.get("response_type") != "code":
                return self.send_json(400, {"error": "invalid_request"})
            code = "code-%d" % tok()
            STATE["codes"][code] = (q["code_challenge"], q["redirect_uri"])
            loc = q["redirect_uri"] + "?" + urllib.parse.urlencode({"code": code, "state": q.get("state", "")})
            self.send_response(302)
            self.send_header("Location", loc)
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif u.path == "/revoke":
            STATE["refresh"].clear()
            self.send_json(200, {"revoked": True})
        elif u.path == "/stats":
            self.send_json(200, STATE["stats"])
        else:
            self.send_json(404, {"error": "not_found"})

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        f = dict(urllib.parse.parse_qsl(self.rfile.read(n).decode()))
        if f.get("client_id") != "cid" or f.get("client_secret", "sec") != "sec":
            return self.send_json(401, {"error": "invalid_client", "error_description": "client inconnu"})
        if self.path.startswith("/gh/"):
            return self.github(f)
        if self.path == "/device":
            self.count("device")
            dc = secrets.token_hex(8)
            STATE["device"][dc] = 0
            return self.send_json(200, {"device_code": dc, "user_code": "ABCD-EFGH",
                                        "verification_uri": "http://10.0.2.2:8090/link",
                                        "verification_uri_complete": "http://10.0.2.2:8090/link?c=ABCD-EFGH",
                                        "expires_in": 600, "interval": 1})
        if self.path != "/token":
            return self.send_json(404, {"error": "not_found"})
        g = f.get("grant_type")
        self.count(g or "?")
        if g == "urn:ietf:params:oauth:grant-type:device_code":
            dc = f.get("device_code")
            if dc not in STATE["device"]:
                return self.send_json(400, {"error": "expired_token"})
            STATE["device"][dc] += 1
            if STATE["device"][dc] < 3:
                return self.send_json(400, {"error": "authorization_pending"})
            del STATE["device"][dc]
        elif g == "authorization_code":
            c = STATE["codes"].pop(f.get("code"), None)
            if not c:
                return self.send_json(400, {"error": "invalid_grant", "error_description": "code inconnu"})
            ver = f.get("code_verifier", "")
            ch = base64.urlsafe_b64encode(hashlib.sha256(ver.encode()).digest()).rstrip(b"=").decode()
            if ch != c[0] or f.get("redirect_uri") != c[1]:
                return self.send_json(400, {"error": "invalid_grant", "error_description": "PKCE ou redirect_uri"})
        elif g == "refresh_token":
            r = f.get("refresh_token")
            if r not in STATE["refresh"]:
                return self.send_json(400, {"error": "invalid_grant", "error_description": "jeton révoqué"})
            STATE["refresh"].discard(r)
        else:
            return self.send_json(400, {"error": "unsupported_grant_type"})
        k = tok()
        rt = "rt-%d-%s" % (k, secrets.token_hex(4))
        STATE["refresh"].add(rt)
        self.send_json(200, {"access_token": "at-%d" % k, "token_type": "Bearer", "expires_in": 3600,
                             "refresh_token": rt}, chunked=(g == "refresh_token"))

    def github(self, f):
        form = "application/json" not in self.headers.get("Accept", "")
        if form:
            self.count("gh_form")
        if self.path == "/gh/device":
            self.count("gh_device")
            dc = secrets.token_hex(8)
            STATE["device"][dc] = -1 if f.get("scope") == "deny" else 0
            return self.send_json(200, {"device_code": dc, "user_code": "WDJB-MJHT",
                                        "verification_uri": "https://github.com/login/device",
                                        "expires_in": 900, "interval": 1}, form=form)
        if self.path != "/gh/token":
            return self.send_json(404, {"error": "not_found"})
        self.count("gh_token")
        if f.get("grant_type") != "urn:ietf:params:oauth:grant-type:device_code":
            return self.send_json(200, {"error": "unsupported_grant_type"}, form=form)
        dc = f.get("device_code")
        if dc not in STATE["device"]:
            return self.send_json(200, {"error": "expired_token"}, form=form)
        if STATE["device"][dc] < 0:
            del STATE["device"][dc]
            return self.send_json(200, {"error": "access_denied",
                                        "error_description": "The authorization request was denied."}, form=form)
        STATE["device"][dc] += 1
        if STATE["device"][dc] == 1:
            return self.send_json(200, {"error": "authorization_pending"}, form=form)
        if STATE["device"][dc] == 2:
            self.count("gh_slow_down")
            return self.send_json(200, {"error": "slow_down", "interval": 2}, form=form)
        del STATE["device"][dc]
        self.send_json(200, {"access_token": "gho_%d%s" % (tok(), secrets.token_hex(8)), "token_type": "bearer",
                             "scope": "repo,workflow"}, form=form)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8090
    ThreadingHTTPServer(("0.0.0.0", port), H).serve_forever()
