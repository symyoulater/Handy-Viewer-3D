using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace ThinPlaceVR
{
    internal sealed class UpdateInfo
    {
        public Version Version;
        public string DownloadUrl;
        public string FileName;
        public string Sha256; // from GitHub's record of the file, if it has one
        public long Size;
    }

    // Looks for new versions on the repo's GitHub Releases page and installs them.
    internal static class Updater
    {
        // Every step is written to C:\ProgramData\Thin Place VR\app.log so a failed update can be diagnosed.
        public static void Log(string msg)
        {
            try
            {
                Directory.CreateDirectory(Paths.LogDir);
                File.AppendAllText(Path.Combine(Paths.LogDir, "app.log"),
                    DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + msg + Environment.NewLine);
            }
            catch { }
        }

        private const string LatestUrl = "https://api.github.com/repos/symyoulater/Thin-Place-VR/releases?per_page=20";

        private const string ReleasePrefix = "https://github.com/symyoulater/Thin-Place-VR/releases/download/";

        private static WebClient NewClient()
        {
            var wc = new WebClient();
            wc.Headers[HttpRequestHeader.UserAgent] = "ThinPlaceVR/" + Paths.AppVersion;
            wc.Headers[HttpRequestHeader.Accept] = "application/vnd.github+json";
            return wc;
        }

        // Returns the newest release, or throws with a plain-English message.
        public static async Task<UpdateInfo> GetLatestAsync()
        {
            string text;
            Log("Checking for updates (this is " + Paths.AppVersion + ")");
            try
            {
                using (var wc = NewClient())
                    text = await wc.DownloadStringTaskAsync(LatestUrl);
            }
            catch (WebException ex) when ((ex.Response as HttpWebResponse)?.StatusCode == HttpStatusCode.NotFound)
            {
                Log("Update check: 404 " + ex.Message);
                throw new Exception("The update page couldn't be found. The GitHub repo may still be private.");
            }
            catch (WebException ex)
            {
                Log("Update check failed: " + ex);
                throw new Exception("Couldn't reach GitHub (" + ex.Message + ")");
            }

            // Look through recent releases and take the highest version that has an installer attached.
            // (GitHub's own "latest" goes by publish time, which can be wrong when two builds finish together.)
            var list = new JavaScriptSerializer().DeserializeObject(text) as IEnumerable;
            if (list == null) throw new Exception("GitHub sent back something unexpected.");

            UpdateInfo info = null;
            foreach (var r in list)
            {
                if (!(r is Dictionary<string, object> json)) continue;
                if (json.TryGetValue("draft", out object d) && d is bool isDraft && isDraft) continue;
                if (json.TryGetValue("prerelease", out object pr) && pr is bool isPre && isPre) continue;
                string tag = ConfigStore.Str(json, "tag_name", "").TrimStart('v', 'V');
                if (!Version.TryParse(tag, out Version v)) continue;
                if (info != null && v <= info.Version) continue;
                if (!json.TryGetValue("assets", out object a) || !(a is IEnumerable assets)) continue;
                foreach (var o in assets)
                {
                    if (!(o is Dictionary<string, object> asset)) continue;
                    string name = ConfigStore.Str(asset, "name", "");
                    if (name.StartsWith("ThinPlaceVR-Setup", StringComparison.OrdinalIgnoreCase) &&
                        name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                    {
                        string digest = ConfigStore.Str(asset, "digest", "");
                        info = new UpdateInfo
                        {
                            Version = v,
                            FileName = Path.GetFileName(name),
                            DownloadUrl = ConfigStore.Str(asset, "browser_download_url", null),
                            Sha256 = digest.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase) ? digest.Substring(7).ToLowerInvariant() : null,
                            Size = (long)ConfigStore.Num(asset, "size", 0),
                        };
                        break;
                    }
                }
            }
            if (info == null) throw new Exception("No release with an installer was found.");
            if (info.DownloadUrl == null) throw new Exception("The latest release has no installer attached yet.");
            if (!info.DownloadUrl.StartsWith(ReleasePrefix, StringComparison.OrdinalIgnoreCase))
            {
                Log("Refused an installer link outside the project: " + info.DownloadUrl);
                throw new Exception("The update link didn't point at Thin Place VR's own releases, so it wasn't used.");
            }
            Log("Latest is " + info.Version + " at " + info.DownloadUrl);
            return info;
        }

        public static bool IsNewer(UpdateInfo info) => info != null && info.Version > Paths.AppVersion;

        // Downloads the installer, starts it, and returns once it has been launched.
        public static async Task DownloadAndRunAsync(UpdateInfo info, IProgress<int> progress)
        {
            string target = Path.Combine(Path.GetTempPath(), info.FileName);
            Log("Downloading to " + target);
            try
            {
                using (var wc = NewClient())
                {
                    wc.Headers[HttpRequestHeader.Accept] = "application/octet-stream";
                    wc.DownloadProgressChanged += (s, e) => progress?.Report(e.ProgressPercentage);
                    await wc.DownloadFileTaskAsync(new Uri(info.DownloadUrl), target);
                }
            }
            catch (Exception ex)
            {
                Log("Download failed: " + ex);
                throw new Exception("the download didn't finish (" + ex.Message + ")");
            }
            // Make sure the file is exactly the one GitHub has on record before running it.
            long length = new FileInfo(target).Length;
            string hash;
            using (var sha = System.Security.Cryptography.SHA256.Create())
            using (var fs = File.OpenRead(target))
                hash = BitConverter.ToString(sha.ComputeHash(fs)).Replace("-", "").ToLowerInvariant();
            bool sizeOk = info.Size <= 0 || info.Size == length;
            bool hashOk = info.Sha256 == null || info.Sha256 == hash;
            if (!sizeOk || !hashOk)
            {
                Log("Installer check failed: " + length + " bytes, SHA-256 " + hash + "; expected " + info.Size + " bytes, " + (info.Sha256 ?? "no hash"));
                try { File.Delete(target); } catch { }
                throw new Exception("the downloaded installer didn't match the release, so it wasn't run. Try again in a minute.");
            }
            Log("Downloaded " + length + " bytes, SHA-256 " + hash + (info.Sha256 != null ? " (matches GitHub's record)" : " (GitHub gave no hash to compare)") + "; starting installer");

            // The installer closes this app, updates everything, then opens the app again.
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = target,
                    Arguments = "/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS",
                    UseShellExecute = true,
                });
            }
            catch (System.ComponentModel.Win32Exception ex) when (ex.NativeErrorCode == 1223)
            {
                Log("Installer cancelled at the Windows permission prompt");
                throw new Exception("Windows asked for permission and it was declined. Click Install again and choose Yes.");
            }
            catch (Exception ex)
            {
                Log("Installer failed to start: " + ex);
                throw new Exception("the installer couldn't start (" + ex.Message + ")");
            }
        }
    }
}
