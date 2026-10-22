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
            Controls.Add(_web);
            Load += async (s, e) => await StartAsync();
            FormClosed += (s, e) => _bridge?.Dispose();
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            // Dark title bar to match the app (Windows 10 2004+ / Windows 11).
            int on = 1;
            DwmSetWindowAttribute(Handle, 20, ref on, sizeof(int));
        }

        [DllImport("dwmapi.dll")]
        private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

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
