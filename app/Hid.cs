using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.Win32.SafeHandles;

namespace ThinPlaceVR
{
    // Reads wheel bases directly over USB (HID), the same way the VR layer does (src/steering.cpp),
    // so the app can list them, find one by turning it, and show steering without a game running.
    internal static class Hid
    {
        // Same lists as src/steering.cpp - keep them in step.
        private static readonly string[] KnownBases =
        {
            "simucube", "fanatec", "moza", "gudsen", "simagic", "thrustmaster", "logitech g", "g29", "g920",
            "g923", "g pro", "driving force", "asetek", "forte", "invicta", "la prima", "vrs", "directforce",
            "cammus", "conspit", "turtle beach", "velocityone", "pxn", "accuforce", "osw", "leo bodnar",
            "sim-r", "simsteering", "t300", "t248", "t-gt", "t818", "ts-pc", "ts-xw", "tx racing", "t150", "tmx",
        };
        private static readonly string[] NotBases =
        {
            "pedal", "handbrake", "hand brake", "shifter", "sequential", "button box", "buttonbox", "joystick",
            "throttle", "hotas", "gamepad", "controller", "xbox", "dualsense", "dualshock",
        };

        public static bool IsKnownBase(string name)
        {
            string n = (name ?? "").ToLowerInvariant();
            if (NotBases.Any(n.Contains)) return false;
            return KnownBases.Any(n.Contains);
        }

        public static bool IsAuto(string match) =>
            string.IsNullOrWhiteSpace(match) || match.Trim().Equals("auto", StringComparison.OrdinalIgnoreCase);

        public sealed class Device : IDisposable
        {
            public string Name;
            public string Path;
            public SafeFileHandle Handle;
            public IntPtr Preparsed;
            public int ReportLength;
            public byte ReportId;
            public int LogicalMin, LogicalMax, BitSize;

            public string Caps = ""; // a summary of the base's inputs, for the log

            private volatile bool _streaming;
            public volatile bool Failed; // the device stopped answering (unplugged)
            private double? _latest;
            private SafeFileHandle _streamHandle;

            // Steering from -1 (left) to +1 (right), or null if nothing has been read yet.
            // The base's report stream is read on a separate connection; this returns the latest position.
            // (Bases aren't asked for their position: some, like the Simucube 2, stall for seconds when asked.)
            public double? ReadSteering()
            {
                if (Handle == null || Handle.IsInvalid || ReportLength <= 0) return null;
                if (!_streaming)
                {
                    _streaming = true;
                    var t = new Thread(Stream) { IsBackground = true, Name = "HID reader" };
                    t.Start();
                }
                return _latest;
            }

            private void Stream()
            {
                var counts = new SortedDictionary<int, int>();
                int withSteering = 0, total = 0, zeroReads = 0, dumps = 0;
                byte[] last = null;
                bool summarised = false;
                var started = DateTime.UtcNow;
                try
                {
                    _streamHandle = CreateFile(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
                    if (_streamHandle.IsInvalid)
                    {
                        Updater.Log("Couldn't open a reading connection to \"" + Name + "\" (error " + Marshal.GetLastWin32Error() + ")");
                        Failed = true;
                        return;
                    }
                    using (var fs = new FileStream(_streamHandle, FileAccess.Read, ReportLength, false))
                    {
                        var buf = new byte[ReportLength];
                        while (true)
                        {
                            int n = fs.Read(buf, 0, buf.Length);
                            if (n <= 0)
                            {
                                // Windows reported "end of data" - not normal for a HID device; give it a moment.
                                if (++zeroReads > 50) throw new IOException("the base stopped sending");
                                Thread.Sleep(20);
                                continue;
                            }
                            zeroReads = 0;
                            total++;
                            counts[buf[0]] = counts.TryGetValue(buf[0], out int c) ? c + 1 : 1;
                            var v = Decode(buf);
                            if (v != null) { _latest = v; withSteering++; }
                            // For diagnosis: log the raw bytes when a report without steering changes (first 25 changes).
                            if (v == null && dumps < 25 && (last == null || !buf.Take(n).SequenceEqual(last)))
                            {
                                dumps++;
                                Updater.Log("Raw \"" + Name + "\" " + BitConverter.ToString(buf, 0, Math.Min(n, 32)));
                                last = buf.Take(n).ToArray();
                            }
                            if (!summarised && (DateTime.UtcNow - started).TotalSeconds >= 3)
                            {
                                summarised = true;
                                Updater.Log("Reading \"" + Name + "\": " + total + " reports in 3 s (" +
                                    string.Join(", ", counts.Select(kv => "id " + kv.Key + " x" + kv.Value)) + "); steering in " + withSteering +
                                    (_latest.HasValue ? ", now " + _latest.Value.ToString("0.000") : ""));
                            }
                        }
                    }
                }
                catch (Exception ex)
                {
                    if (Handle != null && !Handle.IsClosed)
                        Updater.Log("Stopped reading \"" + Name + "\" after " + total + " reports: " + ex.Message);
                }
                _latest = null;
                if (Handle != null && !Handle.IsClosed) Failed = true;
            }

            private double? Decode(byte[] report)
            {
                if (Preparsed == IntPtr.Zero) return null;
                if (HidP_GetUsageValue(0, 0x01, 0, 0x30, out uint raw, Preparsed, report, (uint)report.Length) != HIDP_STATUS_SUCCESS) return null;
                long v = raw;
                if (LogicalMin < 0 && BitSize > 0 && BitSize < 32 && (raw & (1u << (BitSize - 1))) != 0) v = (long)(raw | (~0u << BitSize)) - (1L << 32);
                long lo = LogicalMin, hi = LogicalMax;
                if (hi < lo) hi = (1L << BitSize) - 1;
                double t = hi > lo ? (double)(v - lo) / (hi - lo) : 0.5;
                return Math.Max(-1, Math.Min(1, t * 2 - 1));
            }

            public void Dispose()
            {
                Handle?.Dispose();
                _streamHandle?.Dispose(); // ends any report-stream read
                IntPtr p = Preparsed;
                Preparsed = IntPtr.Zero;
                if (p != IntPtr.Zero) HidD_FreePreparsedData(p);
            }
        }

        // Every connected game controller that has a steering-type (X) axis.
        public static List<Device> OpenAll()
        {
            var list = new List<Device>();
            HidD_GetHidGuid(out Guid guid);
            IntPtr set = SetupDiGetClassDevs(ref guid, IntPtr.Zero, IntPtr.Zero, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (set == new IntPtr(-1)) return list;
            try
            {
                var iface = new SP_DEVICE_INTERFACE_DATA { cbSize = Marshal.SizeOf<SP_DEVICE_INTERFACE_DATA>() };
                for (uint i = 0; SetupDiEnumDeviceInterfaces(set, IntPtr.Zero, ref guid, i, ref iface); i++)
                {
                    string path = DevicePath(set, ref iface);
                    if (path == null) continue;
                    var dev = TryOpen(path);
                    if (dev != null) list.Add(dev);
                }
            }
            finally { SetupDiDestroyDeviceInfoList(set); }
            return list;
        }

        // The device the settings point at: a name to look for, or "auto" for the first known wheel base.
        public static Device OpenChosen(string match)
        {
            var all = OpenAll();
            Device pick = IsAuto(match)
                ? all.FirstOrDefault(d => IsKnownBase(d.Name))
                : all.FirstOrDefault(d => d.Name.IndexOf(match, StringComparison.OrdinalIgnoreCase) >= 0);
            foreach (var d in all) if (d != pick) d.Dispose();
            return pick;
        }

        private static Device TryOpen(string path)
        {
            var h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
            if (h.IsInvalid) { h.Dispose(); return null; }
            var d = new Device { Path = path, Handle = h };
            try
            {
                var name = new byte[512];
                if (HidD_GetProductString(h, name, name.Length))
                    d.Name = System.Text.Encoding.Unicode.GetString(name).TrimEnd('\0');
                if (string.IsNullOrWhiteSpace(d.Name)) d.Name = "Unnamed controller";
                if (!HidD_GetPreparsedData(h, out d.Preparsed)) { d.Dispose(); return null; }

                var caps = new byte[64];
                if (HidP_GetCaps(d.Preparsed, caps) != HIDP_STATUS_SUCCESS) { d.Dispose(); return null; }
                ushort usage = BitConverter.ToUInt16(caps, 0), usagePage = BitConverter.ToUInt16(caps, 2);
                d.ReportLength = BitConverter.ToUInt16(caps, 4);
                ushort valueCaps = BitConverter.ToUInt16(caps, 4 + 2 * 3 + 2 * 17 + 2 * 2);
                bool controller = usagePage == 0x01 && (usage == 0x04 || usage == 0x05 || usage == 0x08);
                if (!controller || valueCaps == 0) { d.Dispose(); return null; }

                const int size = 72;
                var vc = new byte[size * valueCaps];
                ushort n = valueCaps;
                if (HidP_GetValueCaps(0, vc, ref n, d.Preparsed) != HIDP_STATUS_SUCCESS) { d.Dispose(); return null; }
                var parts = new List<string>();
                bool found = false;
                for (int k = 0; k < n; k++)
                {
                    int o = k * size;
                    ushort page = BitConverter.ToUInt16(vc, o);
                    bool isRange = vc[o + 12] != 0;
                    ushort u0 = BitConverter.ToUInt16(vc, o + 56), u1 = BitConverter.ToUInt16(vc, o + 58);
                    parts.Add("id " + vc[o + 2] + " " + page.ToString("X2") + ":" + u0.ToString("X2") + (isRange ? "-" + u1.ToString("X2") : "") +
                        " " + BitConverter.ToUInt16(vc, o + 18) + "bit");
                    bool hasX = page == 0x01 && (isRange ? u0 <= 0x30 && u1 >= 0x30 : u0 == 0x30);
                    if (!hasX || found) continue;
                    found = true;
                    d.ReportId = vc[o + 2];
                    d.BitSize = BitConverter.ToUInt16(vc, o + 18);
                    d.LogicalMin = BitConverter.ToInt32(vc, o + 40);
                    d.LogicalMax = BitConverter.ToInt32(vc, o + 44);
                }
                d.Caps = "collection " + usagePage.ToString("X2") + ":" + usage.ToString("X2") + ", inputs: " + string.Join("; ", parts);
                if (found) return d;
            }
            catch { }
            d.Dispose();
            return null;
        }

        // Watch every controller for a few seconds and return the one whose steering moved most.
        public static string FindByTurning(TimeSpan duration)
        {
            var all = OpenAll();
            try
            {
                var min = new Dictionary<Device, double>();
                var max = new Dictionary<Device, double>();
                var end = DateTime.UtcNow + duration;
                while (DateTime.UtcNow < end)
                {
                    foreach (var d in all)
                    {
                        var v = d.ReadSteering();
                        if (v == null) continue;
                        min[d] = min.TryGetValue(d, out var a) ? Math.Min(a, v.Value) : v.Value;
                        max[d] = max.TryGetValue(d, out var b) ? Math.Max(b, v.Value) : v.Value;
                    }
                    Thread.Sleep(30);
                }
                var best = all.Where(min.ContainsKey).OrderByDescending(d => max[d] - min[d]).FirstOrDefault();
                if (best == null || max[best] - min[best] < 0.02) return null;
                return best.Name;
            }
            finally { foreach (var d in all) d.Dispose(); }
        }

        // ---- Windows API ---------------------------------------------------------------------

        private const uint DIGCF_PRESENT = 0x2, DIGCF_DEVICEINTERFACE = 0x10;
        private const uint GENERIC_READ = 0x80000000, FILE_SHARE_READ = 1, FILE_SHARE_WRITE = 2, OPEN_EXISTING = 3;
        private const int HIDP_STATUS_SUCCESS = 0x00110000;

        [StructLayout(LayoutKind.Sequential)]
        private struct SP_DEVICE_INTERFACE_DATA
        {
            public int cbSize;
            public Guid InterfaceClassGuid;
            public int Flags;
            public IntPtr Reserved;
        }

        private static string DevicePath(IntPtr set, ref SP_DEVICE_INTERFACE_DATA iface)
        {
            SetupDiGetDeviceInterfaceDetail(set, ref iface, IntPtr.Zero, 0, out int size, IntPtr.Zero);
            if (size <= 0) return null;
            IntPtr buf = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.WriteInt32(buf, IntPtr.Size == 8 ? 8 : 6); // cbSize of SP_DEVICE_INTERFACE_DETAIL_DATA_W
                if (!SetupDiGetDeviceInterfaceDetail(set, ref iface, buf, size, out _, IntPtr.Zero)) return null;
                return Marshal.PtrToStringUni(buf + 4);
            }
            finally { Marshal.FreeHGlobal(buf); }
        }

        [DllImport("hid.dll")] private static extern void HidD_GetHidGuid(out Guid guid);
        [DllImport("hid.dll")] private static extern bool HidD_GetProductString(SafeFileHandle h, byte[] buf, int len);
        [DllImport("hid.dll")] private static extern bool HidD_GetPreparsedData(SafeFileHandle h, out IntPtr data);
        [DllImport("hid.dll")] private static extern bool HidD_FreePreparsedData(IntPtr data);
        [DllImport("hid.dll")] private static extern bool HidD_GetInputReport(SafeFileHandle h, byte[] report, int len);
        [DllImport("hid.dll")] private static extern int HidP_GetCaps(IntPtr data, byte[] caps);
        [DllImport("hid.dll")] private static extern int HidP_GetValueCaps(int type, byte[] caps, ref ushort len, IntPtr data);
        [DllImport("hid.dll")] private static extern int HidP_GetUsageValue(int type, ushort page, ushort link, ushort usage, out uint value, IntPtr data, byte[] report, uint len);

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr SetupDiGetClassDevs(ref Guid guid, IntPtr enumerator, IntPtr parent, uint flags);
        [DllImport("setupapi.dll")]
        private static extern bool SetupDiEnumDeviceInterfaces(IntPtr set, IntPtr info, ref Guid guid, uint index, ref SP_DEVICE_INTERFACE_DATA data);
        [DllImport("setupapi.dll", CharSet = CharSet.Unicode)]
        private static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr set, ref SP_DEVICE_INTERFACE_DATA data, IntPtr detail, int size, out int required, IntPtr info);
        [DllImport("setupapi.dll")] private static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFile(string name, uint access, uint share, IntPtr sec, uint disposition, uint flags, IntPtr template);
    }

    // Keeps the chosen wheel base open and reads its steering in the background (for the app's live view
    // when no game is running).
    internal sealed class WheelWatcher : IDisposable
    {
        private readonly object _lock = new object();
        private Thread _thread;
        private volatile bool _run;
        private string _match = "auto";
        private string _name = "";
        private double? _value;

        public void Start()
        {
            if (_run) return;
            _run = true;
            _thread = new Thread(Loop) { IsBackground = true, Name = "Wheel watcher" };
            _thread.Start();
        }

        public void SetMatch(string match)
        {
            lock (_lock) { if (_match != (match ?? "auto")) { _match = match ?? "auto"; _value = null; _name = ""; } }
        }

        public bool TryGet(out string name, out double value)
        {
            lock (_lock)
            {
                name = _name;
                value = _value ?? 0;
                return _value.HasValue;
            }
        }

        private void Loop()
        {
            Hid.Device dev = null;
            string openedFor = null;
            DateTime nextSearch = DateTime.MinValue;
            while (_run)
            {
                string want;
                lock (_lock) want = _match;
                if (dev != null && want != openedFor) { dev.Dispose(); dev = null; }
                if (dev == null)
                {
                    if (DateTime.UtcNow < nextSearch) { Thread.Sleep(100); continue; }
                    try { dev = Hid.OpenChosen(want); } catch { dev = null; }
                    openedFor = want;
                    nextSearch = DateTime.UtcNow.AddSeconds(2);
                    lock (_lock) { _name = dev?.Name ?? ""; _value = null; }
                    if (dev == null) continue;
                    Updater.Log("Wheel base connected: \"" + dev.Name + "\" (report " + dev.ReportId + ", " + dev.ReportLength + " bytes, " + dev.BitSize + "-bit steering " + dev.LogicalMin + " to " + dev.LogicalMax + "); " + dev.Caps);
                }
                double? v = null;
                try { v = dev.ReadSteering(); } catch { }
                if (dev.Failed)
                {
                    // Unplugged: look for it again.
                    dev.Dispose();
                    dev = null;
                    lock (_lock) _value = null;
                    continue;
                }
                lock (_lock) _value = v ?? _value;
                Thread.Sleep(50);
            }
            dev?.Dispose();
        }

        public void Dispose()
        {
            _run = false;
            _thread?.Join(500);
        }
    }
}
