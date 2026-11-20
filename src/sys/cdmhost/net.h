// net.h -- une requête HTTPS POST bloquante (OpenSSL) : l'échange de licence des essais (cdmtest, ipctest).
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

// le corps de la réponse dans out, le statut HTTP dans *status ; false : pas de réponse (message sur stderr) ;
// attend jusqu'à 90 s que le nom se résolve (le réseau au démarrage)
bool https_post (const std::string &url, const std::vector<uint8_t> &body, std::vector<uint8_t> &out, int *status);
