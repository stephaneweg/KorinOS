// pathtest : les chemins d'ARMPI vus de System.IO.
using System;
using System.IO;

static class P
{
	static void Try (string name, Func<string> f)
	{
		try { Console.WriteLine (name + " : " + f ()); }
		catch (Exception e) { Console.WriteLine (name + " : KO " + e.GetType ().Name + " " + e.Message); }
	}

	static void Main ()
	{
		Try ("cwd", () => Environment.CurrentDirectory);
		Try ("full SD0", () => Path.GetFullPath ("SD0:/etc/theme.txt"));
		Try ("lire SD0", () => File.ReadAllText ("SD0:/etc/theme.txt").Length.ToString ());
		Try ("lire /etc", () => File.ReadAllText ("/etc/theme.txt").Length.ToString ());
		Try ("mkdir SD0", () => Directory.CreateDirectory ("SD0:/home/ptest/a").FullName);
		Try ("ecrire SD0", () => { File.WriteAllText ("SD0:/home/ptest/a/x.txt", "bonjour"); return "ok"; });
		Try ("mkdir /home", () => Directory.CreateDirectory ("/home/ptest/b").FullName);
		Try ("ecrire /home", () => { File.WriteAllText ("/home/ptest/b/y.txt", "bonjour"); return "ok"; });
		Try ("lire /home", () => File.ReadAllText ("/home/ptest/b/y.txt"));
		Try ("lister", () => string.Join (",", Directory.GetFiles ("/home/ptest/b")));
		Try ("lister SD0", () => string.Join (",", Directory.GetFiles ("SD0:/home/ptest/b")));
		Try ("existe", () => File.Exists ("SD0:/home/ptest/a/x.txt") + " " + File.Exists ("/home/ptest/a/x.txt") + " " + Directory.Exists ("SD0:/home"));
		Try ("effacer", () => { File.Delete ("/home/ptest/b/y.txt"); return File.Exists ("/home/ptest/b/y.txt").ToString (); });
		Try ("env HOME", () => Environment.GetEnvironmentVariable ("HOME") + " " + Environment.GetFolderPath (Environment.SpecialFolder.UserProfile));
		Try ("heure", () => DateTime.Now.ToString ("O") + " " + DateTime.UtcNow.ToString ("O"));
		Try ("guid", () => Guid.NewGuid ().ToString ());
	}
}
