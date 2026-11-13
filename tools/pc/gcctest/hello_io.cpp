// hello_io.cpp : iostream, string, vector, sort (essai de g++ dans Korin PC)
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

int main ()
{
	std::vector<std::string> v {"x86-64", "Korin", "g++"};
	std::sort (v.begin (), v.end ());
	std::cout << "bonjour de iostream :";
	for (const auto &s : v)
		std::cout << ' ' << s;
	std::cout << std::endl;
	return 0;
}
