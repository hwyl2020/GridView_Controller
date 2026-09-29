# LiveMonitor.ps1
#
# Serves a live view of the running GridControllerLive window (captured with
# PrintWindow, so it works even when the window is behind other windows) plus
# process stats, at http://localhost:<Port>/ . Open that URL in any browser,
# or in the Claude desktop app's browser pane.
#
#   powershell -ExecutionPolicy Bypass -File tools\LiveMonitor.ps1 [-Port 8790]

param(
    [int]$Port = 8790,
    [string]$ExePath = (Join-Path $PSScriptRoot "..\bin\x64\Debug\GridControllerLive.exe")
)

$ErrorActionPreference = 'Stop'
$ExePath = [System.IO.Path]::GetFullPath($ExePath)

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Net;
using System.Runtime.InteropServices;
using System.Text;

public static class LiveMon
{
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] struct RECT { public int L, T, R, B; }

    [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);

    const string MainClass = "AccuVisionPrototypeMainWindow";
    const uint PW_RENDERFULLCONTENT = 2;

    static string exePath;
    static TimeSpan lastCpu = TimeSpan.Zero;
    static DateTime lastSample = DateTime.MinValue;
    static int lastPid = -1;

    static Process FindApp()
    {
        string name = Path.GetFileNameWithoutExtension(exePath);
        return Process.GetProcessesByName(name).FirstOrDefault();
    }

    // Visible top-level windows of the app: its main window plus any
    // message boxes (e.g. "Connection Failed").
    static List<KeyValuePair<IntPtr, string>> AppWindows(int pid, out IntPtr main)
    {
        var list = new List<KeyValuePair<IntPtr, string>>();
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) =>
        {
            int p; GetWindowThreadProcessId(h, out p);
            if (p != pid || !IsWindowVisible(h)) return true;
            var cls = new StringBuilder(256); GetClassName(h, cls, 256);
            var title = new StringBuilder(512); GetWindowText(h, title, 512);
            if (cls.ToString() == MainClass) found = h;
            else if (cls.ToString() != "ConsoleWindowClass") list.Add(new KeyValuePair<IntPtr, string>(h, title.ToString()));
            return true;
        }, IntPtr.Zero);
        main = found;
        return list;
    }

    static byte[] Capture(IntPtr h)
    {
        if (h == IntPtr.Zero || IsIconic(h)) return null;
        RECT r; GetWindowRect(h, out r);
        int w = r.R - r.L, ht = r.B - r.T;
        if (w <= 0 || ht <= 0) return null;
        using (var bmp = new Bitmap(w, ht, PixelFormat.Format24bppRgb))
        {
            using (var g = Graphics.FromImage(bmp))
            {
                IntPtr hdc = g.GetHdc();
                try { PrintWindow(h, hdc, PW_RENDERFULLCONTENT); }
                finally { g.ReleaseHdc(hdc); }
            }
            var enc = ImageCodecInfo.GetImageEncoders().First(c => c.MimeType == "image/jpeg");
            var ps = new EncoderParameters(1);
            ps.Param[0] = new EncoderParameter(System.Drawing.Imaging.Encoder.Quality, 85L);
            using (var ms = new MemoryStream()) { bmp.Save(ms, enc, ps); return ms.ToArray(); }
        }
    }

    static string Esc(string s) { return s.Replace("\\", "\\\\").Replace("\"", "\\\""); }

    static string StatusJson()
    {
        var p = FindApp();
        if (p == null) { lastPid = -1; return "{\"running\":false}"; }
        p.Refresh();
        IntPtr main;
        var others = AppWindows(p.Id, out main);

        double cpu = 0;
        var now = DateTime.UtcNow;
        if (lastPid == p.Id && lastSample != DateTime.MinValue)
        {
            double dt = (now - lastSample).TotalMilliseconds;
            if (dt > 0) cpu = (p.TotalProcessorTime - lastCpu).TotalMilliseconds / dt / Environment.ProcessorCount * 100.0;
        }
        lastPid = p.Id; lastCpu = p.TotalProcessorTime; lastSample = now;

        var sb = new StringBuilder();
        sb.Append("{\"running\":true");
        sb.AppendFormat(",\"pid\":{0}", p.Id);
        sb.AppendFormat(System.Globalization.CultureInfo.InvariantCulture, ",\"cpu\":{0:F1}", cpu);
        sb.AppendFormat(",\"memMB\":{0}", p.WorkingSet64 / (1024 * 1024));
        sb.AppendFormat(",\"threads\":{0}", p.Threads.Count);
        sb.AppendFormat(",\"uptime\":\"{0:hh\\:mm\\:ss}\"", now - p.StartTime.ToUniversalTime());
        sb.AppendFormat(",\"mainWindow\":{0}", main != IntPtr.Zero ? "true" : "false");
        sb.AppendFormat(",\"minimized\":{0}", main != IntPtr.Zero && IsIconic(main) ? "true" : "false");
        sb.Append(",\"dialogs\":[" + string.Join(",", others.Select(o => "\"" + Esc(o.Value) + "\"")) + "]");
        sb.Append("}");
        return sb.ToString();
    }

    static void Send(HttpListenerResponse res, int code, string type, byte[] body)
    {
        res.StatusCode = code;
        res.ContentType = type;
        res.Headers["Cache-Control"] = "no-store";
        res.ContentLength64 = body.Length;
        res.OutputStream.Write(body, 0, body.Length);
        res.OutputStream.Close();
    }

    public static void Run(int port, string exe, string html)
    {
        SetProcessDPIAware();
        exePath = exe;
        var listener = new HttpListener();
        listener.Prefixes.Add("http://localhost:" + port + "/");
        listener.Start();
        Console.WriteLine("LiveMonitor on http://localhost:" + port + "/  (watching " + exe + ")");

        while (true)
        {
            var ctx = listener.GetContext();
            try
            {
                string path = ctx.Request.Url.AbsolutePath;
                if (path == "/")
                    Send(ctx.Response, 200, "text/html; charset=utf-8", Encoding.UTF8.GetBytes(html));
                else if (path == "/status")
                    Send(ctx.Response, 200, "application/json", Encoding.UTF8.GetBytes(StatusJson()));
                else if (path == "/frame.jpg" || path.StartsWith("/dialog"))
                {
                    var p = FindApp();
                    byte[] img = null;
                    if (p != null)
                    {
                        IntPtr main;
                        var others = AppWindows(p.Id, out main);
                        IntPtr target = main;
                        if (path.StartsWith("/dialog")) target = others.Count > 0 ? others[0].Key : IntPtr.Zero;
                        img = Capture(target);
                    }
                    if (img != null) Send(ctx.Response, 200, "image/jpeg", img);
                    else Send(ctx.Response, 204, "text/plain", new byte[0]);
                }
                else if (path == "/launch" && ctx.Request.HttpMethod == "POST")
                {
                    if (FindApp() == null && File.Exists(exePath))
                        Process.Start(new ProcessStartInfo(exePath) { WorkingDirectory = Path.GetDirectoryName(exePath), UseShellExecute = true });
                    Send(ctx.Response, 200, "application/json", Encoding.UTF8.GetBytes("{\"ok\":true}"));
                }
                else
                    Send(ctx.Response, 404, "text/plain", Encoding.UTF8.GetBytes("not found"));
            }
            catch (Exception e)
            {
                Console.WriteLine("request error: " + e.Message);
                try { ctx.Response.Abort(); } catch { }
            }
        }
    }
}
"@

$html = @'
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GridControllerLive Monitor</title>
<style>
  :root { --bg:#f4f5f7; --panel:#ffffff; --text:#1d2330; --muted:#667085; --line:#d9dde3; --ok:#12805c; --warn:#b54708; --bad:#c0362c; }
  @media (prefers-color-scheme: dark) { :root { --bg:#14161a; --panel:#1d2026; --text:#e6e8eb; --muted:#9aa3ae; --line:#2e333b; --ok:#3ccf91; --warn:#f5a524; --bad:#f06a5f; } }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--text); font:14px/1.4 system-ui, "Segoe UI", sans-serif; }
  header { display:flex; flex-wrap:wrap; gap:8px 16px; align-items:center; padding:10px 16px; border-bottom:1px solid var(--line); background:var(--panel); }
  h1 { font-size:15px; margin:0; font-weight:600; }
  .pill { display:inline-flex; align-items:center; gap:6px; padding:2px 10px; border-radius:999px; border:1px solid var(--line); font-size:12px; }
  .dot { width:8px; height:8px; border-radius:50%; background:var(--muted); }
  .ok .dot { background:var(--ok); } .warn .dot { background:var(--warn); } .bad .dot { background:var(--bad); }
  .stats { display:flex; flex-wrap:wrap; gap:14px; color:var(--muted); font-size:12px; font-variant-numeric:tabular-nums; }
  .stats b { color:var(--text); font-weight:600; }
  main { padding:12px 16px; }
  .view { background:#000; border:1px solid var(--line); border-radius:6px; overflow:hidden; min-height:200px; display:flex; align-items:center; justify-content:center; }
  .view img { display:block; max-width:100%; height:auto; }
  .empty { color:#9aa3ae; padding:40px 16px; text-align:center; }
  .alert { margin:0 0 10px; padding:8px 12px; border-radius:6px; border:1px solid var(--warn); color:var(--warn); background:var(--panel); }
  button { font:inherit; padding:4px 12px; border-radius:6px; border:1px solid var(--line); background:var(--panel); color:var(--text); cursor:pointer; }
  .spacer { flex:1; }
  .fps { color:var(--muted); font-size:12px; }
</style>
</head>
<body>
<header>
  <h1>GridControllerLive</h1>
  <span id="state" class="pill"><span class="dot"></span><span id="stateText">Connecting&hellip;</span></span>
  <div class="stats" id="stats"></div>
  <span class="spacer"></span>
  <span class="fps" id="fps"></span>
  <button id="pause">Pause</button>
  <button id="launch" hidden>Launch app</button>
</header>
<main>
  <div id="alert" class="alert" hidden></div>
  <div class="view" id="view"><div class="empty" id="empty">Waiting for the app window&hellip;</div><img id="img" alt="Live capture of the GridControllerLive window" hidden></div>
</main>
<script>
  const $ = id => document.getElementById(id);
  let paused = false, frames = 0, t0 = performance.now(), dialogOpen = false;

  $('pause').onclick = () => { paused = !paused; $('pause').textContent = paused ? 'Resume' : 'Pause'; if (!paused) nextFrame(); };
  $('launch').onclick = () => fetch('/launch', { method: 'POST' });

  function setState(cls, text) { $('state').className = 'pill ' + cls; $('stateText').textContent = text; }

  async function poll() {
    try {
      const s = await (await fetch('/status', { cache: 'no-store' })).json();
      $('launch').hidden = s.running;
      if (!s.running) { setState('bad', 'App not running'); $('stats').innerHTML = ''; $('alert').hidden = true; dialogOpen = false; }
      else {
        dialogOpen = s.dialogs.length > 0;
        const streaming = s.cpu > 5;
        if (dialogOpen) setState('warn', 'Dialog open');
        else if (s.minimized) setState('warn', 'Minimized (cannot capture)');
        else if (!s.mainWindow) setState('warn', 'Starting...');
        else setState(streaming ? 'ok' : 'warn', streaming ? 'Running - likely streaming' : 'Running - idle');
        $('stats').innerHTML = `<span>PID <b>${s.pid}</b></span><span>CPU <b>${s.cpu.toFixed(1)}%</b></span><span>Mem <b>${s.memMB} MB</b></span><span>Threads <b>${s.threads}</b></span><span>Up <b>${s.uptime}</b></span>`;
        $('alert').hidden = !dialogOpen;
        if (dialogOpen) $('alert').textContent = 'App is showing: ' + s.dialogs.map(d => d || '(untitled)').join(', ');
      }
    } catch { setState('bad', 'Monitor offline'); }
    setTimeout(poll, 1000);
  }

  function nextFrame() {
    if (paused) return;
    const img = new Image();
    img.onload = () => {
      $('img').src = img.src; $('img').hidden = false; $('empty').hidden = true;
      frames++; const dt = (performance.now() - t0) / 1000;
      if (dt >= 2) { $('fps').textContent = (frames / dt).toFixed(1) + ' fps (capture)'; frames = 0; t0 = performance.now(); }
      setTimeout(nextFrame, 150);
    };
    img.onerror = () => { $('img').hidden = true; $('empty').hidden = false; setTimeout(nextFrame, 1000); };
    img.src = (dialogOpen ? '/dialog.jpg' : '/frame.jpg') + '?t=' + Date.now();
  }

  poll(); nextFrame();
</script>
</body>
</html>
'@

[LiveMon]::Run($Port, $ExePath, $html)
