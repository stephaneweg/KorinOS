// host.h -- l'hôte du module Widevine de Google (cdm::Host_10 de l'interface CDM de Chromium ;
// docs/etude/WIDEVINE.md § 3.3) : tampons, minuteries, heure, stockage, identifiant de stockage, et les
// événements du module renvoyés à un « client » (cdmhost : Jet par IPC ; cdmtest : l'essai).
//
// Un seul thread : le module est appelé depuis ce thread, et ses rappels (y compris ceux qu'il demande
// plus tard : minuteries, fichiers, identifiant de stockage) sont servis par pump (), dans ce thread,
// comme le fait Chromium.
#pragma once

#include <stdint.h>

#include <functional>
#include <string>
#include <vector>

#include "content_decryption_module.h"

// ce que le module annonce (les promesses d'EME, les messages de licence, les clés)
class CdmClient
{
public:
	virtual ~CdmClient () {}
	virtual void initialized (bool ok) = 0;
	virtual void new_session (uint32_t promise, const std::string &session) = 0;
	virtual void resolved (uint32_t promise) = 0;
	virtual void rejected (uint32_t promise, uint32_t exception, uint32_t system_code, const std::string &msg) = 0;
	virtual void key_status (uint32_t promise, uint32_t status) = 0;
	virtual void message (const std::string &session, uint32_t type, const uint8_t *data, size_t size) = 0;
	virtual void keys_change (const std::string &session, bool has_new_usable,
				  const std::vector<std::vector<uint8_t>> &kids, const std::vector<uint32_t> &status) = 0;
	virtual void expiration (const std::string &session, double t) = 0;
	virtual void closed (const std::string &session) = 0;
};

class CdmHost : public cdm::Host_10
{
public:
	// storage_dir : où le module garde ses fichiers (CreateFileIO), « » : pas de stockage
	CdmHost (CdmClient *client, const std::string &storage_dir);
	cdm::ContentDecryptionModule_10 *cdm = nullptr;

	// sert les rappels en attente et les minuteries échues ; rend le délai (ms) jusqu'à la prochaine
	// minuterie (-1 : aucune)
	int pump ();
	// pump () pendant ms millisecondes, ou jusqu'à ce que done () soit vrai
	void run (int ms, const std::function<bool ()> &done = nullptr);

	// cdm::Host_10
	cdm::Buffer *Allocate (uint32_t capacity) override;
	void SetTimer (int64_t delay_ms, void *context) override;
	cdm::Time GetCurrentWallTime () override;
	void OnInitialized (bool success) override;
	void OnResolveKeyStatusPromise (uint32_t promise_id, cdm::KeyStatus key_status) override;
	void OnResolveNewSessionPromise (uint32_t promise_id, const char *session_id, uint32_t session_id_size) override;
	void OnResolvePromise (uint32_t promise_id) override;
	void OnRejectPromise (uint32_t promise_id, cdm::Exception exception, uint32_t system_code, const char *error_message,
			      uint32_t error_message_size) override;
	void OnSessionMessage (const char *session_id, uint32_t session_id_size, cdm::MessageType message_type,
			       const char *message, uint32_t message_size) override;
	void OnSessionKeysChange (const char *session_id, uint32_t session_id_size, bool has_additional_usable_key,
				  const cdm::KeyInformation *keys_info, uint32_t keys_info_count) override;
	void OnExpirationChange (const char *session_id, uint32_t session_id_size, cdm::Time new_expiry_time) override;
	void OnSessionClosed (const char *session_id, uint32_t session_id_size) override;
	void SendPlatformChallenge (const char *service_id, uint32_t service_id_size, const char *challenge,
				    uint32_t challenge_size) override;
	void EnableOutputProtection (uint32_t desired_protection_mask) override;
	void QueryOutputProtectionStatus () override;
	void OnDeferredInitializationDone (cdm::StreamType stream_type, cdm::Status decoder_status) override;
	cdm::FileIO *CreateFileIO (cdm::FileIOClient *client) override;
	void RequestStorageId (uint32_t version) override;

	// une tâche à faire au prochain pump () (les rappels « plus tard » du module)
	void post (const std::function<void ()> &task);
	const std::string &storage_dir () const { return storage_dir_; }

private:
	struct Timer
	{
		int64_t due_ms;
		void *context;
	};
	CdmClient *client_;
	std::string storage_dir_;
	std::vector<Timer> timers_;
	std::vector<std::function<void ()>> tasks_;
};

// un tampon de l'hôte (cdm::Buffer), et un bloc déchiffré (cdm::DecryptedBlock)
class CdmBuffer : public cdm::Buffer
{
public:
	explicit CdmBuffer (uint32_t capacity) : data_ (capacity) {}
	void Destroy () override { delete this; }
	uint32_t Capacity () const override { return (uint32_t) data_.size (); }
	uint8_t *Data () override { return data_.data (); }
	void SetSize (uint32_t size) override { size_ = size; }
	uint32_t Size () const override { return size_; }

private:
	std::vector<uint8_t> data_;
	uint32_t size_ = 0;
};

class CdmDecryptedBlock : public cdm::DecryptedBlock
{
public:
	~CdmDecryptedBlock () { if (buffer_) buffer_->Destroy (); }
	void SetDecryptedBuffer (cdm::Buffer *b) override { buffer_ = b; }
	cdm::Buffer *DecryptedBuffer () override { return buffer_; }
	void SetTimestamp (int64_t t) override { timestamp_ = t; }
	int64_t Timestamp () const override { return timestamp_; }

private:
	cdm::Buffer *buffer_ = nullptr;
	int64_t timestamp_ = 0;
};

// une image décodée par le module (DecryptAndDecodeFrame) : plans YUV dans un tampon de l'hôte
class CdmVideoFrame : public cdm::VideoFrame
{
public:
	~CdmVideoFrame () { if (buffer_) buffer_->Destroy (); }
	void SetFormat (cdm::VideoFormat f) override { format_ = f; }
	cdm::VideoFormat Format () const override { return format_; }
	void SetSize (cdm::Size s) override { size_ = s; }
	cdm::Size Size () const override { return size_; }
	void SetFrameBuffer (cdm::Buffer *b) override { buffer_ = b; }
	cdm::Buffer *FrameBuffer () override { return buffer_; }
	void SetPlaneOffset (cdm::VideoPlane p, uint32_t o) override { if (p < cdm::kMaxPlanes) offset_[p] = o; }
	uint32_t PlaneOffset (cdm::VideoPlane p) override { return p < cdm::kMaxPlanes ? offset_[p] : 0; }
	void SetStride (cdm::VideoPlane p, uint32_t s) override { if (p < cdm::kMaxPlanes) stride_[p] = s; }
	uint32_t Stride (cdm::VideoPlane p) override { return p < cdm::kMaxPlanes ? stride_[p] : 0; }
	void SetTimestamp (int64_t t) override { timestamp_ = t; }
	int64_t Timestamp () const override { return timestamp_; }

private:
	cdm::VideoFormat format_ = cdm::kUnknownVideoFormat;
	cdm::Size size_ = { 0, 0 };
	cdm::Buffer *buffer_ = nullptr;
	uint32_t offset_[cdm::kMaxPlanes] = {}, stride_[cdm::kMaxPlanes] = {};
	int64_t timestamp_ = 0;
};

// le temps monotone en millisecondes
int64_t cdm_now_ms ();
