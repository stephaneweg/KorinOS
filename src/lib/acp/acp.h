/*
 * acp.h -- libacp : client de l'Agent Client Protocol (ACP) pour Korin (docs/PYTHON.md §5, docs/PYTHON-PORT.md
 * « Étape 8 »). L'agent (vibe-acp) est un process fils ; on lui parle en JSON-RPC 2.0, un message par ligne, sur deux
 * tubes ; sa sortie d'erreur va dans un journal.
 *
 * Deux niveaux :
 *   - acp_rpc_* : le JSON-RPC (requêtes en vol et leurs réponses, notifications, requêtes de l'agent) ;
 *   - acp_* : une séance, telle que la façade la voit : initialize, connexion au compte par navigateur au besoin
 *     (browser-auth-delegated : une URL à ouvrir), session/new ou session/load, le mode d'approbation, les tours
 *     (session/prompt), Échap (session/cancel), les approbations (session/request_permission), le flux des mises à
 *     jour (texte, outils, diffs) rendu en événements.
 *
 * Un seul fil : le client appelle acp_pump quand acp_fd est lisible (poll), ou avec un délai ; tous les événements
 * partent de acp_pump. Rien n'est bloquant sauf l'écriture d'un message vers l'agent.
 */
#ifndef ACP_H
#define ACP_H

#include "json.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct acp acp_t;

/* --- niveau JSON-RPC ----------------------------------------------------------------------------------------------- */

typedef void (*acp_reply_fn) (acp_t *a, void *data, const json_t *result, const json_t *error);

/* params_json : un objet JSON déjà écrit (ou NULL pour {}) ; rend l'identifiant de la requête, -1 si l'agent est parti */
long acp_rpc_request (acp_t *a, const char *method, const char *params_json, acp_reply_fn fn, void *data);
int acp_rpc_notify (acp_t *a, const char *method, const char *params_json);
int acp_rpc_respond (acp_t *a, const json_t *id, const char *result_json);
int acp_rpc_error (acp_t *a, const json_t *id, int code, const char *message);

/* --- niveau séance ------------------------------------------------------------------------------------------------- */

enum acp_state {
	ACP_STARTING,		/* initialize envoyé */
	ACP_SIGNING_IN,		/* connexion au compte : l'URL est donnée, on attend */
	ACP_OPENING,		/* session/new ou session/load */
	ACP_IDLE,		/* séance ouverte, pas de tour */
	ACP_TURN,		/* un tour en cours */
	ACP_DEAD		/* l'agent est parti */
};

enum acp_text_kind { ACP_AGENT_TEXT, ACP_THOUGHT_TEXT, ACP_USER_TEXT };

/* un appel d'outil, tel que connu après chaque mise à jour */
typedef struct acp_tool {
	const char *id;
	const char *title;	/* « Reading notes.txt », « bash: git status »... */
	const char *kind;	/* read, edit, execute, search, fetch, think, other... */
	const char *status;	/* pending, in_progress, completed, failed */
	const char *text;	/* sortie de l'outil (blocs de texte de content), "" sinon */
	const char *diff_path;	/* un diff de content : chemin, ancien et nouveau texte ; NULL sans diff */
	const char *diff_old;
	const char *diff_new;
	int is_new;		/* premier événement de cet appel */
} acp_tool;

typedef struct acp_option {
	const char *id;		/* optionId, à rendre par acp_permit */
	const char *name;	/* libellé de l'agent */
	const char *kind;	/* allow_once, allow_always, reject_once, reject_always */
} acp_option;

typedef struct acp_events {
	/* initialize a répondu */
	void (*ready) (void *ud, const char *agent, const char *version);
	/* connexion au compte : l'URL à ouvrir dans un navigateur (sur cet appareil ou un autre) */
	void (*sign_in) (void *ud, const char *url, const char *expires);
	/* ok = 0 : status est le message de Vibe (« Browser sign-in timed out. »...) : acp_sign_in_again pour réessayer */
	void (*signed_in) (void *ud, int ok, const char *status);
	/* séance ouverte (loaded : par session/load ; l'historique a été rejoué par text et tool) */
	void (*session) (void *ud, const char *session_id, const char *mode, int loaded);
	void (*mode) (void *ud, const char *mode);
	void (*text) (void *ud, int kind, const char *text);
	void (*tool) (void *ud, const acp_tool *t);
	/* une approbation : répondre par acp_permit (req, optionId), ou acp_permit (req, NULL) pour annuler ; tool peut
	   être un appel déjà connu (titre, diff) */
	void (*permission) (void *ud, long req, const acp_tool *t, int n, const acp_option *opts);
	void (*turn_end) (void *ud, const char *stop_reason);
	void (*plan) (void *ud, const json_t *entries);
	void (*error) (void *ud, const char *message);
	void (*exited) (void *ud);
	/* ce que l'agent dit (stderr) va dans un journal ; status : une ligne d'état (« démarrage de l'agent... ») */
	void (*status) (void *ud, const char *text);
	/* le contexte : jetons utilisés et taille de la fenêtre du modèle (usage_update) */
	void (*usage) (void *ud, long used, long size);
	/* le modèle de la séance a changé (acp_set_model, ou l'agent) : sa valeur (alias) et son nom */
	void (*model) (void *ud, const char *value, const char *name);
} acp_events;

typedef struct acp_options {
	char *const *argv;	/* la commande de l'agent ; NULL : vibe-acp */
	const char *cwd;	/* dossier de la séance ; NULL : le dossier courant */
	const char *mode;	/* mode voulu après l'ouverture (ask...) ; NULL : celui de l'agent */
	const char *load;	/* session/load de cet identifiant ; "last" : la plus récente du dossier ; NULL : session/new */
	const char *log;	/* journal de la sortie d'erreur de l'agent ; NULL : /tmp/acp-agent.log */
	const char *client;	/* nom du client (clientInfo) */
	int sign_in;		/* connexion au compte quand session/new dit qu'il manque une clé (1 par défaut dans agent) */
} acp_options;

acp_t *acp_start (const acp_options *o, const acp_events *ev, void *ud);
int acp_fd (acp_t *a);				/* la sortie de l'agent, pour poll ; -1 s'il est parti */
int acp_pump (acp_t *a, int timeout_ms);	/* lit et traite ; -1 quand l'agent est parti */
int acp_state (acp_t *a);
const char *acp_session_id (acp_t *a);
const char *acp_mode (acp_t *a);
const char *acp_modes (acp_t *a);		/* les modes offerts, séparés par des espaces */
const char *acp_model (acp_t *a);		/* le modèle (option « model » de la séance), "" s'il n'est pas dit */
/* les modèles que la séance offre (option « model » de configOptions, ceux de config.toml de Vibe) : la valeur
   (l'alias, à donner à acp_set_model) et le nom affiché ; acp_model_id : la valeur courante */
int acp_model_count (acp_t *a);
const char *acp_model_value (acp_t *a, int i);
const char *acp_model_name (acp_t *a, int i);
const char *acp_model_id (acp_t *a);
/* le modèle de la séance (session/set_config_option « model ») ; l'événement model suit la réponse */
int acp_set_model (acp_t *a, const char *value);

/* une autre séance dans le même agent (sans attendre son redémarrage) : load comme dans acp_options (NULL : neuve) ;
   cwd NULL : le même dossier */
int acp_open (acp_t *a, const char *load, const char *cwd);
const char *acp_cwd (acp_t *a);
const char *acp_log (acp_t *a);			/* le journal de la sortie d'erreur de l'agent */
/* une connexion au compte qui n'a pas abouti (signed_in avec ok = 0 : délai dépassé, refusée...) : un nouvel essai,
   avec une nouvelle URL (événement sign_in) ; 0 si l'état ne le permet pas */
int acp_sign_in_again (acp_t *a);
/* la plus ancienne requête de l'agent restée sans réponse (initialize, session/new... ; pas session/prompt) et depuis
   combien de secondes : pour dire ce qui attend, et proposer de relancer ; NULL si rien n'attend */
const char *acp_waiting (acp_t *a, double *seconds);

int acp_prompt (acp_t *a, const char *text);	/* un tour ; 0 si l'état ne le permet pas */
int acp_cancel (acp_t *a);			/* Échap : session/cancel du tour en cours */
int acp_set_mode (acp_t *a, const char *mode);
int acp_permit (acp_t *a, long req, const char *option_id);
int acp_pending_permission (acp_t *a);		/* une approbation attend sa réponse */
void acp_close (acp_t *a);			/* ferme les tubes, attend l'agent (2 s), puis le tue */

/* le dossier de l'agent pour le compte en cours (home.c) : $VIBE_HOME, sinon $HOME/.local/vibe (claude :
   $CLAUDE_ACP_HOME, $HOME/.local/claude-acp ; mai : $MAI_ACP_HOME, $HOME/.local/mai-acp), créé ; les compétences et
   AGENTS.md des paquets (/.vibe,
   /.claude-acp, /.mai-acp) y sont recopiés, et ce qu'un compte avait laissé là avant les comptes y est déplacé une
   fois. Rend 0. */
enum { ACP_PROFILE_VIBE, ACP_PROFILE_CLAUDE, ACP_PROFILE_MAI };
int acp_home (int profile, char *out, unsigned long cap);

#ifdef __cplusplus
}
#endif

#endif
