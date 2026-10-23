using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace ThinPlaceVR
{
    // The app window: a native frame holding the HTML interface from the ui folder.
    internal sealed class MainForm : Form
    {
        private readonly WebView2 _web = new WebView2 { Dock = DockStyle.Fill, DefaultBackgroundColor = Color.FromArgb(13, 14, 17) };
        private Bridge _bridge;

        public MainForm()
        {
            Text = "Thin Place VR";
            BackColor = Color.FromArgb(13, 14, 17);
            StartPosition = FormStartPosition.CenterScreen;
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(1200, 760);
            MinimumSize = new Size(980, 640);
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }
            Padding = new Padding(ResizeBorder); // a thin edge outside the web view, so the window can be resized
            Controls.Add(_web);
            Load += async (s, e) => await StartAsync();
            Resize += (s, e) =>
            {
                Padding = WindowState == FormWindowState.Maximized ? new Padding(0) : new Padding(ResizeBorder);
                _bridge?.PostWindowState(WindowState == FormWindowState.Maximized);
            };
            FormClosed += (s, e) => _bridge?.Dispose();
        }

        // ---- our own window frame -------------------------------------------------------------
        // The standard title bar is removed (WM_NCCALCSIZE) while keeping a normal resizable window,
        // so Windows still gives it a shadow, rounded corners, snapping and minimise from the taskbar.
        // The interface draws its own title bar and asks us to start dragging or change state.

        private const int ResizeBorder = 6;
        private const int WM_NCCALCSIZE = 0x83, WM_NCHITTEST = 0x84, WM_NCLBUTTONDOWN = 0xA1, HTCAPTION = 2;

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            int on = 1;
            DwmSetWindowAttribute(Handle, 20, ref on, sizeof(int)); // dark frame colours
            int round = 2;
            DwmSetWindowAttribute(Handle, 33, ref round, sizeof(int)); // rounded corners (Windows 11)
            var m = new MARGINS { Top = 1 };
            DwmExtendFrameIntoClientArea(Handle, ref m); // keeps the shadow
            SetWindowPos(Handle, IntPtr.Zero, 0, 0, 0, 0, 0x0027); // SWP_FRAMECHANGED | NOMOVE | NOSIZE | NOZORDER
        }

        protected override void WndProc(ref Message msg)
        {
            if (msg.Msg == WM_NCCALCSIZE && msg.WParam != IntPtr.Zero)
            {
                if (WindowState == FormWindowState.Maximized)
                {
                    // Keep a maximised window inside the screen's working area (not under the taskbar).
                    var p = Marshal.PtrToStructure<NCCALCSIZE_PARAMS>(msg.LParam);
                    var wa = Screen.FromHandle(Handle).WorkingArea;
                    p.rgrc0 = new RECT { Left = wa.Left, Top = wa.Top, Right = wa.Right, Bottom = wa.Bottom };
                    Marshal.StructureToPtr(p, msg.LParam, false);
                }
                msg.Result = IntPtr.Zero;
                return;
            }
            if (msg.Msg == WM_NCHITTEST && WindowState != FormWindowState.Maximized)
            {
                var pt = PointToClient(new Point((short)(msg.LParam.ToInt64() & 0xFFFF), (short)((msg.LParam.ToInt64() >> 16) & 0xFFFF)));
                int b = ResizeBorder;
                bool l = pt.X < b, r = pt.X >= ClientSize.Width - b, t = pt.Y < b, bo = pt.Y >= ClientSize.Height - b;
                int hit = t && l ? 13 : t && r ? 14 : bo && l ? 16 : bo && r ? 17 : l ? 10 : r ? 11 : t ? 12 : bo ? 15 : 0;
                if (hit != 0) { msg.Result = (IntPtr)hit; return; }
            }
            base.WndProc(ref msg);
        }

        public void BeginDrag()
        {
            ReleaseCapture();
            SendMessage(Handle, WM_NCLBUTTONDOWN, (IntPtr)HTCAPTION, IntPtr.Zero);
        }

        public void ToggleMaximise() =>
            WindowState = WindowState == FormWindowState.Maximized ? FormWindowState.Normal : FormWindowState.Maximized;

        public void MinimiseWindow() => WindowState = FormWindowState.Minimized;

        [StructLayout(LayoutKind.Sequential)] private struct MARGINS { public int Left, Right, Top, Bottom; }
        [StructLayout(LayoutKind.Sequential)] private struct RECT { public int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential)] private struct NCCALCSIZE_PARAMS { public RECT rgrc0, rgrc1, rgrc2; public IntPtr lppos; }

        [DllImport("dwmapi.dll")] private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);
        [DllImport("dwmapi.dll")] private static extern int DwmExtendFrameIntoClientArea(IntPtr hwnd, ref MARGINS m);
        [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
        [DllImport("user32.dll")] private static extern bool ReleaseCapture();
        [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr h, int msg, IntPtr w, IntPtr l);

        private async System.Threading.Tasks.Task StartAsync()
        {
            try
            {
                // Program Files isn't writable, so WebView2 keeps its working files under Local AppData.
                string data = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Thin Place VR", "WebView2");
                var env = await CoreWebView2Environment.CreateAsync(null, data);
                await _web.EnsureCoreWebView2Async(env);
            }
            catch (WebView2RuntimeNotFoundException)
            {
                var r = MessageBox.Show(this,
                    "Thin Place VR needs Microsoft Edge WebView2, which isn't installed on this PC.\n\nOpen the download page now?",
                    Text, MessageBoxButtons.YesNo, MessageBoxIcon.Information);
                if (r == DialogResult.Yes)
                    Process.Start(new ProcessStartInfo { FileName = "https://developer.microsoft.com/microsoft-edge/webview2/", UseShellExecute = true });
                Close();
                return;
            }
            catch (Exception ex)
            {
                Updater.Log("WebView2 failed to start: " + ex);
                MessageBox.Show(this, "The app's window couldn't start:\n" + ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
                Close();
                return;
            }

            var core = _web.CoreWebView2;
            core.Settings.AreDefaultContextMenusEnabled = false;
            core.Settings.IsZoomControlEnabled = false;
            core.Settings.IsStatusBarEnabled = false;
#if !DEBUG
            core.Settings.AreDevToolsEnabled = false;
#endif
            core.SetVirtualHostNameToFolderMapping("app.thinplace", Path.Combine(Paths.AppDir, "ui"), CoreWebView2HostResourceAccessKind.Deny);
            // Links to the web open in the normal browser, not inside the app.
            core.NewWindowRequested += (s, e) =>
            {
                e.Handled = true;
                OpenExternal(e.Uri);
            };
            core.NavigationStarting += (s, e) =>
            {
                if (!e.Uri.StartsWith("https://app.thinplace/", StringComparison.OrdinalIgnoreCase))
                {
                    e.Cancel = true;
                    OpenExternal(e.Uri);
                }
            };

            _bridge = new Bridge(this, core);
            core.Navigate("https://app.thinplace/index.html");
        }

        private static void OpenExternal(string uri)
        {
            if (uri != null && uri.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
                Process.Start(new ProcessStartInfo { FileName = uri, UseShellExecute = true });
        }
    }
}
