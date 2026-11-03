// openssltest-cxx.cpp -- la meme bibliotheque depuis le C++ (docs/OPENSSL.md) : les en-tetes sous
// g++, le lien avec libstdc++, std::thread sur libcrypto.
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <openssl/evp.h>

static std::string sha256_hex (const std::string &msg)
{
	unsigned char md[EVP_MAX_MD_SIZE];
	size_t n = sizeof md;
	if (!EVP_Q_digest (nullptr, "SHA256", nullptr, msg.data (), msg.size (), md, &n))
		throw std::runtime_error ("EVP_Q_digest");
	std::string s;
	char h[3];
	for (size_t i = 0; i < n; i++)
	{
		std::snprintf (h, sizeof h, "%02x", md[i]);
		s += h;
	}
	return s;
}

int main ()
{
	const std::string want = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
	std::vector<std::thread> threads;
	std::vector<int> ok (4, 0);
	for (int i = 0; i < 4; i++)
		threads.emplace_back ([&, i] {
			try
			{
				ok[i] = 1;
				for (int j = 0; j < 1000; j++)
					if (sha256_hex ("abc") != want)
						ok[i] = 0;
			}
			catch (const std::exception &)
			{
				ok[i] = 0;
			}
		});
	for (auto &t : threads)
		t.join ();
	int all = ok[0] && ok[1] && ok[2] && ok[3];
	std::printf ("%s openssltest-cxx : SHA-256 par 4 std::thread\n", all ? "ok   " : "ECHEC");
	return !all;
}
