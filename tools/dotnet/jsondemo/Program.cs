// jsondemo : Newtonsoft.Json du runtime partage (/dotnet), objets, reflexion, JObject
using System;
using System.Collections.Generic;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

public class Item
{
	public string Name { get; set; } = "";
	public int Count { get; set; }
	public List<double> Values { get; set; } = new();
}

static class Program
{
	static int Main()
	{
		var a = new Item { Name = "armpi", Count = 3, Values = new() { 1.5, 2.5 } };
		string text = JsonConvert.SerializeObject(a);
		Console.WriteLine(text);
		var b = JsonConvert.DeserializeObject<Item>(text)!;
		var o = JObject.Parse(text);
		o["Count"] = 4;
		int sum = 0;
		for (int i = 0; i < 5000; i++)		// (au-dela du seuil des methodes dynamiques)
			sum += JsonConvert.DeserializeObject<Item>(text)!.Count;
		bool ok = b.Name == "armpi" && b.Values.Count == 2 && b.Values[1] == 2.5 && (int)o["Count"]! == 4 && sum == 15000;
		Console.WriteLine(o.ToString(Formatting.None));
		Console.WriteLine(ok ? "jsondemo : OK" : "jsondemo : ECHEC");
		return ok ? 0 : 1;
	}
}
