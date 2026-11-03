// bigtest : gros corps en TLS par SslStream, en lecture synchrone puis asynchrone, position de la panne ;
// server.py : 18766 TLS 1.3, 18767 TLS 1.2 AES-GCM, 18768 TLS 1.2 ChaCha20.
//     /dotnet/dotnet bigtest.dll [hote]
using System;
using System.Diagnostics;
using System.Net.Security;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;

static class P
{
    static async Task One (string h, int port, int chunk, bool sync)
    {
        var sw = Stopwatch.StartNew ();
        long got = 0, bad = 0, calls = 0;
        try
        {
            using var t = new TcpClient ();
            t.Connect (h, port);
            using var s = new SslStream (t.GetStream (), false, (a, b, c, d) => true);
            s.AuthenticateAsClient (h);
            string info = $"{s.SslProtocol} {s.NegotiatedCipherSuite}";
            s.Write (Encoding.ASCII.GetBytes ($"GET /big?n=2000000 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"));
            byte[] b = new byte[chunk];
            Console.Write ($"{port} chunk {chunk} {(sync ? "sync" : "async")} {info} : ");
            for (;;)
            {
                int n = sync ? s.Read (b, 0, b.Length) : await s.ReadAsync (b, 0, b.Length);
                if (n <= 0) break;
                calls++;
                got += n;
            }
            Console.WriteLine ($"{got} octets, {calls} lectures, {sw.ElapsedMilliseconds} ms");
        }
        catch (Exception e)
        {
            Console.WriteLine ($"KO apres {got} octets, {calls} lectures");
            for (; e != null; e = e.InnerException) Console.WriteLine ($"     {e.GetType ().Name}: {e.Message}");
        }
    }

    static async Task Main (string[] a)
    {
        string h = a.Length > 0 ? a[0] : "10.0.2.2";
        foreach (int port in new[] { 18766, 18767, 18768 })
            foreach (int chunk in new[] { 65536, 1024 })
                foreach (bool sync in new[] { true, false })
                    await One (h, port, chunk, sync);
    }
}
