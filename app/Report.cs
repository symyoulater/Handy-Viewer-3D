using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;

namespace ThinPlaceVR
{
    // "Send a report": gathers the logs and settings into one zip on the desktop, ready to send.
    internal static class Report
    {
        public static string Make(string runtime, bool layerActive)
        {
            string desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
            string file = Path.Combine(desktop, "Thin Place VR report " + DateTime.Now.ToString("yyyy-MM-dd HHmm") + ".zip");
            if (File.Exists(file)) File.Delete(file);

            using (var zip = ZipFile.Open(file, ZipArchiveMode.Create))
            {
                Add(zip, Path.Combine(Paths.LogDir, "layer.log"));
                Add(zip, Path.Combine(Paths.LogDir, "app.log"));
                Add(zip, Path.Combine(Paths.LogDir, "games.json"));
                Add(zip, Path.Combine(Paths.LogDir, "check.txt"));
                Add(zip, Paths.ConfigFile);

                var info = new StringBuilder();
                info.AppendLine("Thin Place VR report");
                info.AppendLine("Made: " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"));
                info.AppendLine("App version: " + Paths.AppVersion);
                info.AppendLine("Windows: " + Environment.OSVersion.VersionString + (Environment.Is64BitOperatingSystem ? " (64-bit)" : ""));
                info.AppendLine("VR layer registered: " + (layerActive ? "yes" : "NO"));
                info.AppendLine("Active OpenXR runtime: " + runtime);
                info.AppendLine();
                info.AppendLine("Game controllers with a steering axis:");
                try
                {
                    var all = Hid.OpenAll();
                    foreach (var d in all)
                    {
                        info.AppendLine("  " + d.Name + (Hid.IsKnownBase(d.Name) ? "  [known wheel base]" : "") +
                                        "  axis " + d.LogicalMin + ".." + d.LogicalMax + ", " + d.BitSize + " bits");
                        d.Dispose();
                    }
                    if (all.Count == 0) info.AppendLine("  (none)");
                }
                catch (Exception ex) { info.AppendLine("  (couldn't list: " + ex.Message + ")"); }

                var entry = zip.CreateEntry("about this PC.txt");
                using (var w = new StreamWriter(entry.Open(), new UTF8Encoding(false))) w.Write(info.ToString());
            }
            Updater.Log("Report saved to " + file);
            return file;
        }

        private static void Add(ZipArchive zip, string path)
        {
            if (!File.Exists(path)) return;
            // Read shared, as the game may be writing the log at the same moment.
            using (var src = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            using (var dst = zip.CreateEntry(Path.GetFileName(path)).Open())
                src.CopyTo(dst);
        }
    }
}
