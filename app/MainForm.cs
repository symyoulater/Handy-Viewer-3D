using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using Microsoft.Win32;

namespace HandyViewer3D
{
    internal sealed class MainForm : Form
    {
        // ---- look ------------------------------------------------------------------------
        private static readonly Color Bg = Color.FromArgb(22, 20, 26);
        private static readonly Color Panel = Color.FromArgb(34, 31, 40);
        private static readonly Color Edge = Color.FromArgb(58, 52, 68);
        private static readonly Color Text1 = Color.FromArgb(236, 232, 242);
        private static readonly Color Text2 = Color.FromArgb(160, 152, 172);
        private static readonly Color Accent = Color.FromArgb(214, 64, 214);
        private static readonly Color Good = Color.FromArgb(96, 200, 128);
        private static readonly Color Warn = Color.FromArgb(232, 176, 72);

        private readonly Font _body = new Font("Segoe UI", 10f);
        private readonly Font _small = new Font("Segoe UI", 9f);
        private readonly Font _head = new Font("Segoe UI Semibold", 11f);
        private readonly Font _title = new Font("Segoe UI Semibold", 16f);

        // ---- controls --------------------------------------------------------------------
        private Label _statusLayer, _statusLast, _statusWheel, _noConfig;
        private CheckBox _showWindows, _invert, _deckShow;
        private ComboBox _rim;
        private NumericUpDown _range, _deckW, _deckH, _nudgeMm, _nudgeDeg;
        private Panel _swatch;
        private Label _swatchText;
        private Button _updateBtn;
        private Label _updateText;
        private ProgressBar _updateProgress;
        private Control[] _settingsControls;

        private readonly FileSystemWatcher _watcher = new FileSystemWatcher();
        private readonly Timer _saveTimer = new Timer { Interval = 400 };
        private readonly Timer _reloadTimer = new Timer { Interval = 300 };
        private readonly Timer _statusTimer = new Timer { Interval = 3000 };
        private bool _loading;
        private DateTime _ignoreWatcherUntil;
        private UpdateInfo _pendingUpdate;

        public MainForm()
        {
            Text = "Handy Viewer 3D";
            BackColor = Bg;
            ForeColor = Text1;
            Font = _body;
            StartPosition = FormStartPosition.CenterScreen;
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(620, 760);
            MinimumSize = new Size(560, 560);
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { }

            BuildUi();

            _saveTimer.Tick += (s, e) => { _saveTimer.Stop(); Save(); };
            _reloadTimer.Tick += (s, e) => { _reloadTimer.Stop(); LoadSettings(); };
            _statusTimer.Tick += (s, e) => RefreshStatus();

            Load += async (s, e) =>
            {
                LoadSettings();
                RefreshStatus();
                WatchConfig();
                _statusTimer.Start();
                await CheckForUpdates(quiet: true);
            };
        }

        // =====================================================================================
        // Layout
        // =====================================================================================
        private void BuildUi()
        {
            var scroll = new Panel { Dock = DockStyle.Fill, AutoScroll = true, Padding = new Padding(18, 14, 18, 14) };
            Controls.Add(scroll);

            var stack = new FlowLayoutPanel
            {
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                Dock = DockStyle.Top,
            };
            scroll.Controls.Add(stack);
            scroll.Resize += (s, e) => { foreach (Control c in stack.Controls) c.Width = scroll.ClientSize.Width - 40; };

            // Header
            var header = new Panel { Height = 56, Margin = new Padding(0, 0, 0, 6) };
            header.Controls.Add(new Label { Text = "Handy Viewer 3D", Font = _title, ForeColor = Text1, AutoSize = true, Location = new Point(0, 2) });
            header.Controls.Add(new Label { Text = "Version " + Paths.AppVersion, Font = _small, ForeColor = Text2, AutoSize = true, Location = new Point(2, 34) });
            stack.Controls.Add(header);

            // Status
            var status = Card("Status");
            _statusLayer = Line(status, "");
            _statusLast = Line(status, "");
            _statusWheel = Line(status, "");
            stack.Controls.Add(status);

            _noConfig = new Label
            {
                Text = "Your settings appear here after the first drive in VR.",
                ForeColor = Warn, AutoSize = true, Margin = new Padding(2, 4, 0, 8), Visible = false,
            };
            stack.Controls.Add(_noConfig);

            // Windows
            var win = Card("Windows in VR");
            _showWindows = Check(win, "Show windows in VR (same as Ctrl+Alt+V)");
            stack.Controls.Add(win);

            // Wheel
            var wheel = Card("Wheel");
            _rim = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 240, FlatStyle = FlatStyle.Flat, BackColor = Bg, ForeColor = Text1 };
            Row(wheel, "Rim", _rim);
            _range = Num(90, 3600, 1, 0);
            Row(wheel, "Steering range (°) — match Simucube Tuner", _range);
            _invert = Check(wheel, "Window turns the opposite way to the wheel (flip)");
            var resetCentre = SmallButton("Reset steering centre");
            resetCentre.Click += (s, e) => ResetCentre();
            Row(wheel, "", resetCentre);
            stack.Controls.Add(wheel);

            // Stream Deck
            var deck = Card("Stream Deck window");
            _deckShow = Check(deck, "Show Stream Deck window");
            _deckW = Num(20, 600, 1, 0);
            Row(deck, "Width (mm)", _deckW);
            _deckH = Num(20, 600, 1, 0);
            Row(deck, "Height (mm)", _deckH);
            stack.Controls.Add(deck);

            // Colour
            var colour = Card("Passthrough colour");
            var colourRow = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0, 2, 0, 2) };
            _swatch = new Panel { Width = 34, Height = 26, BorderStyle = BorderStyle.FixedSingle, Margin = new Padding(0, 2, 10, 0) };
            _swatchText = new Label { AutoSize = true, ForeColor = Text1, Margin = new Padding(0, 6, 12, 0) };
            var pick = SmallButton("Change…");
            pick.Click += (s, e) => PickColour();
            colourRow.Controls.AddRange(new Control[] { _swatch, _swatchText, pick });
            colour.Controls.Add(colourRow);
            Hint(colour, "Must match the colour set under Configure in Virtual Desktop's passthrough settings.");
            stack.Controls.Add(colour);

            // Steps
            var steps = Card("Edit mode step sizes");
            _nudgeMm = Num(1, 50, 1, 0);
            Row(steps, "Move step (mm)", _nudgeMm);
            _nudgeDeg = Num(0.25m, 10, 0.25m, 2);
            Row(steps, "Turn step (°)", _nudgeDeg);
            Hint(steps, "Hold Shift in edit mode for 1 mm / 0.25° steps.");
            stack.Controls.Add(steps);

            // Keys reference
            var keys = Card("Keys in the headset");
            Hint(keys,
                "Ctrl+Alt+H  edit mode on/off      Ctrl+Alt+V  windows on/off\n" +
                "In edit mode:  P place where you look · Tab next window · W swap rim\n" +
                "X set centre · I flip direction · Arrows move · R/F away/closer\n" +
                "T/G tilt · Q/E turn · Z/C roll · =/− Stream Deck size · Esc finish");
            stack.Controls.Add(keys);

            // Tools + updates
            var tools = Card("Tools");
            var toolRow = new FlowLayoutPanel { AutoSize = true, WrapContents = true, Margin = new Padding(0, 2, 0, 2) };
            var openLog = SmallButton("Open log");
            openLog.Click += (s, e) => OpenFile(Paths.LogFile, "No log yet — it appears after the first drive.");
            var runCheck = SmallButton("Run check");
            runCheck.Click += (s, e) => OpenFile(Paths.CheckTool, "The check tool isn't installed.");
            var openFolder = SmallButton("Open settings folder");
            openFolder.Click += (s, e) => { Directory.CreateDirectory(Paths.DataDir); Process.Start("explorer.exe", "\"" + Paths.DataDir + "\""); };
            toolRow.Controls.AddRange(new Control[] { openLog, runCheck, openFolder });
            tools.Controls.Add(toolRow);

            var updRow = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0, 10, 0, 2) };
            _updateBtn = SmallButton("Check for updates");
            _updateBtn.Click += async (s, e) => await OnUpdateClicked();
            _updateText = new Label { AutoSize = true, ForeColor = Text2, Margin = new Padding(10, 7, 0, 0) };
            updRow.Controls.AddRange(new Control[] { _updateBtn, _updateText });
            tools.Controls.Add(updRow);
            _updateProgress = new ProgressBar { Width = 300, Height = 10, Visible = false, Margin = new Padding(2, 4, 0, 2) };
            tools.Controls.Add(_updateProgress);
            stack.Controls.Add(tools);

            _settingsControls = new Control[] { win, wheel, deck, colour, steps };

            // Any edit saves shortly afterwards.
            EventHandler changed = (s, e) => { if (!_loading) { _saveTimer.Stop(); _saveTimer.Start(); } };
            _showWindows.CheckedChanged += changed;
            _invert.CheckedChanged += changed;
            _deckShow.CheckedChanged += changed;
            _rim.SelectedIndexChanged += changed;
            foreach (var n in new[] { _range, _deckW, _deckH, _nudgeMm, _nudgeDeg }) n.ValueChanged += changed;
        }

        private FlowLayoutPanel Card(string title)
        {
            var card = new FlowLayoutPanel
            {
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink,
                BackColor = Panel,
                Padding = new Padding(14, 10, 14, 12),
                Margin = new Padding(0, 0, 0, 12),
            };
            card.Paint += (s, e) =>
            {
                using (var p = new Pen(Edge)) e.Graphics.DrawRectangle(p, 0, 0, card.Width - 1, card.Height - 1);
            };
            card.Controls.Add(new Label { Text = title, Font = _head, ForeColor = Accent, AutoSize = true, Margin = new Padding(0, 0, 0, 6) });
            return card;
        }

        private Label Line(Control parent, string text)
        {
            var l = new Label { Text = text, AutoSize = true, ForeColor = Text1, Margin = new Padding(0, 2, 0, 2) };
            parent.Controls.Add(l);
            return l;
        }

        private void Hint(Control parent, string text)
        {
            parent.Controls.Add(new Label { Text = text, AutoSize = true, ForeColor = Text2, Font = _small, Margin = new Padding(0, 4, 0, 0) });
        }

        private CheckBox Check(Control parent, string text)
        {
            var c = new CheckBox { Text = text, AutoSize = true, ForeColor = Text1, Margin = new Padding(0, 3, 0, 3) };
            parent.Controls.Add(c);
            return c;
        }

        private NumericUpDown Num(decimal min, decimal max, decimal step, int decimals)
        {
            return new NumericUpDown
            {
                Minimum = min, Maximum = max, Increment = step, DecimalPlaces = decimals,
                Width = 90, BackColor = Bg, ForeColor = Text1, BorderStyle = BorderStyle.FixedSingle,
            };
        }

        private void Row(Control parent, string label, Control input)
        {
            var row = new FlowLayoutPanel { AutoSize = true, WrapContents = false, Margin = new Padding(0, 3, 0, 3) };
            if (label.Length > 0)
                row.Controls.Add(new Label { Text = label, AutoSize = false, Width = 300, Height = 26, TextAlign = ContentAlignment.MiddleLeft, ForeColor = Text1 });
            row.Controls.Add(input);
            parent.Controls.Add(row);
        }

        private Button SmallButton(string text)
        {
            var b = new Button
            {
                Text = text, AutoSize = true, FlatStyle = FlatStyle.Flat, BackColor = Bg, ForeColor = Text1,
                Padding = new Padding(8, 2, 8, 2), Margin = new Padding(0, 2, 8, 2), Cursor = Cursors.Hand,
            };
            b.FlatAppearance.BorderColor = Edge;
            b.FlatAppearance.MouseOverBackColor = Color.FromArgb(50, 44, 60);
            return b;
        }

        // =====================================================================================
        // Settings
        // =====================================================================================
        private void LoadSettings()
        {
            var cfg = ConfigStore.Load();
            bool have = cfg != null;
            _noConfig.Visible = !have;
            foreach (var c in _settingsControls) c.Enabled = have;
            if (!have) return;

            _loading = true;
            try
            {
                _showWindows.Checked = ConfigStore.Bool(cfg, "enabled", true);

                var steering = ConfigStore.Obj(cfg, "steering");
                _range.Value = Clamp(_range, ConfigStore.Num(steering, "range_deg", 900));
                _invert.Checked = ConfigStore.Bool(steering, "invert", false);

                string active = ConfigStore.Str(cfg, "active_wheel", "");
                _rim.Items.Clear();
                foreach (var w in ConfigStore.List(cfg, "wheels").OfType<Dictionary<string, object>>())
                    _rim.Items.Add(new RimItem(ConfigStore.Str(w, "id", "?"), ConfigStore.Str(w, "name", "Wheel")));
                for (int i = 0; i < _rim.Items.Count; i++)
                    if (((RimItem)_rim.Items[i]).Id == active) _rim.SelectedIndex = i;
                if (_rim.SelectedIndex < 0 && _rim.Items.Count > 0) _rim.SelectedIndex = 0;

                var deck = DeckWindow(cfg);
                _deckShow.Checked = ConfigStore.Bool(deck, "enabled", true);
                _deckW.Value = Clamp(_deckW, ConfigStore.Num(deck, "width_mm", 130));
                _deckH.Value = Clamp(_deckH, ConfigStore.Num(deck, "height_mm", 95));

                _nudgeMm.Value = Clamp(_nudgeMm, ConfigStore.Num(cfg, "nudge_mm", 5));
                _nudgeDeg.Value = Clamp(_nudgeDeg, ConfigStore.Num(cfg, "nudge_deg", 1));

                ShowColour(KeyColour(cfg));
            }
            finally
            {
                _loading = false;
            }
        }

        private void Save()
        {
            _ignoreWatcherUntil = DateTime.Now.AddSeconds(1);
            bool ok = ConfigStore.Update(cfg =>
            {
                cfg["enabled"] = _showWindows.Checked;
                var steering = ConfigStore.Obj(cfg, "steering");
                steering["range_deg"] = (double)_range.Value;
                steering["invert"] = _invert.Checked;
                if (_rim.SelectedItem is RimItem rim) cfg["active_wheel"] = rim.Id;
                var deck = DeckWindow(cfg);
                if (deck != null)
                {
                    deck["enabled"] = _deckShow.Checked;
                    deck["width_mm"] = (double)_deckW.Value;
                    deck["height_mm"] = (double)_deckH.Value;
                }
                cfg["nudge_mm"] = (double)_nudgeMm.Value;
                cfg["nudge_deg"] = (double)_nudgeDeg.Value;
            });
            if (!ok) MessageBox.Show(this, "Couldn't save the settings. Try again in a moment.", Text);
        }

        private void ResetCentre()
        {
            _ignoreWatcherUntil = DateTime.Now.AddSeconds(1);
            ConfigStore.Update(cfg => ConfigStore.Obj(cfg, "steering")["centre_offset_deg"] = 0.0);
            MessageBox.Show(this, "Steering centre reset. To set it exactly, hold the wheel straight in edit mode and press X.", Text);
        }

        private void PickColour()
        {
            var cfg = ConfigStore.Load();
            if (cfg == null) return;
            using (var dlg = new ColorDialog { Color = KeyColour(cfg), FullOpen = true })
            {
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                var c = dlg.Color;
                _ignoreWatcherUntil = DateTime.Now.AddSeconds(1);
                ConfigStore.Update(x => x["key_colour_rgb"] = new ArrayList { (int)c.R, (int)c.G, (int)c.B });
                ShowColour(c);
            }
        }

        private void ShowColour(Color c)
        {
            _swatch.BackColor = c;
            _swatchText.Text = $"Red {c.R}, Green {c.G}, Blue {c.B}";
        }

        private static Color KeyColour(Dictionary<string, object> cfg)
        {
            var l = ConfigStore.List(cfg, "key_colour_rgb");
            if (l.Count == 3)
            {
                try { return Color.FromArgb(Convert.ToInt32(l[0]), Convert.ToInt32(l[1]), Convert.ToInt32(l[2])); } catch { }
            }
            return Color.FromArgb(255, 0, 255);
        }

        private static Dictionary<string, object> DeckWindow(Dictionary<string, object> cfg)
        {
            var wins = ConfigStore.List(cfg, "windows").OfType<Dictionary<string, object>>().ToList();
            return wins.FirstOrDefault(w => ConfigStore.Str(w, "name", "") == "Stream Deck") ?? wins.FirstOrDefault();
        }

        private static decimal Clamp(NumericUpDown n, double v)
        {
            decimal d = (decimal)v;
            return Math.Max(n.Minimum, Math.Min(n.Maximum, d));
        }

        private void WatchConfig()
        {
            try
            {
                Directory.CreateDirectory(Paths.DataDir);
                _watcher.Path = Paths.DataDir;
                _watcher.Filter = "config.json";
                _watcher.NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.Size;
                FileSystemEventHandler h = (s, e) => BeginInvoke((Action)(() =>
                {
                    if (DateTime.Now < _ignoreWatcherUntil) return;
                    _reloadTimer.Stop();
                    _reloadTimer.Start(); // changes made in the headset show up here
                }));
                _watcher.Changed += h;
                _watcher.Created += h;
                _watcher.Renamed += (s, e) => h(s, e);
                _watcher.EnableRaisingEvents = true;
            }
            catch { }
        }

        private sealed class RimItem
        {
            public readonly string Id, Name;
            public RimItem(string id, string name) { Id = id; Name = name; }
            public override string ToString() => Name;
        }

        // =====================================================================================
        // Status
        // =====================================================================================
        private void RefreshStatus()
        {
            // Is the layer switched on for games?
            bool registered = false;
            try
            {
                using (var key = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)
                           .OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"))
                {
                    object v = key?.GetValue(Paths.ManifestFile);
                    registered = v is int i && i == 0;
                }
            }
            catch { }
            _statusLayer.Text = registered ? "● Active — loads into OpenXR games" : "● Not registered — reinstall Handy Viewer 3D";
            _statusLayer.ForeColor = registered ? Good : Warn;

            // What happened last time a game loaded it?
            if (!File.Exists(Paths.LogFile))
            {
                _statusLast.Text = "Last used: not used in a game yet";
                _statusWheel.Text = "Wheel base: —";
                return;
            }
            string game = null, wheel = null, error = null;
            try
            {
                string[] lines;
                using (var fs = new FileStream(Paths.LogFile, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                using (var sr = new StreamReader(fs))
                    lines = sr.ReadToEnd().Split('\n');
                foreach (var raw in lines)
                {
                    string l = raw.Trim();
                    int at = l.IndexOf(" loaded into ", StringComparison.Ordinal);
                    if (game == null && at >= 0 && l.IndexOf(".exe", StringComparison.OrdinalIgnoreCase) > 0)
                        game = Path.GetFileNameWithoutExtension(l.Substring(at + 13).Trim());
                    int wb = l.IndexOf("Wheel base found: ", StringComparison.Ordinal);
                    if (wb >= 0) wheel = l.Substring(wb + 18).Split('(')[0].Trim();
                    if (l.Contains("windows off for this session") || l.Contains("windows disabled")) error = l.Substring(Math.Min(14, l.Length));
                }
            }
            catch { }
            var when = File.GetLastWriteTime(Paths.LogFile);
            string whenText = when.Date == DateTime.Today ? "today " + when.ToString("HH:mm") : when.ToString("d MMM HH:mm");
            _statusLast.Text = "Last used: " + (game ?? "unknown") + ", " + whenText + (error != null ? "  — problem: " + error : "");
            _statusLast.ForeColor = error != null ? Warn : Text1;
            _statusWheel.Text = "Wheel base: " + (wheel ?? "not found last time");
        }

        private void OpenFile(string path, string missing)
        {
            if (!File.Exists(path)) { MessageBox.Show(this, missing, Text); return; }
            try { Process.Start(new ProcessStartInfo { FileName = path, UseShellExecute = true }); }
            catch (Exception ex) { MessageBox.Show(this, ex.Message, Text); }
        }

        // =====================================================================================
        // Updates
        // =====================================================================================
        private async System.Threading.Tasks.Task CheckForUpdates(bool quiet)
        {
            _updateBtn.Enabled = false;
            if (!quiet) _updateText.Text = "Checking…";
            try
            {
                var latest = await Updater.GetLatestAsync();
                if (Updater.IsNewer(latest))
                {
                    _pendingUpdate = latest;
                    _updateBtn.Text = "Install " + latest.Version;
                    _updateBtn.ForeColor = Accent;
                    _updateText.Text = "A new version is available.";
                    _updateText.ForeColor = Accent;
                }
                else
                {
                    _updateText.Text = "You're up to date (" + Paths.AppVersion + ").";
                    _updateText.ForeColor = Text2;
                }
            }
            catch (Exception ex)
            {
                _updateText.Text = quiet ? "" : ex.Message;
                _updateText.ForeColor = Warn;
            }
            finally
            {
                _updateBtn.Enabled = true;
            }
        }

        private async System.Threading.Tasks.Task OnUpdateClicked()
        {
            if (_pendingUpdate == null)
            {
                await CheckForUpdates(quiet: false);
                return;
            }
            _updateBtn.Enabled = false;
            _updateProgress.Visible = true;
            _updateText.Text = "Downloading " + _pendingUpdate.Version + "…";
            try
            {
                await Updater.DownloadAndRunAsync(_pendingUpdate, new Progress<int>(p => _updateProgress.Value = Math.Max(0, Math.Min(100, p))));
                _updateText.Text = "Installing… the app will reopen when it's done.";
                Close();
            }
            catch (Exception ex)
            {
                _updateText.Text = "Update failed: " + ex.Message;
                _updateText.ForeColor = Warn;
                _updateBtn.Enabled = true;
                _updateProgress.Visible = false;
            }
        }
    }
}
