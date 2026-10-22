using System;
using System.Net;
using System.Threading;
using System.Windows.Forms;

namespace HandyViewer3D
{
    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            // Only one copy of the app at a time; a second launch just brings the first to the front.
            using (var mutex = new Mutex(true, "HandyViewer3D.App.SingleInstance", out bool first))
            {
                if (!first)
                {
                    NativeMethods.BringExistingToFront("Handy Viewer 3D");
                    return;
                }

                ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.Run(new MainForm());
            }
        }
    }

    internal static class NativeMethods
    {
        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        private static extern IntPtr FindWindow(string cls, string title);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr hWnd);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool ShowWindow(IntPtr hWnd, int cmd);

        public static void BringExistingToFront(string title)
        {
            IntPtr h = FindWindow(null, title);
            if (h == IntPtr.Zero) return;
            ShowWindow(h, 9); // SW_RESTORE
            SetForegroundWindow(h);
        }
    }
}
