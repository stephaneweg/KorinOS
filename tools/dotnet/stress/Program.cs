// stress : le runtime sous charge, plusieurs cœurs (docs/DOTNET.md, « Le PC ») : ramasse-miettes avec des graphes
// d'objets vivants partagés entre threads, exceptions lancées dans tous les threads, réflexion et code émis
// (DynamicMethod : le JIT pendant que le GC tourne), arrêt des threads pendant un calcul long.
//     dotnet stress.dll [secondes] [threads]      (défaut : 30 s, deux par cœur)
// Chaque partie se juge seule ; le code de sortie est le nombre d'échecs.
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Threading;
using System.Threading.Tasks;

static class Stress
{
	static int s_fail;

	sealed class Node
	{
		public Node? Left, Right;
		public long Value;
		public byte[] Pad;
		public Node (long v, int pad) { Value = v; Pad = new byte[pad]; Pad[0] = (byte) v; }
	}

	static Node Tree (int depth, long seed)
	{
		var n = new Node (seed, (int) (seed % 200) + 8);
		if (depth > 0)
		{
			n.Left = Tree (depth - 1, seed * 2 + 1);
			n.Right = Tree (depth - 1, seed * 2 + 2);
		}
		return n;
	}

	static long Sum (Node? n) => n == null ? 0 : n.Value + n.Pad[0] - (byte) n.Value + Sum (n.Left) + Sum (n.Right);

	static void Report (string name, bool ok, Stopwatch w, string detail = "")
	{
		if (!ok) s_fail++;
		Console.WriteLine ((ok ? "PASS " : "FAIL ") + name + " (" + w.ElapsedMilliseconds + " ms" + (detail.Length > 0 ? ", " + detail : "") + ")");
	}

	static int Main (string[] args)
	{
		int seconds = args.Length > 0 ? int.Parse (args[0]) : 30;
		int threads = args.Length > 1 ? int.Parse (args[1]) : Environment.ProcessorCount * 2;
		Console.WriteLine ($"stress : {seconds} s, {threads} threads, {Environment.ProcessorCount} coeurs, GC serveur {System.Runtime.GCSettings.IsServerGC}");

		// 1. GC : chaque thread garde des arbres, en jette, en partage avec les autres ; les sommes sont relues
		var w = Stopwatch.StartNew ();
		var shared = new ConcurrentQueue<(Node, long)> ();
		long built = 0, verified = 0, bad = 0;
		var end = DateTime.UtcNow.AddSeconds (seconds);
		var ts = Enumerable.Range (0, threads).Select (i => new Thread (() =>
		{
			var rnd = new Random (i);
			var keep = new List<(Node, long)> ();
			while (DateTime.UtcNow < end)
			{
				var t = Tree (rnd.Next (4, 11), rnd.Next ());
				long s = Sum (t);
				Interlocked.Increment (ref built);
				if (rnd.Next (4) == 0)
					shared.Enqueue ((t, s));
				else
					keep.Add ((t, s));
				if (keep.Count > 40)
					keep.RemoveRange (0, 20);
				if (shared.TryDequeue (out var o))
				{
					if (Sum (o.Item1) != o.Item2) Interlocked.Increment (ref bad);
					Interlocked.Increment (ref verified);
				}
				_ = new byte[rnd.Next (1, 100000)];	// (des déchets de toutes tailles, grands objets compris)
			}
			foreach (var k in keep)
				if (Sum (k.Item1) != k.Item2) Interlocked.Increment (ref bad);
		})).ToArray ();
		foreach (var t in ts) t.Start ();
		foreach (var t in ts) t.Join ();
		Report ("GC sous charge", bad == 0 && built > 0, w,
			$"{built} arbres, {verified} relus par un autre thread, {GC.CollectionCount (0)}/{GC.CollectionCount (1)}/{GC.CollectionCount (2)} collectes, {bad} faux");

		// 2. exceptions dans tous les threads à la fois (déroulement, filtres, finally)
		w.Restart ();
		long caught = 0, finals = 0;
		Parallel.For (0, threads * 200, new ParallelOptions { MaxDegreeOfParallelism = threads }, i =>
		{
			try
			{
				try { Thrower (i % 7, i); }
				finally { Interlocked.Increment (ref finals); }
			}
			catch (InvalidOperationException e) when (e.Message == "io" + i) { Interlocked.Increment (ref caught); }
			catch (ArgumentException) { Interlocked.Increment (ref caught); }
			catch (NullReferenceException) { Interlocked.Increment (ref caught); }
			catch (DivideByZeroException) { Interlocked.Increment (ref caught); }
			catch (IndexOutOfRangeException) { Interlocked.Increment (ref caught); }
		});
		Report ("exceptions dans tous les threads", caught == threads * 200 && finals == threads * 200, w, $"{caught} prises");

		// 3. réflexion et code émis, en parallèle, pendant des collectes
		w.Restart ();
		long okEmit = 0;
		var gcThread = new Thread (() => { for (int k = 0; k < 50; k++) { GC.Collect (); Thread.Sleep (5); } });
		gcThread.Start ();
		Parallel.For (0, threads * 4, i =>
		{
			var dm = new DynamicMethod ("add" + i, typeof (long), new[] { typeof (long), typeof (long) });
			var il = dm.GetILGenerator ();
			il.Emit (OpCodes.Ldarg_0);
			il.Emit (OpCodes.Ldarg_1);
			il.Emit (OpCodes.Add);
			il.Emit (OpCodes.Ldc_I8, (long) i);
			il.Emit (OpCodes.Mul);
			il.Emit (OpCodes.Ret);
			var f = (Func<long, long, long>) dm.CreateDelegate (typeof (Func<long, long, long>));
			var m = typeof (Math).GetMethod ("Max", new[] { typeof (int), typeof (int) })!;
			var gm = typeof (Enumerable).GetMethods ().First (x => x.Name == "Sum" && x.GetParameters ().Length == 1 &&
				x.GetParameters ()[0].ParameterType == typeof (IEnumerable<int>));
			if (f (2, 3) == 5L * i && (int) m.Invoke (null, new object[] { i, 3 })! == Math.Max (i, 3) &&
			    (int) gm.Invoke (null, new object[] { new[] { i, 1 } })! == i + 1)
				Interlocked.Increment (ref okEmit);
		});
		gcThread.Join ();
		Report ("reflexion et DynamicMethod pendant le GC", okEmit == threads * 4, w, $"{okEmit} methodes");

		// 4. arrêt des threads pendant un calcul long (boucles sans appel : le GC doit les suspendre)
		w.Restart ();
		var stop = false;
		long spins = 0;
		var loops = Enumerable.Range (0, Environment.ProcessorCount).Select (i => new Thread (() =>
		{
			long x = 0;
			while (!Volatile.Read (ref stop))
				for (int k = 0; k < 1000000; k++) x += k ^ i;
			Interlocked.Add (ref spins, x & 1);
		})).ToArray ();
		foreach (var t in loops) t.Start ();
		for (int k = 0; k < 20; k++) { _ = new byte[1 << 20]; GC.Collect (2, GCCollectionMode.Forced, true); }
		Volatile.Write (ref stop, true);
		foreach (var t in loops) t.Join ();
		Report ("GC pendant des boucles de calcul", true, w, "20 collectes completes");

		// 5. débordement de pile d'un thread : attrapé ? (non : le process meurt ; essai à part, « dotnet stress.dll so »)
		Console.WriteLine ($"stress : {s_fail} echec(s)");
		return s_fail;
	}

	static void Thrower (int kind, int i)
	{
		int[] a = new int[1];
		object? o = null;
		int z = kind - kind;
		switch (kind)
		{
		case 0: throw new InvalidOperationException ("io" + i);
		case 1: throw new ArgumentException ("a");
		case 2: _ = o!.GetHashCode (); break;
		case 3: _ = i / z; break;
		case 4: _ = a[i + 2]; break;
		case 5: Deeper (20, i); break;
		default: throw new ArgumentNullException ("n");
		}
	}

	static void Deeper (int n, int i)
	{
		if (n == 0) throw new InvalidOperationException ("io" + i);
		try { Deeper (n - 1, i); }
		finally { _ = n; }
	}
}
