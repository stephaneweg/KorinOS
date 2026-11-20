// net.h -- des requêtes HTTP et HTTPS bloquantes (OpenSSL, HTTP/1.0) : l'échange de licence des essais (cdmtest,
// ipctest), et la mise à jour du module (cdmupdate : le serveur de composants de Google, puis le téléchargement).
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

// POST : le corps de la réponse dans out, le statut HTTP dans *status ; false : pas de réponse (message sur
// stderr) ; attend jusqu'à 90 s que le nom se résolve (le réseau au démarrage)
bool https_post (const std::string &url, const std::vector<uint8_t> &body, std::vector<uint8_t> &out, int *status,
		 const char *content_type = "application/octet-stream");

// GET (http:// ou https://), en suivant jusqu'à 5 redirections ; max : la taille la plus grande acceptée
bool http_get (const std::string &url, std::vector<uint8_t> &out, int *status, size_t max = 64u << 20);
