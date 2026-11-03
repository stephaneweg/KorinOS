// libdemo : un programme et deux bibliotheques de lib/ (docs/GCC.md).
//     pibuild /dotnet/test/libdemo libdemo
using System;

class Programme
{
	static int Main (string[] args)
	{
		Console.WriteLine (Texte.Phrase.Somme (args.Length, 40));
		return args.Length;
	}
}
