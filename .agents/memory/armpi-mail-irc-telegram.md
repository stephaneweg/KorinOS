---
name: armpi-mail-irc-telegram
description: "ports mail, irc, telegram d'Onyx, dans main (4eea956, 01/11/2026), serveurs d'essai tools/nettest, jamais essayés sur un vrai service ni sur le Pi"
metadata:
  node_type: memory
  type: project
  originSessionId: fe758f82-6f1c-4681-8c1d-22efb16a772b
  modified: 2026-11-01T00:40:29.395Z
---

Ports de `mail`, `irc`, `telegram` dans main depuis le 01/11/2026 (fusion 4eea956), essayés sous QEMU seulement, non poussés.

**Why:** les essais se font sur `tools/nettest/` (`ircd.py`, `mailsrv.py`, `tgsrv.py`), pas sur de vrais comptes ; deux essais ont touché un serveur public par accident (libera.chat par le serveur par défaut d'irc, pool.ntp.org par netd).

**How to apply:** sous QEMU, ne jamais lancer ces apps sans cible locale : telegram sans `test_host` joint la production dès le lancement ; mettre `ntp=10.0.2.2` dans `/etc/options.txt` de la carte d'essai. Les écritures carte et la lecture de TTF attendent le préchargement de Jet (~130 s) : scénarios à `wait 135`. mail et telegram ne se lient que sur le PC (OpenSSL hors de /gcc), irc reste compilable en natif. Reste à faire quand le chrome sans barre arrive dans main : ces trois apps ne laissent pas `FRAME_CAP_W` libres, voir [[armpi-chrome-notitle]]. Voir aussi [[armpi-ports-sans-pont]], [[armpi-retrait-tlskit]].
