using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Web.WebView2.Core;
using Microsoft.Win32;

namespace ThinPlaceVR
{
    // Everything the HTML interface can ask the app to do, and the state it is sent.
    //
    // Interface -> app:  { id, cmd, args }      app -> interface: { type:"reply", id, ok, error }
    //                                            app -> interface: { type:"state", ... } (pushed)
    internal sealed class Bridge : IDisposable
    {
        private readonly Form _form;
        private readonly CoreWebView2 _web;
        private readonly JavaScriptSerializer _json = new JavaScriptSerializer { MaxJsonLength = int.MaxValue };
        private readonly Link _link = new Link();
        private readonly Timer _liveTimer = new Timer { Interval = 150 };
        private readonly Timer _slowTimer = new Timer { Interval = 2000 };
        private readonly FileSystemWatcher _watcher = new FileSystemWatcher();

        private UpdateInfo _latest;
        private object _replyData;          // extra data a command hands back to the interface
        private TraceResult _pendingRim;    // traced outline waiting for a name and width
        private string _updateState = "idle", _updateMessage = "";
        private int _updateProgress;

        public Bridge(Form form, CoreWebView2 web)
        {
            _form = form;
            _web = web;
            _web.WebMessageReceived += OnMessage;
            _liveTimer.Tick += (s, e) => PushLive();
            _slowTimer.Tick += (s, e) => PushState();
            _liveTimer.Start();
            _slowTimer.Start();
            WatchConfig();
            _web.NavigationCompleted += async (s, e) =>
            {
                PushState();
                await CheckForUpdates(quiet: true);
            };
        }

        // ---------------------------------------------------------------- state ----

        private void Post(object o)
        {
            try { _web.PostWebMessageAsJson(_json.Serialize(o)); } catch { }
        }

        private Link.Status _lastLive;

        private void PushLive()
        {
            var s = _link.Read();
            _lastLive = s;
            Post(new Dictionary<string, object>
            {
                ["type"] = "live",
                ["running"] = s.GameRunning,
                ["game"] = s.Game,
                ["runtime"] = s.Runtime,
                ["wheelBase"] = s.WheelBase,
                ["wheelFound"] = s.WheelFound,
                ["steering"] = Math.Round(s.SteeringDeg, 1),
                ["enabled"] = s.WindowsEnabled,
                ["selected"] = s.Selected,
                ["editMode"] = s.EditMode,
            });
        }

        public void PushState()
        {
            var cfg = ConfigStore.Load();
            var state = new Dictionary<string, object>
            {
                ["type"] = "state",
                ["version"] = Paths.AppVersion.ToString(),
                ["layerActive"] = LayerRegistered(),
                ["activeRuntime"] = ActiveRuntime(),
                ["config"] = cfg == null ? null : ConfigView(cfg),
                ["games"] = Games(),
                ["update"] = new Dictionary<string, object>
                {
                    ["state"] = _updateState,
                    ["latest"] = _latest?.Version.ToString(),
                    ["message"] = _updateMessage,
                    ["progress"] = _updateProgress,
                },
            };
            Post(state);
        }

        private static Dictionary<string, object> ConfigView(Dictionary<string, object> cfg)
        {
            var steering = ConfigStore.Obj(cfg, "steering");
            var key = ConfigStore.List(cfg, "key_colour_rgb");
            var wheels = ConfigStore.List(cfg, "wheels").OfType<Dictionary<string, object>>().Select(w => new Dictionary<string, object>
            {
                ["id"] = ConfigStore.Str(w, "id", ""),
                ["name"] = ConfigStore.Str(w, "name", "Wheel"),
                ["scale"] = Math.Round(ConfigStore.Num(w, "scale", 1.0) * 100),
                ["rotates"] = ConfigStore.Bool(w, "rotates", true),
                ["shape"] = ShapePath(ConfigStore.Str(w, "shape_file", "")),
                ["userAdded"] = ConfigStore.Bool(w, "user_added", false),
            }).ToList();
            var windows = ConfigStore.List(cfg, "windows").OfType<Dictionary<string, object>>().Select(w => new Dictionary<string, object>
            {
                ["name"] = ConfigStore.Str(w, "name", "Window"),
                ["width"] = ConfigStore.Num(w, "width_mm", 130),
                ["height"] = ConfigStore.Num(w, "height_mm", 95),
                ["enabled"] = ConfigStore.Bool(w, "enabled", true),
                ["onlyWhileAdjusting"] = ConfigStore.Bool(w, "only_while_adjusting", false),
            }).ToList();
            return new Dictionary<string, object>
            {
                ["enabled"] = ConfigStore.Bool(cfg, "enabled", true),
                ["keyColour"] = key.Count == 3 ? new[] { Convert.ToInt32(key[0]), Convert.ToInt32(key[1]), Convert.ToInt32(key[2]) } : new[] { 255, 0, 255 },
                ["margin"] = ConfigStore.Num(cfg, "wheel_margin_mm", 25),
                ["distanceCm"] = Math.Round(ConfigStore.Num(cfg, "place_distance_m", 0.6) * 100),
                ["nudgeMm"] = ConfigStore.Num(cfg, "nudge_mm", 5),
                ["nudgeDeg"] = ConfigStore.Num(cfg, "nudge_deg", 1),
                ["range"] = ConfigStore.Num(steering, "range_deg", 900),
                ["invert"] = ConfigStore.Bool(steering, "invert", false),
                ["activeWheel"] = ConfigStore.Str(cfg, "active_wheel", ""),
                ["wheelPlaced"] = ConfigStore.Bool(cfg, "wheel_placed", false),
                ["wheels"] = wheels,
                ["windows"] = windows,
            };
        }

        // The rim outline as an SVG path (mm, y down) for drawing in the interface.
        private static readonly Dictionary<string, string> ShapeCache = new Dictionary<string, string>();

        private static string ShapePath(string file)
        {
            if (string.IsNullOrEmpty(file)) return "";
            if (ShapeCache.TryGetValue(file, out string cached)) return cached;
            string path = "";
            foreach (var dir in new[] { Path.Combine(Paths.DataDir, "shapes"), Path.Combine(Paths.AppDir, "shapes") })
            {
                string f = Path.Combine(dir, file);
                if (!File.Exists(f)) continue;
                try
                {
                    var j = new JavaScriptSerializer().DeserializeObject(File.ReadAllText(f)) as Dictionary<string, object>;
                    var pts = (j["outline_mm"] as IEnumerable).Cast<object>().Select(p => (p as IEnumerable).Cast<object>().Select(Convert.ToDouble).ToArray()).ToList();
                    path = "M" + string.Join(" L", pts.Select(p => string.Format(System.Globalization.CultureInfo.InvariantCulture, "{0:0.#} {1:0.#}", p[0], -p[1]))) + " Z";
                }
                catch { }
                break;
            }
            ShapeCache[file] = path;
            return path;
        }

        private static bool LayerRegistered()
        {
            try
            {
                using (var key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)
                           .OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"))
                    return key?.GetValue(Paths.ManifestFile) is int i && i == 0;
            }
            catch { return false; }
        }

        private static string ActiveRuntime()
        {
            try
            {
                using (var key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)
                           .OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1"))
                {
                    string v = key?.GetValue("ActiveRuntime") as string ?? "";
                    if (v.IndexOf("Virtual Desktop", StringComparison.OrdinalIgnoreCase) >= 0) return "Virtual Desktop (VDXR)";
                    if (v.IndexOf("Steam", StringComparison.OrdinalIgnoreCase) >= 0) return "SteamVR";
                    if (v.IndexOf("oculus", StringComparison.OrdinalIgnoreCase) >= 0) return "Meta Quest Link";
                    if (v.IndexOf("pimax", StringComparison.OrdinalIgnoreCase) >= 0) return "Pimax";
                    return v.Length == 0 ? "None set" : Path.GetFileNameWithoutExtension(v);
                }
            }
            catch { return "Unknown"; }
        }

        private static List<Dictionary<string, object>> Games()
        {
            var list = new List<Dictionary<string, object>>();
            try
            {
                string f = Path.Combine(Paths.LogDir, "games.json");
                if (!File.Exists(f)) return list;
                var j = new JavaScriptSerializer().DeserializeObject(File.ReadAllText(f)) as Dictionary<string, object>;
                foreach (var kv in j)
                {
                    if (!(kv.Value is Dictionary<string, object> g)) continue;
                    string exe = kv.Key;
                    list.Add(new Dictionary<string, object>
                    {
                        ["exe"] = exe,
                        ["name"] = FriendlyGameName(exe, ConfigStore.Str(g, "name", exe)),
                        ["lastUsed"] = ConfigStore.Str(g, "last_used", ""),
                    });
                }
            }
            catch { }
            return list.OrderByDescending(g => (string)g["lastUsed"]).ToList();
        }

        private static string FriendlyGameName(string exe, string fallback)
        {
            switch (Path.GetFileNameWithoutExtension(exe).ToLowerInvariant())
            {
                case "assettocorsaevo": return "Assetto Corsa EVO";
                case "le mans ultimate": return "Le Mans Ultimate";
                case "elitedangerous64": return "Elite Dangerous";
                case "thinplacevrcheck": return "Thin Place VR check";
                default: return string.IsNullOrEmpty(fallback) ? exe : fallback;
            }
        }

        private void WatchConfig()
        {
            try
            {
                Directory.CreateDirectory(Paths.DataDir);
                _watcher.Path = Paths.DataDir;
                _watcher.Filter = "config.json";
                _watcher.NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size;
                FileSystemEventHandler h = (s, e) => _form.BeginInvoke((Action)PushState);
                _watcher.Changed += h;
                _watcher.Created += h;
                _watcher.EnableRaisingEvents = true;
            }
            catch { }
        }

        // ------------------------------------------------------------- commands ----

        private async void OnMessage(object sender, CoreWebView2WebMessageReceivedEventArgs e)
        {
            Dictionary<string, object> msg;
            try { msg = _json.DeserializeObject(e.WebMessageAsJson) as Dictionary<string, object>; }
            catch { return; }
            if (msg == null) return;
            object id = msg.TryGetValue("id", out object i) ? i : null;
            string cmd = ConfigStore.Str(msg, "cmd", "");
            var args = msg.TryGetValue("args", out object a) ? a as Dictionary<string, object> ?? new Dictionary<string, object>() : new Dictionary<string, object>();
            string error = null;
            try
            {
                error = await Handle(cmd, args);
            }
            catch (Exception ex)
            {
                error = ex.Message;
                Updater.Log("Command " + cmd + " failed: " + ex);
            }
            Post(new Dictionary<string, object> { ["type"] = "reply", ["id"] = id, ["ok"] = error == null, ["error"] = error, ["data"] = _replyData });
            _replyData = null;
            if (cmd != "action") PushState();
        }

        private bool Save(Action<Dictionary<string, object>> change)
        {
            if (!ConfigStore.Exists) throw new Exception("Settings appear after Thin Place VR has run in a game once.");
            if (!ConfigStore.Update(change)) throw new Exception("Couldn't save the settings. Try again in a moment.");
            return true;
        }

        private static double D(Dictionary<string, object> a, string k) => ConfigStore.Num(a, k, 0);

        private Dictionary<string, object> WindowAt(Dictionary<string, object> cfg, int index)
        {
            var list = ConfigStore.List(cfg, "windows");
            if (index < 0 || index >= list.Count) throw new Exception("That window no longer exists.");
            return (Dictionary<string, object>)list[index];
        }

        private async System.Threading.Tasks.Task<string> Handle(string cmd, Dictionary<string, object> a)
        {
            switch (cmd)
            {
                case "refresh":
                    return null;

                case "set":
                {
                    string key = ConfigStore.Str(a, "key", "");
                    object value = a.TryGetValue("value", out object v) ? v : null;
                    Save(cfg =>
                    {
                        var steering = ConfigStore.Obj(cfg, "steering");
                        switch (key)
                        {
                            case "enabled": cfg["enabled"] = Convert.ToBoolean(value); break;
                            case "margin": cfg["wheel_margin_mm"] = Clamp(value, 0, 150); break;
                            case "distanceCm": cfg["place_distance_m"] = Clamp(value, 20, 150) / 100.0; break;
                            case "nudgeMm": cfg["nudge_mm"] = Clamp(value, 1, 50); break;
                            case "nudgeDeg": cfg["nudge_deg"] = Clamp(value, 0.25, 10); break;
                            case "range": steering["range_deg"] = Clamp(value, 90, 3600); break;
                            case "invert": steering["invert"] = Convert.ToBoolean(value); break;
                            case "activeWheel": cfg["active_wheel"] = Convert.ToString(value); break;
                            case "wheelScale":
                            {
                                string active = ConfigStore.Str(cfg, "active_wheel", "");
                                foreach (var w in ConfigStore.List(cfg, "wheels").OfType<Dictionary<string, object>>())
                                    if (ConfigStore.Str(w, "id", "") == active) w["scale"] = Clamp(value, 50, 200) / 100.0;
                                break;
                            }
                            case "resetCentre": steering["centre_offset_deg"] = 0.0; break;
                            default: throw new Exception("Unknown setting " + key);
                        }
                    });
                    return null;
                }

                case "setWindow":
                {
                    int index = (int)D(a, "index");
                    Save(cfg =>
                    {
                        var w = WindowAt(cfg, index);
                        if (a.ContainsKey("name")) w["name"] = Convert.ToString(a["name"]).Trim().Length > 0 ? Convert.ToString(a["name"]).Trim() : "Window";
                        if (a.ContainsKey("width")) w["width_mm"] = Clamp(a["width"], 20, 800);
                        if (a.ContainsKey("height")) w["height_mm"] = Clamp(a["height"], 20, 800);
                        if (a.ContainsKey("enabled")) w["enabled"] = Convert.ToBoolean(a["enabled"]);
                        if (a.ContainsKey("onlyWhileAdjusting")) w["only_while_adjusting"] = Convert.ToBoolean(a["onlyWhileAdjusting"]);
                    });
                    return null;
                }

                case "addWindow":
                    Save(cfg =>
                    {
                        var list = ConfigStore.List(cfg, "windows");
                        string name = ConfigStore.Str(a, "name", "").Trim();
                        if (name.Length == 0) name = "Window " + (list.Count + 1);
                        list.Add(new Dictionary<string, object>
                        {
                            ["name"] = name,
                            ["width_mm"] = Clamp(a.TryGetValue("width", out object w) ? w : 200, 20, 800),
                            ["height_mm"] = Clamp(a.TryGetValue("height", out object h) ? h : 120, 20, 800),
                            ["enabled"] = true,
                            ["only_while_adjusting"] = ConfigStore.Bool(a, "onlyWhileAdjusting", false),
                            ["pose"] = new Dictionary<string, object>
                            {
                                ["position_m"] = new ArrayList { 0.0, 0.9, -0.5 },
                                ["yaw_deg"] = 0.0, ["pitch_deg"] = -30.0, ["roll_deg"] = 0.0,
                            },
                        });
                    });
                    return null;

                case "removeWindow":
                {
                    int index = (int)D(a, "index");
                    Save(cfg =>
                    {
                        var list = ConfigStore.List(cfg, "windows");
                        if (index >= 0 && index < list.Count) list.RemoveAt(index);
                    });
                    return null;
                }

                case "pickColour":
                {
                    var cfg = ConfigStore.Load() ?? throw new Exception("Settings appear after Thin Place VR has run in a game once.");
                    var k = ConfigStore.List(cfg, "key_colour_rgb");
                    var start = k.Count == 3 ? Color.FromArgb(Convert.ToInt32(k[0]), Convert.ToInt32(k[1]), Convert.ToInt32(k[2])) : Color.Magenta;
                    using (var dlg = new ColorDialog { Color = start, FullOpen = true })
                    {
                        if (dlg.ShowDialog(_form) != DialogResult.OK) return null;
                        var c = dlg.Color;
                        Save(x => x["key_colour_rgb"] = new ArrayList { (int)c.R, (int)c.G, (int)c.B });
                    }
                    return null;
                }

                case "action":
                {
                    // Placing and nudging happen in the game, over the live link.
                    if (!_link.Send((int)D(a, "action"), (int)ConfigStore.Num(a, "target", -1), ConfigStore.Bool(a, "fine", false)))
                        return "Start a game in VR first — windows are moved inside the game.";
                    return null;
                }

                case "traceRim":
                {
                    string file;
                    using (var dlg = new OpenFileDialog
                    {
                        Title = "Choose a front-on photo of the rim",
                        Filter = "Pictures|*.png;*.jpg;*.jpeg;*.bmp;*.gif|All files|*.*",
                    })
                    {
                        if (dlg.ShowDialog(_form) != DialogResult.OK) return null;
                        file = dlg.FileName;
                    }
                    _pendingRim = Tracer.Trace(file);
                    var r = _pendingRim;
                    _replyData = new Dictionary<string, object>
                    {
                        ["image"] = r.ThumbnailDataUrl,
                        ["width"] = r.Width,
                        ["height"] = r.Height,
                        ["outline"] = string.Join(" ", r.Outline.Select(p => ((int)p.X) + "," + ((int)p.Y))),
                        ["boundsW"] = r.Bounds.Width,
                        ["boundsH"] = r.Bounds.Height,
                        ["name"] = Path.GetFileNameWithoutExtension(file),
                    };
                    return null;
                }

                case "saveRim":
                {
                    if (_pendingRim == null) return "Choose a photo first.";
                    string name = ConfigStore.Str(a, "name", "").Trim();
                    if (name.Length == 0) return "Give the rim a name.";
                    double widthMm = Clamp(a.TryGetValue("widthMm", out object wv) ? wv : 300, 150, 450);
                    double hubUpMm = ConfigStore.Num(a, "hubUpMm", 0);
                    bool rotates = ConfigStore.Bool(a, "rotates", true);
                    SaveRim(name, widthMm, hubUpMm, rotates);
                    _pendingRim = null;
                    return null;
                }

                case "removeRim":
                {
                    string rid = ConfigStore.Str(a, "id", "");
                    Save(cfg =>
                    {
                        var wheels = ConfigStore.List(cfg, "wheels");
                        var w = wheels.OfType<Dictionary<string, object>>().FirstOrDefault(x => ConfigStore.Str(x, "id", "") == rid);
                        if (w == null) return;
                        if (!ConfigStore.Bool(w, "user_added", false)) throw new Exception("Built-in rims can't be removed.");
                        wheels.Remove(w);
                        if (ConfigStore.Str(cfg, "active_wheel", "") == rid)
                            cfg["active_wheel"] = wheels.Count > 0 ? ConfigStore.Str((Dictionary<string, object>)wheels[0], "id", "") : "";
                    });
                    return null;
                }

                case "openLog": return Open(Paths.LogFile, "There's no log yet. It appears after the first drive.");
                case "openFolder":
                    Directory.CreateDirectory(Paths.DataDir);
                    Process.Start("explorer.exe", "\"" + Paths.DataDir + "\"");
                    return null;
                case "runCheck": return Open(Paths.CheckTool, "The check tool isn't installed.");
                case "openUrl":
                {
                    string url = ConfigStore.Str(a, "url", "");
                    if (url.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
                        Process.Start(new ProcessStartInfo { FileName = url, UseShellExecute = true });
                    return null;
                }

                case "checkUpdates":
                    await CheckForUpdates(quiet: false);
                    return _updateState == "error" ? _updateMessage : null;

                case "installUpdate":
                    return await InstallUpdate();

                default:
                    return "Unknown command " + cmd;
            }
        }

        // Turn the traced outline into a shape file (millimetres, centred on the hub) and add it as a rim.
        private void SaveRim(string name, double widthMm, double hubUpMm, bool rotates)
        {
            var r = _pendingRim;
            double s = widthMm / r.Bounds.Width;
            double cx = r.Bounds.Left + r.Bounds.Width / 2, cy = r.Bounds.Top + r.Bounds.Height / 2;
            var outline = new ArrayList();
            foreach (var p in r.Outline)
                outline.Add(new ArrayList { Math.Round((p.X - cx) * s, 1), Math.Round((cy - p.Y) * s - hubUpMm, 1) });

            string slug = new string(name.ToLowerInvariant().Select(ch => char.IsLetterOrDigit(ch) ? ch : '-').ToArray()).Trim('-');
            if (slug.Length == 0) slug = "rim";
            string id = "user-" + slug;
            var cfgNow = ConfigStore.Load();
            var taken = new HashSet<string>(ConfigStore.List(cfgNow, "wheels").OfType<Dictionary<string, object>>().Select(w => ConfigStore.Str(w, "id", "")));
            for (int n = 2; taken.Contains(id); n++) id = "user-" + slug + "-" + n;

            string shapeFile = id + ".json";
            string dir = Path.Combine(Paths.DataDir, "shapes");
            Directory.CreateDirectory(dir);
            var shape = new Dictionary<string, object>
            {
                ["name"] = name,
                ["source"] = "traced from a photo in the Thin Place VR app",
                ["width_mm"] = widthMm,
                ["height_mm"] = Math.Round(r.Bounds.Height * s, 1),
                ["origin"] = "wheel hub (rotation centre)",
                ["outline_mm"] = outline,
            };
            File.WriteAllText(Path.Combine(dir, shapeFile), _json.Serialize(shape));
            ShapeCache.Remove(shapeFile);

            Save(cfg =>
            {
                var wheels = ConfigStore.List(cfg, "wheels");
                // Start the new rim where the current one is, so it's roughly in place already.
                object pose = null;
                string active = ConfigStore.Str(cfg, "active_wheel", "");
                var current = wheels.OfType<Dictionary<string, object>>().FirstOrDefault(w => ConfigStore.Str(w, "id", "") == active);
                if (current != null && current.TryGetValue("pose", out object cp)) pose = _json.DeserializeObject(_json.Serialize(cp));
                var entry = new Dictionary<string, object>
                {
                    ["id"] = id,
                    ["name"] = name,
                    ["shape_file"] = shapeFile,
                    ["scale"] = 1.0,
                    ["rotates"] = rotates,
                    ["hub_offset_mm"] = new ArrayList { 0.0, 0.0 },
                    ["user_added"] = true,
                };
                if (pose != null) entry["pose"] = pose;
                wheels.Add(entry);
                cfg["active_wheel"] = id;
            });
            Updater.Log("Added rim " + name + " (" + r.Outline.Count + " points, " + widthMm + " mm wide)");
        }

        private static double Clamp(object v, double min, double max)
        {
            double d = Convert.ToDouble(v, System.Globalization.CultureInfo.InvariantCulture);
            return Math.Max(min, Math.Min(max, d));
        }

        private static string Open(string path, string missing)
        {
            if (!File.Exists(path)) return missing;
            Process.Start(new ProcessStartInfo { FileName = path, UseShellExecute = true });
            return null;
        }

        // -------------------------------------------------------------- updates ----

        private async System.Threading.Tasks.Task CheckForUpdates(bool quiet)
        {
            _updateState = "checking";
            PushState();
            try
            {
                _latest = await Updater.GetLatestAsync();
                _updateState = Updater.IsNewer(_latest) ? "available" : "uptodate";
                _updateMessage = "";
            }
            catch (Exception ex)
            {
                _updateState = quiet ? "idle" : "error";
                _updateMessage = ex.Message;
            }
            PushState();
        }

        private async System.Threading.Tasks.Task<string> InstallUpdate()
        {
            if (_latest == null || !Updater.IsNewer(_latest)) return "There's no update to install.";
            _updateState = "downloading";
            _updateProgress = 0;
            PushState();
            try
            {
                await Updater.DownloadAndRunAsync(_latest, new Progress<int>(p => { _updateProgress = p; PushState(); }));
                _updateState = "installing";
                PushState();
                // The installer closes and reopens the app; give it a moment, then step aside.
                var t = new Timer { Interval = 1500 };
                t.Tick += (s, e) => { t.Stop(); _form.Close(); };
                t.Start();
                return null;
            }
            catch (Exception ex)
            {
                _updateState = "error";
                _updateMessage = "The update didn't install: " + ex.Message;
                PushState();
                return _updateMessage;
            }
        }

        public void Dispose()
        {
            _liveTimer.Stop();
            _slowTimer.Stop();
            _watcher.Dispose();
            _link.Dispose();
        }
    }
}
