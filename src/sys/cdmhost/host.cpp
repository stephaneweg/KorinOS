// host.cpp -- l'hôte du module Widevine (host.h).
#include "host.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

int64_t cdm_now_ms ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (int64_t) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static std::string str (const char *p, uint32_t n) { return std::string (p ? p : "", p ? n : 0); }

CdmHost::CdmHost (CdmClient *client, const std::string &storage_dir) : client_ (client), storage_dir_ (storage_dir) {}

void CdmHost::post (const std::function<void ()> &task) { tasks_.push_back (task); }

int CdmHost::pump ()
{
	// les tâches posées pendant qu'on sert les autres attendent le tour suivant
	std::vector<std::function<void ()>> now;
	now.swap (tasks_);
	for (auto &t : now)
		t ();
	int64_t at = cdm_now_ms ();
	for (size_t i = 0; i < timers_.size ();)
		if (timers_[i].due_ms <= at)
		{
			void *c = timers_[i].context;
			timers_.erase (timers_.begin () + (long) i);
			if (cdm)
				cdm->TimerExpired (c);
			i = 0;	// (la liste a pu changer)
		}
		else
			i++;
	int64_t next = -1;
	for (auto &t : timers_)
		if (next < 0 || t.due_ms - at < next)
			next = t.due_ms - at;
	if (!tasks_.empty ())
		next = 0;
	return next < 0 ? -1 : (int) (next < 0 ? 0 : next);
}

void CdmHost::run (int ms, const std::function<bool ()> &done)
{
	int64_t end = cdm_now_ms () + ms;
	for (;;)
	{
		int next = pump ();
		if (done && done ())
			return;
		int64_t left = end - cdm_now_ms ();
		if (left <= 0)
			return;
		int wait = next < 0 || next > 10 ? 10 : next;
		if (wait > left)
			wait = (int) left;
		if (wait > 0)
			usleep ((useconds_t) wait * 1000);
	}
}

cdm::Buffer *CdmHost::Allocate (uint32_t capacity) { return new CdmBuffer (capacity); }

void CdmHost::SetTimer (int64_t delay_ms, void *context)
{
	timers_.push_back ({ cdm_now_ms () + delay_ms, context });
}

cdm::Time CdmHost::GetCurrentWallTime ()
{
	struct timeval tv;
	gettimeofday (&tv, 0);
	return (double) tv.tv_sec + tv.tv_usec / 1e6;
}

void CdmHost::OnInitialized (bool success) { client_->initialized (success); }

void CdmHost::OnResolveKeyStatusPromise (uint32_t promise_id, cdm::KeyStatus key_status)
{
	client_->key_status (promise_id, key_status);
}

void CdmHost::OnResolveNewSessionPromise (uint32_t promise_id, const char *session_id, uint32_t session_id_size)
{
	client_->new_session (promise_id, str (session_id, session_id_size));
}

void CdmHost::OnResolvePromise (uint32_t promise_id) { client_->resolved (promise_id); }

void CdmHost::OnRejectPromise (uint32_t promise_id, cdm::Exception exception, uint32_t system_code,
			       const char *error_message, uint32_t error_message_size)
{
	client_->rejected (promise_id, exception, system_code, str (error_message, error_message_size));
}

void CdmHost::OnSessionMessage (const char *session_id, uint32_t session_id_size, cdm::MessageType message_type,
				const char *message, uint32_t message_size)
{
	client_->message (str (session_id, session_id_size), message_type, (const uint8_t *) message, message_size);
}

void CdmHost::OnSessionKeysChange (const char *session_id, uint32_t session_id_size, bool has_additional_usable_key,
				   const cdm::KeyInformation *keys_info, uint32_t keys_info_count)
{
	std::vector<std::vector<uint8_t>> kids;
	std::vector<uint32_t> status;
	for (uint32_t i = 0; i < keys_info_count; i++)
	{
		kids.emplace_back (keys_info[i].key_id, keys_info[i].key_id + keys_info[i].key_id_size);
		status.push_back (keys_info[i].status);
	}
	client_->keys_change (str (session_id, session_id_size), has_additional_usable_key, kids, status);
}

void CdmHost::OnExpirationChange (const char *session_id, uint32_t session_id_size, cdm::Time new_expiry_time)
{
	client_->expiration (str (session_id, session_id_size), new_expiry_time);
}

void CdmHost::OnSessionClosed (const char *session_id, uint32_t session_id_size)
{
	client_->closed (str (session_id, session_id_size));
}

void CdmHost::SendPlatformChallenge (const char *, uint32_t, const char *, uint32_t)
{
	// (pas de défi de plateforme : réponse vide, comme un Chrome sans attestation)
	post ([this] {
		cdm::PlatformChallengeResponse r = {};
		if (cdm)
			cdm->OnPlatformChallengeResponse (r);
	});
}

void CdmHost::EnableOutputProtection (uint32_t) {}

void CdmHost::QueryOutputProtectionStatus ()
{
	// pas de lien externe protégé à décrire : « aucun lien, aucune protection » (ce que dit Chrome sous Linux)
	post ([this] {
		if (cdm)
			cdm->OnQueryOutputProtectionStatus (cdm::kQuerySucceeded, 0, 0);
	});
}

void CdmHost::OnDeferredInitializationDone (cdm::StreamType, cdm::Status) {}

// ---- stockage : un fichier par nom dans storage_dir ; toutes les réponses viennent au pump () suivant ----
class CdmFileIO : public cdm::FileIO
{
public:
	CdmFileIO (CdmHost *host, cdm::FileIOClient *client) : host_ (host), client_ (client) {}

	void Open (const char *file_name, uint32_t size) override
	{
		std::string n (file_name, size);
		bool ok = !host_->storage_dir ().empty () && !n.empty () && n[0] != '_' && n.find ('/') == std::string::npos
			  && n.find ("..") == std::string::npos;
		if (ok)
			path_ = host_->storage_dir () + "/" + n;
		host_->post ([this, ok] {
			client_->OnOpenComplete (ok ? cdm::FileIOClient::Status::kSuccess : cdm::FileIOClient::Status::kError);
		});
	}

	void Read () override
	{
		host_->post ([this] {
			std::vector<uint8_t> d;
			FILE *f = path_.empty () ? nullptr : fopen (path_.c_str (), "rb");
			if (f)
			{
				uint8_t b[4096];
				size_t k;
				while ((k = fread (b, 1, sizeof b, f)) > 0)
					d.insert (d.end (), b, b + k);
				fclose (f);
			}
			// (un fichier absent se lit vide, comme dans Chromium)
			client_->OnReadComplete (path_.empty () ? cdm::FileIOClient::Status::kError : cdm::FileIOClient::Status::kSuccess,
						 d.data (), (uint32_t) d.size ());
		});
	}

	void Write (const uint8_t *data, uint32_t size) override
	{
		std::vector<uint8_t> d (data, data + size);
		host_->post ([this, d] {
			bool ok = false;
			if (!path_.empty ())
			{
				std::string tmp = path_ + ".tmp";
				FILE *f = fopen (tmp.c_str (), "wb");
				if (f)
				{
					ok = fwrite (d.data (), 1, d.size (), f) == d.size ();
					ok = fclose (f) == 0 && ok;
					ok = ok && rename (tmp.c_str (), path_.c_str ()) == 0;
				}
			}
			client_->OnWriteComplete (ok ? cdm::FileIOClient::Status::kSuccess : cdm::FileIOClient::Status::kError);
		});
	}

	void Close () override
	{
		host_->post ([this] { delete this; });
	}

private:
	CdmHost *host_;
	cdm::FileIOClient *client_;
	std::string path_;
};

cdm::FileIO *CdmHost::CreateFileIO (cdm::FileIOClient *client)
{
	if (storage_dir_.empty ())
		return nullptr;
	mkdir (storage_dir_.c_str (), 0700);
	return new CdmFileIO (this, client);
}

// un identifiant stable de cette machine et de cet hôte, gardé dans storage_dir/storage_id (32 octets aléatoires)
void CdmHost::RequestStorageId (uint32_t version)
{
	post ([this, version] {
		uint8_t id[32] = {};
		bool ok = false;
		if (!storage_dir_.empty () && (version == 0 || version == 1))
		{
			std::string p = storage_dir_ + "/storage_id";
			FILE *f = fopen (p.c_str (), "rb");
			if (f)
			{
				ok = fread (id, 1, sizeof id, f) == sizeof id;
				fclose (f);
			}
			if (!ok)
			{
				FILE *r = fopen ("/dev/urandom", "rb");
				if (r)
				{
					ok = fread (id, 1, sizeof id, r) == sizeof id;
					fclose (r);
				}
				mkdir (storage_dir_.c_str (), 0700);
				f = ok ? fopen (p.c_str (), "wb") : nullptr;
				if (f)
				{
					fwrite (id, 1, sizeof id, f);
					fclose (f);
				}
			}
		}
		if (cdm)
			cdm->OnStorageId (1, ok ? id : nullptr, ok ? (uint32_t) sizeof id : 0);
	});
}
