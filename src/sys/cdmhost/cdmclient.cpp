// cdmclient.cpp -- le client de cdmhost (cdmclient.h).
#include "cdmclient.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

std::unique_ptr<CdmHostClient> CdmHostClient::spawn (const std::string &cdmhost, const std::string &module, const std::string &storage)
{
	int c[2], m[2];
	if (socketpair (AF_UNIX, SOCK_STREAM, 0, c) || socketpair (AF_UNIX, SOCK_STREAM, 0, m))
	{
		perror ("cdmclient : socketpair");
		return nullptr;
	}
	// (nos côtés ne passent pas dans cdmhost ; les siens, si : ses numéros dans ses arguments)
	fcntl (c[0], F_SETFD, FD_CLOEXEC);
	fcntl (m[0], F_SETFD, FD_CLOEXEC);
	char cfd[16], mfd[16];
	snprintf (cfd, sizeof cfd, "%d", c[1]);
	snprintf (mfd, sizeof mfd, "%d", m[1]);
	pid_t pid = fork ();
	if (pid < 0)
	{
		perror ("cdmclient : fork");
		return nullptr;
	}
	if (pid == 0)
	{
		const char *argv[] = { cdmhost.c_str (), "--control", cfd, "--media", mfd, "--module", module.c_str (),
				       storage.empty () ? nullptr : "--storage", storage.c_str (), nullptr };
		execv (cdmhost.c_str (), (char *const *) argv);
		perror ("cdmclient : exec de cdmhost");
		_exit (127);
	}
	close (c[1]);
	close (m[1]);
	std::unique_ptr<CdmHostClient> h (new CdmHostClient ());
	h->control_ = c[0];
	h->media_ = m[0];
	h->pid_ = pid;
	return h;
}

CdmHostClient::~CdmHostClient ()
{
	// (la fin du contrôle arrête cdmhost)
	if (control_ >= 0)
		close (control_);
	if (media_ >= 0)
		close (media_);
	if (pid_ > 0)
	{
		int st;
		for (int i = 0; i < 100 && waitpid (pid_, &st, WNOHANG) == 0; i++)
			usleep (20000);
		if (waitpid (pid_, &st, WNOHANG) == 0)
		{
			kill (pid_, SIGKILL);
			waitpid (pid_, &st, 0);
		}
	}
}

bool CdmHostClient::init (const std::string &ks, bool distinctive, bool persistent)
{
	CdmOut m (CDM_INIT);
	return cdm_send (control_, m.str (ks).u32 (distinctive).u32 (persistent));
}

bool CdmHostClient::set_server_certificate (uint32_t p, const std::vector<uint8_t> &cert)
{
	CdmOut m (CDM_SET_SERVER_CERT);
	return cdm_send (control_, m.u32 (p).blk (cert.data (), cert.size ()));
}

bool CdmHostClient::create_session (uint32_t p, uint32_t st, uint32_t it, const std::vector<uint8_t> &d)
{
	CdmOut m (CDM_CREATE_SESSION);
	return cdm_send (control_, m.u32 (p).u32 (st).u32 (it).blk (d.data (), d.size ()));
}

bool CdmHostClient::update_session (uint32_t p, const std::string &s, const std::vector<uint8_t> &r)
{
	CdmOut m (CDM_UPDATE_SESSION);
	return cdm_send (control_, m.u32 (p).str (s).blk (r.data (), r.size ()));
}

bool CdmHostClient::close_session (uint32_t p, const std::string &s)
{
	CdmOut m (CDM_CLOSE_SESSION);
	return cdm_send (control_, m.u32 (p).str (s));
}

bool CdmHostClient::read_event (CdmIn &ev) { return cdm_recv (control_, ev); }

int CdmHostClient::decrypt (const CdmCrypt &c, const uint8_t *data, size_t size, std::vector<uint8_t> &out)
{
	std::lock_guard<std::mutex> lock (media_lock_);
	CdmOut m (CDM_DECRYPT);
	CdmIn r;
	if (!cdm_send (media_, m.crypt (c).blk (data, size)) || !cdm_recv (media_, r) || r.type != CDM_DECRYPTED)
		return -1;
	int st = (int) r.u32 ();
	out = r.blk ();
	return r.ok ? st : -1;
}

int CdmHostClient::init_video (uint32_t codec, uint32_t profile, uint32_t w, uint32_t h, const std::vector<uint8_t> &extra)
{
	std::lock_guard<std::mutex> lock (media_lock_);
	CdmOut m (CDM_INIT_VIDEO);
	CdmIn r;
	if (!cdm_send (media_, m.u32 (codec).u32 (profile).u32 (w).u32 (h).blk (extra.data (), extra.size ())) || !cdm_recv (media_, r))
		return -1;
	return (int) r.u32 ();
}

int CdmHostClient::decode_video (const CdmCrypt &c, const uint8_t *data, size_t size, CdmFrame &f)
{
	std::lock_guard<std::mutex> lock (media_lock_);
	CdmOut m (CDM_DECODE_VIDEO);
	CdmIn r;
	if (!cdm_send (media_, m.crypt (c).blk (data, size)) || !cdm_recv (media_, r) || r.type != CDM_FRAME)
		return -1;
	f.status = r.u32 ();
	f.format = r.u32 ();
	f.width = r.u32 ();
	f.height = r.u32 ();
	for (int p = 0; p < 3; p++)
		f.stride[p] = r.u32 ();
	for (int p = 0; p < 3; p++)
		f.offset[p] = r.u32 ();
	f.timestamp = r.i64 ();
	f.pixels = r.blk ();
	return r.ok ? (int) f.status : -1;
}

int CdmHostClient::reset_video ()
{
	std::lock_guard<std::mutex> lock (media_lock_);
	CdmOut m (CDM_RESET_VIDEO);
	CdmIn r;
	if (!cdm_send (media_, m) || !cdm_recv (media_, r))
		return -1;
	return (int) r.u32 ();
}
