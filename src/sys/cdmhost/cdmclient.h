// cdmclient.h -- le client de cdmhost, dans le process qui joue le média (Jet ; cdmtest --ipc pour les essais) :
// docs/etude/WIDEVINE.md § 3.3, proto.h.
//
// Le fil principal envoie les requêtes des sessions et lit les événements (control_fd () dans sa boucle,
// read_event) ; les fils de décodage appellent decrypt / init_video / decode_video, synchrones, l'un après
// l'autre (un verrou).
#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "proto.h"

struct CdmFrame
{
	uint32_t status = 0, format = 0, width = 0, height = 0;
	uint32_t stride[3] = {}, offset[3] = {};
	int64_t timestamp = 0;
	std::vector<uint8_t> pixels;
};

class CdmHostClient
{
public:
	// lance cdmhost (son chemin), avec le module et le dossier de stockage ; nullptr : échec (stderr)
	static std::unique_ptr<CdmHostClient> spawn (const std::string &cdmhost, const std::string &module, const std::string &storage);
	~CdmHostClient ();

	// le contrôle (le fil principal)
	int control_fd () const { return control_; }
	bool init (const std::string &key_system, bool distinctive_id, bool persistent_state);
	bool set_server_certificate (uint32_t promise, const std::vector<uint8_t> &cert);
	bool create_session (uint32_t promise, uint32_t session_type, uint32_t init_data_type, const std::vector<uint8_t> &data);
	bool update_session (uint32_t promise, const std::string &session, const std::vector<uint8_t> &response);
	bool close_session (uint32_t promise, const std::string &session);
	// le prochain événement (bloquant : après un poll sur control_fd pour ne pas attendre)
	bool read_event (CdmIn &ev);

	// le média (n'importe quel fil, un à la fois) ; rendent le cdm::Status du module, ou -1 si cdmhost est parti
	int decrypt (const CdmCrypt &c, const uint8_t *data, size_t size, std::vector<uint8_t> &out);
	int init_video (uint32_t codec, uint32_t profile, uint32_t width, uint32_t height, const std::vector<uint8_t> &extra);
	int decode_video (const CdmCrypt &c, const uint8_t *data, size_t size, CdmFrame &out);
	int reset_video ();

private:
	CdmHostClient () = default;
	int control_ = -1, media_ = -1;
	pid_t pid_ = -1;
	std::mutex media_lock_;
};
