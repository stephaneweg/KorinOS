// libuse : un programme qui utilise une bibliotheque installee par pibuild lib (docs/DOTNET.md).
//     pibuild lib /devtools/dotnet/test/libdemo/lib/Calcul Calcul
//     pibuild bin /devtools/dotnet/test/libuse libuse
using System;

class Programme
{
	static int Main ()
	{
		Console.WriteLine (Calcul.Maths.Plus (2, 40));
		return 0;
	}
}
