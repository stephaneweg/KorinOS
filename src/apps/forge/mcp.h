// mcp.h -- Forge : le serveur MCP (Model Context Protocol) qui laisse un agent (Claude Code...) piloter Forge.
//
// Transport « Streamable HTTP » (MCP 2025-06-18), sans flux SSE : chaque message JSON-RPC arrive en POST sur /mcp et
// sa réponse repart en application/json (202 pour une notification). Un thread accepte les connexions ; le message est
// passé à la boucle de l'interface, qui l'exécute entre deux événements (OpenCASCADE et UIKit ne servent qu'à elle),
// puis la réponse repart. Un jeton (« Authorization: Bearer ») est demandé si un jeton est donné ; une requête qui
// porte un en-tête Origin (un navigateur) est refusée.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#pragma once
#include <functional>
#include <string>

namespace forge {
namespace mcp {

// écoute sur toutes les adresses, au port donné -> faux (et err) si le port n'a pas pu être pris
bool start (int port, const std::string &token, std::string &err);
void stop ();
bool running ();
int port ();
// nombre de messages traités depuis le départ
long served ();

// (boucle de l'interface) exécute le message en attente, s'il y en a un -> vrai si un message a été traité.
// handler : le message JSON-RPC reçu -> la réponse ("" : pas de réponse, une notification)
bool poll (const std::function<std::string (const std::string &)> &handler);

// base64 (les images des captures)
std::string base64 (const unsigned char *p, size_t n);

} // namespace mcp
} // namespace forge
