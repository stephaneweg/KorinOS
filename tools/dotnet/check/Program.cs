// L'essai du framework sous ARMPI (tools/dotnet/hello.sh check) : chaque partie se juge seule,
// une exception ne coupe pas les suivantes.
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Numerics;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

static class Check
{
    static int s_pass, s_fail;

    static void Run(string name, Func<bool> f)
    {
        var w = Stopwatch.StartNew();
        try
        {
            bool ok = f();
            if (ok) s_pass++; else s_fail++;
            Console.WriteLine((ok ? "PASS " : "FAIL ") + name + " (" + w.ElapsedMilliseconds + " ms)");
        }
        catch (Exception e)
        {
            s_fail++;
            Console.WriteLine("FAIL " + name + " : " + e.GetType().Name + " : " + e.Message);
        }
    }

    static object? s_null;
    static int s_zero;

    sealed class Fin { public static int Count; ~Fin() { Interlocked.Increment(ref Count); } }

    static int Deep(int n) => n == 0 ? 0 : 1 + Deep(n - 1);

    static async Task<int> AsyncAdd(int a, int b) { await Task.Delay(10); return a + b; }

    static void Main()
    {
        var inv = System.Globalization.CultureInfo.InvariantCulture;
        Run("chaines et format", () => string.Format("{0:D3}|{1:X}", 7, 255) == "007|FF");
        Run("double : ToString, Parse", () => double.Parse("2.5", inv) * 2 == 5.0 && Math.Sqrt(2).ToString("F4", inv) == "1.4142");
        Run("collections", () =>
        {
            var d = new Dictionary<string, int>();
            for (int i = 0; i < 10000; i++) d["k" + i] = i;
            var l = new List<int>(d.Values); l.Sort();
            return l[9999] == 9999 && new HashSet<int>(l).Count == 10000;
        });
        Run("LINQ", () => Enumerable.Range(1, 1000).Where(x => x % 3 == 0).Select(x => x * x).OrderByDescending(x => x).First() == 999 * 999);
        Run("exception", () => { try { throw new InvalidOperationException("x"); } catch (InvalidOperationException e) { return e.Message == "x"; } });
        Run("exception : finally, relance", () =>
        {
            int f = 0;
            try { try { throw new ArgumentException("a"); } finally { f = 1; } }
            catch (ArgumentException) { return f == 1; }
        });
        Run("NullReferenceException (faute materielle)", () => { try { return s_null!.GetHashCode() == 12345; } catch (NullReferenceException) { return true; } });
        Run("DivideByZeroException", () => { try { return 1 / s_zero == 0; } catch (DivideByZeroException) { return true; } });
        Run("IndexOutOfRangeException", () => { try { var a = new int[2]; return a[s_zero + 5] == 0; } catch (IndexOutOfRangeException) { return true; } });
        Run("pile profonde (100 000 appels)", () =>
        {
            int r = 0;
            var t = new Thread(() => r = Deep(100000), 16 * 1024 * 1024);
            t.Start(); t.Join();
            return r == 100000;
        });
        Run("threads et lock", () =>
        {
            int n = 0; object o = new object();
            var ts = Enumerable.Range(0, 8).Select(_ => new Thread(() => { for (int i = 0; i < 50000; i++) lock (o) n++; })).ToArray();
            foreach (var t in ts) t.Start();
            foreach (var t in ts) t.Join();
            return n == 400000;
        });
        Run("ThreadPool, Task", () => Task.WhenAll(Enumerable.Range(0, 32).Select(i => Task.Run(() => i * 2))).Result.Sum() == 992);
        Run("async / await, Task.Delay", () => AsyncAdd(2, 3).Result == 5);
        Run("Parallel.For", () => { long s = 0; Parallel.For(0, 100000, i => Interlocked.Add(ref s, i)); return s == 4999950000L; });
        Run("ConcurrentDictionary", () => { var c = new ConcurrentDictionary<int, int>(); Parallel.For(0, 10000, i => c[i] = i); return c.Count == 10000; });
        Run("Monitor.Wait / Pulse", () =>
        {
            object o = new object(); bool go = false;
            var t = new Thread(() => { lock (o) { go = true; Monitor.Pulse(o); } });
            lock (o) { t.Start(); while (!go) Monitor.Wait(o, 5000); }
            t.Join();
            return go;
        });
        Run("GC : 100 Mo d'allocations", () =>
        {
            long sum = 0;
            for (int i = 0; i < 1000; i++) { var b = new byte[100000]; b[i] = 1; sum += b[i]; }
            GC.Collect();
            return sum == 1000 && GC.CollectionCount(0) > 0;
        });
        Run("GC : objets gardes", () =>
        {
            var l = new List<string>();
            for (int i = 0; i < 100000; i++) l.Add("objet " + i);
            GC.Collect(); GC.Collect();
            return l[99999] == "objet 99999";
        });
        Run("GC : finaliseurs", () =>
        {
            new Thread(() => { for (int i = 0; i < 100; i++) _ = new Fin(); }).Start();
            Thread.Sleep(50);
            GC.Collect(); GC.WaitForPendingFinalizers();
            return Fin.Count > 0;
        });
        Run("reflexion", () => typeof(string).GetMethod("Trim", Type.EmptyTypes)!.Invoke(" a ", null) as string == "a");
        Run("generiques, interfaces", () => ((IEnumerable<int>)new[] { 1, 2 }).Sum() == 3);
        Run("Span, stackalloc", () => { Span<int> s = stackalloc int[4]; s.Fill(3); return s[3] == 3; });
        Run("SIMD (Vector<T>)", () => (new Vector<int>(2) + new Vector<int>(3))[0] == 5);
        Run("BigInteger", () => BigInteger.Pow(2, 200).ToString().Length == 61);
        Run("Regex", () => Regex.Match("armpi 2026", "([0-9]+)").Groups[1].Value == "2026");
        Run("JSON (JsonDocument)", () => JsonDocument.Parse("{\"a\":[1,2,3]}").RootElement.GetProperty("a").GetArrayLength() == 3);
        Run("Encoding UTF-8", () => Encoding.UTF8.GetByteCount("été") == 5);
        Run("DateTime, TimeZone", () => DateTime.UtcNow.Year >= 2026 && TimeZoneInfo.Local != null);
        Run("Stopwatch, Thread.Sleep", () => { var w = Stopwatch.StartNew(); Thread.Sleep(100); return w.ElapsedMilliseconds >= 90; });
        Run("Environment", () => Environment.ProcessorCount >= 1 && Environment.CurrentDirectory.Length > 0 && Environment.TickCount64 > 0);
        Run("fichiers : ecrire, relire, effacer", () =>
        {
            string p = "/tmp/dotnetcheck.txt";
            File.WriteAllText(p, "bonjour\n"); File.AppendAllText(p, "fin");
            bool ok = File.ReadAllLines(p).Length == 2 && new FileInfo(p).Length == 11;
            File.Delete(p);
            return ok && !File.Exists(p);
        });
        Run("dossiers", () =>
        {
            Directory.CreateDirectory("/tmp/dotnetdir/a");
            File.WriteAllText("/tmp/dotnetdir/a/x", "1");
            bool ok = Directory.GetFiles("/tmp/dotnetdir", "*", SearchOption.AllDirectories).Length == 1;
            Directory.Delete("/tmp/dotnetdir", true);
            return ok;
        });
        Run("FileStream asynchrone", () =>
        {
            string p = "/tmp/dotnetfs";
            using (var f = new FileStream(p, FileMode.Create, FileAccess.Write, FileShare.None, 4096, true)) f.WriteAsync(new byte[10000]).AsTask().Wait();
            bool ok = new FileInfo(p).Length == 10000;
            File.Delete(p);
            return ok;
        });
        // (le PC, vu de QEMU : le script d'essai y sert une petite page sur le port 8765)
        Run("sockets : TCP vers l'hote", () =>
        {
            using var c = new TcpClient();
            c.Connect(IPAddress.Parse("10.0.2.2"), 8765);
            var s = c.GetStream();
            s.Write(Encoding.ASCII.GetBytes("GET / HTTP/1.0\r\n\r\n"));
            var b = new byte[256];
            return s.Read(b, 0, b.Length) > 0 && Encoding.ASCII.GetString(b).StartsWith("HTTP/1.");
        });
        Run("sockets : asynchrones (epoll)", () =>
        {
            using var c = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
            c.ConnectAsync(IPAddress.Parse("10.0.2.2"), 8765).Wait();
            c.SendAsync(Encoding.ASCII.GetBytes("GET / HTTP/1.0\r\n\r\n")).Wait();
            var b = new byte[256];
            return c.ReceiveAsync(b).Result > 0;
        });
        Run("HttpClient", () =>
        {
            using var h = new System.Net.Http.HttpClient();
            return h.GetStringAsync("http://10.0.2.2:8765/").Result.Contains("ARMPI");
        });
        Run("sockets : boucle locale", () =>
        {
            const int port = 18765;			// (bind sur le port 0 n'en choisit pas : DOTNET.md)
            var l = new TcpListener(IPAddress.Loopback, port);
            l.Start();
            var t = Task.Run(() =>
            {
                using var a = l.AcceptTcpClient();
                var q = a.GetStream();
                var r = new byte[4];
                int n = q.Read(r, 0, 4);
                q.Write(r, 0, n);
            });
            using var c = new TcpClient();
            c.Connect(IPAddress.Loopback, port);
            var s = c.GetStream();
            s.Write(Encoding.ASCII.GetBytes("ping"));
            var b = new byte[4];
            int m = s.Read(b, 0, 4);
            t.Wait();
            l.Stop();
            return m == 4 && Encoding.ASCII.GetString(b) == "ping";
        });
        Run("cryptographie : SHA256", () => System.Security.Cryptography.SHA256.HashData(new byte[1]).Length == 32);
        Run("Process.Start", () => { using var p = Process.Start("/bin/true"); p!.WaitForExit(); return p.ExitCode == 0; });
        Console.WriteLine("dotnetcheck : " + s_pass + " reussis, " + s_fail + " echecs");
    }
}
