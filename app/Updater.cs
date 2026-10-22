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
    }

    // Looks for new versions on the repo's GitHub Releases page and installs them.
    internal static class Updater
    {
        private const string LatestUrl = "https://api.github.com/repos/symyoulater/Thin-Place-VR/releases/latest";

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
            try
            {
                using (var wc = NewClient())
                    text = await wc.DownloadStringTaskAsync(LatestUrl);
            }
            catch (WebException ex) when ((ex.Response as HttpWebResponse)?.StatusCode == HttpStatusCode.NotFound)
            {
                throw new Exception("The update page couldn't be found. The GitHub repo may still be private.");
            }
            catch (WebException)
            {
                throw new Exception("Couldn't reach GitHub. Check the PC is online.");
            }

            var json = new JavaScriptSerializer().DeserializeObject(text) as Dictionary<string, object>;
            if (json == null) throw new Exception("GitHub sent back something unexpected.");

            string tag = ConfigStore.Str(json, "tag_name", "").TrimStart('v', 'V');
            if (!Version.TryParse(tag, out Version v)) throw new Exception("Couldn't read the latest version number.");

            var info = new UpdateInfo { Version = v };
            if (json.TryGetValue("assets", out object a) && a is IEnumerable assets)
            {
                foreach (var o in assets)
                {
                    if (!(o is Dictionary<string, object> asset)) continue;
                    string name = ConfigStore.Str(asset, "name", "");
                    if (name.StartsWith("ThinPlaceVR-Setup", StringComparison.OrdinalIgnoreCase) &&
                        name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                    {
                        info.FileName = name;
                        info.DownloadUrl = ConfigStore.Str(asset, "browser_download_url", null);
                        break;
                    }
                }
            }
            if (info.DownloadUrl == null) throw new Exception("The latest release has no installer attached yet.");
            return info;
        }

        public static bool IsNewer(UpdateInfo info) => info != null && info.Version > Paths.AppVersion;

        // Downloads the installer, starts it, and returns once it has been launched.
        public static async Task DownloadAndRunAsync(UpdateInfo info, IProgress<int> progress)
        {
            string target = Path.Combine(Path.GetTempPath(), info.FileName);
            using (var wc = NewClient())
            {
                wc.Headers[HttpRequestHeader.Accept] = "application/octet-stream";
                wc.DownloadProgressChanged += (s, e) => progress?.Report(e.ProgressPercentage);
                await wc.DownloadFileTaskAsync(new Uri(info.DownloadUrl), target);
            }

            // The installer closes this app, updates everything, then opens the app again.
            Process.Start(new ProcessStartInfo
            {
                FileName = target,
                Arguments = "/SILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS",
                UseShellExecute = true,
            });
        }
    }
}
