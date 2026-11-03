#include <cstdio>
#include <numeric>
#include <vector>
#include "commun.h"

int main ()
{
	std::vector<int> v { double_de (10), ajoute (20, 2) };
	std::printf ("somme %d\n", std::accumulate (v.begin (), v.end (), 0));
	return 0;
}
