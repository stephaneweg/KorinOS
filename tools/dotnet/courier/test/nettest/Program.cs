// nettest : ce dont Courier a besoin dans le .NET de l'OS, essaye contre tools/dotnet/courier/test/server.py.
//     /dotnet/dotnet nettest.dll [hote]
using System;
using System.Diagnostics;
using System.Net;
using System.Net.Http;
using System.Net.Security;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

static class P
{
    static int ok, ko;
    static string H = "10.0.2.2";

    static async Task Run (string name, Func<Task<string>> f)
    {
        var sw = Stopwatch.StartNew ();
        try
        {
            string r = await f ();
            ok++;
            Console.WriteLine ($"ok   {name} [{sw.ElapsedMilliseconds} ms] {r}");
        }
        catch (Exception e)
        {
            ko++;
            Console.WriteLine ($"KO   {name} [{sw.ElapsedMilliseconds} ms] {e.GetType ().Name}: {e.Message}");
            for (var i = e.InnerException; i != null; i = i.InnerException) Console.WriteLine ($"     <- {i.GetType ().Name}: {i.Message}");
        }
    }

    static HttpClient Client (bool trustAll)
    {
        var h = new SocketsHttpHandler { AllowAutoRedirect = true, UseCookies = false, AutomaticDecompression = DecompressionMethods.All };
        if (trustAll) h.SslOptions = new SslClientAuthenticationOptions { RemoteCertificateValidationCallback = (s, c, ch, e) => true };
        return new HttpClient (h) { Timeout = TimeSpan.FromSeconds (60) };
    }

    static async Task Main (string[] a)
    {
        var total = Stopwatch.StartNew ();
        if (a.Length > 0) H = a[0];
        string http = $"http://{H}:18765", https = $"https://{H}:18766";
        using var c = Client (true);
        await Run ("http GET", async () => { var r = await c.GetAsync (http + "/json"); return $"{(int) r.StatusCode} {r.Version} {(await r.Content.ReadAsStringAsync ()).Length} car."; });
        await Run ("System.Text.Json", async () =>
        {
            using var d = JsonDocument.Parse (await c.GetStringAsync (http + "/json"));
            string token = d.RootElement.GetProperty ("token").GetString ();
            string back = JsonSerializer.Serialize (d.RootElement, new JsonSerializerOptions { WriteIndented = true });
            return $"{token}, {back.Length} car. indente";
        });
        await Run ("gzip", async () => (await c.GetStringAsync (http + "/gzip")).Substring (0, 20));
        await Run ("deflate", async () => (await c.GetStringAsync (http + "/deflate")).Substring (0, 20));
        await Run ("chunked", async () => await c.GetStringAsync (http + "/chunked"));
        await Run ("POST json", async () =>
        {
            var r = await c.PostAsync (http + "/echo", new StringContent ("{\"a\":1}", Encoding.UTF8, "application/json"));
            return (await r.Content.ReadAsStringAsync ()).Length + " car.";
        });
        await Run ("redirection", async () => { var r = await c.GetAsync (http + "/redirect"); return r.RequestMessage.RequestUri.ToString (); });
        await Run ("1 Mo", async () => (await c.GetByteArrayAsync (http + "/big?n=1000000")).Length.ToString ());
        await Run ("annulation http (attente de l'en-tete)", async () =>
        {
            using var cts = new CancellationTokenSource (1000);
            try { await c.GetAsync (http + "/hang", cts.Token); throw new Exception ("pas annule"); }
            catch (OperationCanceledException) { return "annule"; }
        });
        await Run ("annulation http (corps en cours)", async () =>
        {
            using var cts = new CancellationTokenSource ();
            var r = await c.GetAsync (http + "/slow?s=30", HttpCompletionOption.ResponseHeadersRead, cts.Token);
            cts.CancelAfter (500);
            try { await r.Content.ReadAsStringAsync (cts.Token); throw new Exception ("pas annule"); }
            catch (OperationCanceledException) { return "annule"; }
        });
        await Run ("https GET, certificat accepte par le rappel", async () =>
        {
            var r = await c.GetAsync (https + "/json");
            return $"{(int) r.StatusCode} {(await r.Content.ReadAsStringAsync ()).Length} car.";
        });
        await Run ("https second appel (poignee faite)", async () => (await c.GetStringAsync (https + "/gzip")).Substring (0, 20));
        await Run ("annulation https", async () =>
        {
            using var cts = new CancellationTokenSource (1000);
            try { await c.GetAsync (https + "/hang", cts.Token); throw new Exception ("pas annule"); }
            catch (OperationCanceledException) { return "annule"; }
        });
        await Run ("https 1 Mo", async () => (await c.GetByteArrayAsync (https + "/big?n=1000000")).Length.ToString ());
        await Run ("https refuse sans le rappel (certificat inconnu)", async () =>
        {
            using var s = Client (false);
            try { await s.GetAsync (https + "/json"); throw new Exception ("accepte a tort"); }
            catch (HttpRequestException e) { return "refuse : " + (e.InnerException?.Message ?? e.Message); }
        });
        await Run ("https de la vraie vie (verifie par /etc/ca-bundle)", async () =>
        {
            using var s = Client (false);
            var r = await s.GetAsync ("https://example.com/");
            return $"{(int) r.StatusCode} {r.Content.Headers.ContentLength}";
        });
        Console.WriteLine ($"fin : {ok} ok, {ko} KO, {total.ElapsedMilliseconds} ms dans main");
    }
}
