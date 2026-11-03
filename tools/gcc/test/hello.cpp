// essai de G++ sous ARMPI (docs/GCC.md) : STL, heritage, lambda, exceptions
#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct Base
{
	virtual ~Base () {}
	virtual int f (int) = 0;
};

struct Impl : Base
{
	std::function<int (int)> g;
	int f (int x) override { return g (x); }
};

int main ()
{
	std::vector<std::unique_ptr<Base>> v;
	auto a = std::make_unique<Impl> ();
	a->g = [] (int x) { return x + 1; };
	v.push_back (std::move (a));
	std::map<std::string, int> m;
	m["reponse"] = v[0]->f (41);
	try
	{
		throw std::runtime_error ("exception attrapee");
	}
	catch (const std::exception &e)
	{
		std::printf ("bonjour de g++ sous ARMPI : %s, reponse = %d\n", e.what (), m["reponse"]);
	}
	return 0;
}
