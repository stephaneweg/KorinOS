// proto.cpp -- l'envoi et la réception des messages entre cdmclient et cdmhost (proto.h).
#include <errno.h>
#include <unistd.h>

#include "proto.h"

bool cdm_send (int fd, CdmOut &m)
{
	const std::vector<uint8_t> &b = m.done ();
	size_t off = 0;
	while (off < b.size ())
	{
		ssize_t k = write (fd, b.data () + off, b.size () - off);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0)
			return false;
		off += (size_t) k;
	}
	return true;
}

static bool read_all (int fd, void *p, size_t n)
{
	size_t off = 0;
	while (off < n)
	{
		ssize_t k = read (fd, (uint8_t *) p + off, n - off);
		if (k < 0 && errno == EINTR)
			continue;
		if (k <= 0)
			return false;
		off += (size_t) k;
	}
	return true;
}

bool cdm_recv (int fd, CdmIn &m)
{
	uint32_t n, type;
	if (!read_all (fd, &n, 4) || n < 4 || n > (64u << 20) || !read_all (fd, &type, 4))
		return false;
	m.type = type;
	m.at = 0;
	m.ok = true;
	m.b.resize (n - 4);
	return n == 4 || read_all (fd, m.b.data (), n - 4);
}
