using System;

static class Program
{
	static int Main ()
	{
#if MARQUE && RELEASE && NET10_0_OR_GREATER
		Console.WriteLine (MLib.Hello.Text () + " " + Sub.Value + " " + Common.Value + " " + More.Value);
		return 0;
#else
		return 1;
#endif
	}
}
