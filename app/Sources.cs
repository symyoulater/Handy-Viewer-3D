using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace ThinPlaceVR
{
    // Panels: windows in the headset that show something from the PC instead of the real world.
    // The app captures each one (a desktop window, a web page, a PDF, the camera or a game-data page)
    // and shares the picture with the game through shared memory; the layer shows it (src/layer.cpp).
    internal sealed class SourceHost : IDisposable
    {
        private readonly Form _owner;
        private readonly Dictionary<string, Capturer> _running = new Dictionary<string, Capturer>();

        // Called with (source id, width, height) the first time a desktop window's shape is known,
        // so the panel can be given the same proportions.
        public event Action<string, int, int> AspectKnown;

        public SourceHost(Form owner) { _owner = owner; }

        public sealed class Wanted
        {
            public string Id, Kind, Target, Name;
            public double WidthMm, HeightMm;
            public int Index;
        }

        // Start, change or stop captures to match the windows in the settings.
        public void Sync(IList<Wanted> wanted)
        {
            var keep = new HashSet<string>();
            foreach (var w in wanted)
            {
                if (string.IsNullOrEmpty(w.Id) || string.IsNullOrEmpty(w.Kind)) continue;
                keep.Add(w.Id);
                if (_running.TryGetValue(w.Id, out var c))
                {
                    c.Index = w.Index;
                    if (c.Kind == w.Kind && c.Target == w.Target && c.SameSize(w)) continue;
                    c.Dispose();
                    _running.Remove(w.Id);
                }
                Capturer nc = null;
                try
                {
                    nc = w.Kind == "window" ? new WindowCapturer(w, this) : (Capturer)new WebCapturer(w, _owner);
                    nc.Index = w.Index;
                    nc.Start();
                    _running[w.Id] = nc;
                    Updater.Log("Panel \"" + w.Name + "\": showing " + w.Kind + (string.IsNullOrEmpty(w.Target) ? "" : " " + w.Target));
                }
                catch (Exception ex)
                {
                    Updater.Log("Panel \"" + w.Name + "\" couldn't start: " + ex.Message);
                    nc?.Dispose();
                }
            }
            foreach (var id in _running.Keys.Where(k => !keep.Contains(k)).ToList())
            {
                _running[id].Dispose();
                _running.Remove(id);
            }
        }

        internal void RaiseAspect(string id, int w, int h) =>
            _owner.BeginInvoke((Action)(() => AspectKnown?.Invoke(id, w, h)));

        public void Scroll(int windowIndex, int direction)
        {
            var c = _running.Values.FirstOrDefault(x => x.Index == windowIndex);
            c?.Scroll(direction);
        }

        public void ScrollById(string id, int direction)
        {
            if (_running.TryGetValue(id ?? "", out var c)) c.Scroll(direction);
        }

        // A line of status per panel, for the app's Windows page.
        public Dictionary<string, object> Status() =>
            _running.ToDictionary(kv => kv.Key, kv => (object)kv.Value.State);

        public void Dispose()
        {
            foreach (var c in _running.Values) c.Dispose();
            _running.Clear();
        }

        // ---- desktop windows that can be shown --------------------------------------------------

        public static List<string> DesktopWindowTitles()
        {
            var titles = new List<string>();
            int self = Process.GetCurrentProcess().Id;
            EnumWindows((h, l) =>
            {
                if (!IsWindowVisible(h) || IsIconic(h)) return true;
                if ((GetWindowLong(h, -20) & 0x80) != 0) return true; // WS_EX_TOOLWINDOW
                GetWindowThreadProcessId(h, out int pid);
                if (pid == self) return true;
                string t = TitleOf(h);
                if (t.Length > 0 && t != "Program Manager" && !titles.Contains(t)) titles.Add(t);
                return true;
            }, IntPtr.Zero);
            return titles;
        }

        internal static IntPtr FindDesktopWindow(string wanted)
        {
            IntPtr exact = IntPtr.Zero, partial = IntPtr.Zero;
            int self = Process.GetCurrentProcess().Id;
            EnumWindows((h, l) =>
            {
                if (!IsWindowVisible(h)) return true;
                GetWindowThreadProcessId(h, out int pid);
                if (pid == self) return true;
                string t = TitleOf(h);
                if (t.Length == 0) return true;
                if (t.Equals(wanted, StringComparison.OrdinalIgnoreCase)) { exact = h; return false; }
                if (partial == IntPtr.Zero && t.IndexOf(wanted, StringComparison.OrdinalIgnoreCase) >= 0) partial = h;
                return true;
            }, IntPtr.Zero);
            return exact != IntPtr.Zero ? exact : partial;
        }

        internal static string TitleOf(IntPtr h)
        {
            int n = GetWindowTextLength(h);
            if (n <= 0) return "";
            var sb = new StringBuilder(n + 1);
            GetWindowText(h, sb, sb.Capacity);
            return sb.ToString();
        }

        private delegate bool EnumProc(IntPtr h, IntPtr l);
        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc cb, IntPtr l);
        [DllImport("user32.dll")] internal static extern bool IsWindowVisible(IntPtr h);
        [DllImport("user32.dll")] internal static extern bool IsIconic(IntPtr h);
        [DllImport("user32.dll")] internal static extern bool IsWindow(IntPtr h);
        [DllImport("user32.dll")] private static extern int GetWindowLong(IntPtr h, int index);
        [DllImport("user32.dll")] private static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr h, StringBuilder sb, int max);
        [DllImport("user32.dll")] private static extern int GetWindowTextLength(IntPtr h);
    }

    // Shares one panel's picture with the game: "Local\ThinPlaceVR.Src.<id>".
    // Layout (must match src/layer.cpp): uint32 magic 'TPVS', uint32 seq (odd while writing),
    // uint32 width, uint32 height, 16 spare bytes, then width*height BGRA pixels.
    internal sealed class FrameWriter : IDisposable
    {
        public const int MaxW = 1920, MaxH = 1200;
        private const int Header = 32;
        private const uint Magic = 0x53565054;
        private readonly MemoryMappedFile _file;
        private readonly MemoryMappedViewAccessor _view;
        private readonly IntPtr _base;
        private uint _seq;

        public FrameWriter(string id)
        {
            _file = MemoryMappedFile.CreateOrOpen("Local\\ThinPlaceVR.Src." + id, Header + (long)MaxW * MaxH * 4);
            _view = _file.CreateViewAccessor();
            _base = _view.SafeMemoryMappedViewHandle.DangerousGetHandle() + (int)_view.PointerOffset;
            _seq = _view.ReadUInt32(4) & ~1u;
            _view.Write(0, Magic);
        }

        // Copies a picture in, shrinking it first if it's bigger than the shared space allows.
        public void Write(Bitmap bmp)
        {
            Bitmap src = bmp;
            bool owned = false;
            if (bmp.Width > MaxW || bmp.Height > MaxH || bmp.PixelFormat != PixelFormat.Format32bppArgb && bmp.PixelFormat != PixelFormat.Format32bppRgb)
            {
                double f = Math.Min(1.0, Math.Min((double)MaxW / bmp.Width, (double)MaxH / bmp.Height));
                src = new Bitmap(Math.Max(1, (int)(bmp.Width * f)), Math.Max(1, (int)(bmp.Height * f)), PixelFormat.Format32bppArgb);
                using (var g = Graphics.FromImage(src))
                {
                    g.InterpolationMode = InterpolationMode.HighQualityBilinear;
                    g.DrawImage(bmp, 0, 0, src.Width, src.Height);
                }
                owned = true;
            }
            var data = src.LockBits(new Rectangle(0, 0, src.Width, src.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            try
            {
                _seq |= 1;
                _view.Write(4, _seq);                // writing
                _view.Write(8, (uint)src.Width);
                _view.Write(12, (uint)src.Height);
                int row = src.Width * 4;
                for (int y = 0; y < src.Height; y++)
                    CopyMemory(_base + Header + y * row, data.Scan0 + y * data.Stride, (UIntPtr)(uint)row);
                Thread.MemoryBarrier();
                _seq += 1;
                _view.Write(4, _seq);                // done
            }
            finally
            {
                src.UnlockBits(data);
                if (owned) src.Dispose();
            }
        }

        [DllImport("kernel32.dll", EntryPoint = "RtlMoveMemory")]
        private static extern void CopyMemory(IntPtr dest, IntPtr src, UIntPtr count);

        public void Dispose()
        {
            _view?.Dispose();
            _file?.Dispose();
        }
    }

    internal abstract class Capturer : IDisposable
    {
        public string Id, Kind, Target, Name;
        public double WidthMm, HeightMm;
        public int Index;
        public volatile string State = "Starting";
        protected FrameWriter Writer;

        protected Capturer(SourceHost.Wanted w)
        {
            Id = w.Id; Kind = w.Kind; Target = w.Target ?? ""; Name = w.Name;
            WidthMm = w.WidthMm; HeightMm = w.HeightMm;
            Writer = new FrameWriter(Id);
        }

        // Web pages are drawn at the panel's proportions, so a size change means starting again.
        public virtual bool SameSize(SourceHost.Wanted w) => true;

        public abstract void Start();
        public virtual void Scroll(int direction) { }
        public virtual void Dispose() => Writer?.Dispose();
    }

    // A window on the desktop (Discord, a browser, a track map...), copied ten times a second.
    internal sealed class WindowCapturer : Capturer
    {
        private readonly SourceHost _host;
        private Thread _thread;
        private volatile bool _run;
        private bool _aspectSent;

        public WindowCapturer(SourceHost.Wanted w, SourceHost host) : base(w) { _host = host; }

        public override void Start()
        {
            _run = true;
            _thread = new Thread(Loop) { IsBackground = true, Name = "Panel " + Name };
            _thread.Start();
        }

        private void Loop()
        {
            IntPtr hwnd = IntPtr.Zero;
            DateTime nextFind = DateTime.MinValue;
            while (_run)
            {
                try
                {
                    if (hwnd == IntPtr.Zero || !SourceHost.IsWindow(hwnd))
                    {
                        hwnd = IntPtr.Zero;
                        if (DateTime.UtcNow >= nextFind)
                        {
                            nextFind = DateTime.UtcNow.AddSeconds(2);
                            hwnd = SourceHost.FindDesktopWindow(Target);
                        }
                        if (hwnd == IntPtr.Zero) { State = "Window not found: open \"" + Target + "\""; Thread.Sleep(200); continue; }
                    }
                    if (SourceHost.IsIconic(hwnd)) { State = "Minimised - restore it to show it"; Thread.Sleep(300); continue; }
                    GetClientRect(hwnd, out RECT rc);
                    int w = rc.Right - rc.Left, h = rc.Bottom - rc.Top;
                    if (w < 16 || h < 16) { State = "Window too small"; Thread.Sleep(300); continue; }
                    using (var bmp = new Bitmap(w, h, System.Drawing.Imaging.PixelFormat.Format32bppArgb))
                    {
                        using (var g = Graphics.FromImage(bmp))
                        {
                            IntPtr dc = g.GetHdc();
                            try { PrintWindow(hwnd, dc, 3); } // client area, including GPU-drawn content
                            finally { g.ReleaseHdc(dc); }
                        }
                        Writer.Write(bmp);
                    }
                    if (!_aspectSent) { _aspectSent = true; _host.RaiseAspect(Id, w, h); }
                    State = "Showing " + w + " x " + h;
                }
                catch (Exception ex)
                {
                    State = "Problem: " + ex.Message;
                    Thread.Sleep(500);
                }
                Thread.Sleep(100);
            }
        }

        public override void Dispose()
        {
            _run = false;
            _thread?.Join(1000);
            base.Dispose();
        }

        [StructLayout(LayoutKind.Sequential)] private struct RECT { public int Left, Top, Right, Bottom; }
        [DllImport("user32.dll")] private static extern bool GetClientRect(IntPtr h, out RECT r);
        [DllImport("user32.dll")] private static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    }

    // A web page, PDF, the camera or a game-data page, drawn by an off-screen browser
    // at the panel's proportions and copied ten times a second.
    internal sealed class WebCapturer : Capturer
    {
        private static CoreWebView2Environment _env;
        private static System.Threading.Tasks.Task<CoreWebView2Environment> _envTask;
        private readonly Form _owner;
        private Form _form;
        private WebView2 _web;
        private System.Windows.Forms.Timer _timer;
        private bool _busy, _disposed;
        private int _pxW, _pxH;

        public WebCapturer(SourceHost.Wanted w, Form owner) : base(w) { _owner = owner; }

        public override bool SameSize(SourceHost.Wanted w) =>
            Math.Abs(w.WidthMm / Math.Max(1, w.HeightMm) - WidthMm / Math.Max(1, HeightMm)) < 0.01;

        private static System.Threading.Tasks.Task<CoreWebView2Environment> Env()
        {
            if (_envTask != null) return _envTask;
            // Panels keep their own browser data (so web logins are remembered), and keep drawing
            // even though their window is off-screen.
            string data = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Thin Place VR", "WebView2Panels");
            var opts = new CoreWebView2EnvironmentOptions
            {
                AdditionalBrowserArguments = "--disable-features=CalculateNativeWinOcclusion --disable-backgrounding-occluded-windows " +
                                             "--disable-renderer-backgrounding --disable-background-timer-throttling",
            };
            _envTask = CoreWebView2Environment.CreateAsync(null, data, opts);
            return _envTask;
        }

        public override async void Start()
        {
            try
            {
                double aspect = WidthMm / Math.Max(1.0, HeightMm);
                _pxW = aspect >= 1 ? 1280 : Math.Max(240, (int)Math.Round(1280 * aspect));
                _pxH = aspect >= 1 ? Math.Max(200, (int)Math.Round(1280 / aspect)) : 1280;
                _pxW = Math.Min(_pxW, FrameWriter.MaxW);
                _pxH = Math.Min(_pxH, FrameWriter.MaxH);

                _form = new Form
                {
                    FormBorderStyle = FormBorderStyle.None,
                    ShowInTaskbar = false,
                    StartPosition = FormStartPosition.Manual,
                    Location = new Point(-32000, -32000),
                    ClientSize = new Size(_pxW, _pxH),
                    Text = "Thin Place VR panel",
                };
                _web = new WebView2 { Dock = DockStyle.Fill, DefaultBackgroundColor = Color.White };
                _form.Controls.Add(_web);
                _form.Show(_owner);
                _form.Location = new Point(-32000, -32000);

                _env = await Env();
                if (_disposed) return;
                await _web.EnsureCoreWebView2Async(_env);
                if (_disposed) return;
                var core = _web.CoreWebView2;
                core.Settings.AreDefaultContextMenusEnabled = false;
                core.Settings.IsStatusBarEnabled = false;
                core.Settings.IsPasswordAutosaveEnabled = false;
                core.NewWindowRequested += (s, e) => { e.Handled = true; core.Navigate(e.Uri); }; // links stay in the panel
                core.SetVirtualHostNameToFolderMapping("app.thinplace", Path.Combine(Paths.AppDir, "ui"), CoreWebView2HostResourceAccessKind.Deny);
                core.PermissionRequested += (s, e) =>
                {
                    bool ours = e.Uri.StartsWith("https://app.thinplace/", StringComparison.OrdinalIgnoreCase);
                    e.State = ours && e.PermissionKind == CoreWebView2PermissionKind.Camera ? CoreWebView2PermissionState.Allow : CoreWebView2PermissionState.Deny;
                };
                core.Navigate(Address());
                _timer = new System.Windows.Forms.Timer { Interval = Kind == "camera" ? 66 : 100 };
                _timer.Tick += async (s, e) => await Grab();
                _timer.Start();
                State = "Loading";
            }
            catch (Exception ex)
            {
                State = "Couldn't start: " + ex.Message;
                Updater.Log("Panel \"" + Name + "\" browser failed: " + ex);
            }
        }

        private string Address()
        {
            switch (Kind)
            {
                case "pdf":
                    return File.Exists(Target) ? new Uri(Target).AbsoluteUri : "about:blank";
                case "camera":
                    return "https://app.thinplace/panels/camera.html";
                case "data":
                    return "https://app.thinplace/panels/" + Uri.EscapeDataString(Target) + ".html";
                default:
                    string t = Target.Trim();
                    if (t.Length == 0) return "about:blank";
                    return t.StartsWith("http://", StringComparison.OrdinalIgnoreCase) || t.StartsWith("https://", StringComparison.OrdinalIgnoreCase) ? t : "https://" + t;
            }
        }

        private async System.Threading.Tasks.Task Grab()
        {
            if (_busy || _disposed || _web?.CoreWebView2 == null) return;
            _busy = true;
            try
            {
                using (var ms = new MemoryStream())
                {
                    await _web.CoreWebView2.CapturePreviewAsync(CoreWebView2CapturePreviewImageFormat.Jpeg, ms);
                    if (_disposed) return;
                    ms.Position = 0;
                    using (var img = new Bitmap(ms)) Writer.Write(img);
                }
                State = "Showing " + _pxW + " x " + _pxH;
            }
            catch (Exception ex)
            {
                State = "Problem: " + ex.Message;
            }
            finally { _busy = false; }
        }

        // Scrolls as if the mouse wheel were turned over the middle of the panel.
        public override void Scroll(int direction)
        {
            var core = _web?.CoreWebView2;
            if (core == null) return;
            int dy = (direction == 1 ? -1 : 1) * (int)(_pxH * 0.8);
            string p = "{\"type\":\"mouseWheel\",\"x\":" + _pxW / 2 + ",\"y\":" + _pxH / 2 + ",\"deltaX\":0,\"deltaY\":" + dy + "}";
            _ = core.CallDevToolsProtocolMethodAsync("Input.dispatchMouseEvent", p);
        }

        // Game-data pages get their numbers from the app.
        public void PostData(string json)
        {
            try { _web?.CoreWebView2?.PostWebMessageAsJson(json); } catch { }
        }

        public override void Dispose()
        {
            _disposed = true;
            _timer?.Stop();
            _timer?.Dispose();
            try { _web?.Dispose(); } catch { }
            try { _form?.Close(); _form?.Dispose(); } catch { }
            base.Dispose();
        }
    }
}
