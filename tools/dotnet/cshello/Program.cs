// cshello : un programme C# compile dans ARMPI par pibuild (docs/DOTNET.md), qui appelle un kit.
using System;
using System.AudioKit;
using System.Linq;

class Program
{
	static void Main (string[] args)
	{
		var notes = Enumerable.Range (60, 3).Select (n => AK.ak_note_mhz (n) / 1000).ToArray ();
		Console.WriteLine ("cshello compile sous ARMPI : " + args.Length + " argument(s), la4 = " + AK.ak_note_mhz (69) / 1000 + " Hz, do mi : " + string.Join (" ", notes));
	}
}
