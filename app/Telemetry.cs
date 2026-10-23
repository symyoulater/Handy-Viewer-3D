using System;
using System.Collections.Generic;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text;
using System.Web.Script.Serialization;

namespace ThinPlaceVR
{
    // Reads live data from games for the game-data panels (ui/panels/*.html).
    // Each reader returns a small dictionary that is sent to its page as JSON.
    internal static class Telemetry
    {
        private static readonly JavaScriptSerializer Json = new JavaScriptSerializer();

        public static string Read(string page)
        {
            Dictionary<string, object> d;
            try
            {
                d = page == "acevo" ? AcEvo.Read() : page == "elite" ? Elite.Read() : null;
            }
            catch (Exception ex)
            {
                d = new Dictionary<string, object> { ["ok"] = false, ["message"] = ex.Message };
            }
            return d == null ? null : Json.Serialize(d);
        }

        // ---- Assetto Corsa EVO -------------------------------------------------------------------
        // The game shares its data in memory ("Local\acevo_pmf_physics" and "..._graphics").
        // Offsets follow the layout published by the community (live-telemetry-evo's ctypes
        // structures, pack 4); values are sanity-checked so a changed layout shows a message
        // rather than nonsense.
        private static class AcEvo
        {
            private static MemoryMappedFile _phys, _gfx, _stat;
            private static MemoryMappedViewAccessor _p, _g, _s;
            private static bool _versionLogged;

            private static bool Open()
            {
                if (_p != null && _g != null) return true;
                try
                {
                    _phys = MemoryMappedFile.OpenExisting("Local\\acevo_pmf_physics", MemoryMappedFileRights.Read);
                    _gfx = MemoryMappedFile.OpenExisting("Local\\acevo_pmf_graphics", MemoryMappedFileRights.Read);
                    _p = _phys.CreateViewAccessor(0, 800, MemoryMappedFileAccess.Read);
                    _g = _gfx.CreateViewAccessor(0, 4900, MemoryMappedFileAccess.Read);
                    try
                    {
                        _stat = MemoryMappedFile.OpenExisting("Local\\acevo_pmf_static", MemoryMappedFileRights.Read);
                        _s = _stat.CreateViewAccessor(0, 240, MemoryMappedFileAccess.Read);
                    }
                    catch { }
                    return true;
                }
                catch
                {
                    Close();
                    return false;
                }
            }

            private static void Close()
            {
                _p?.Dispose(); _g?.Dispose(); _s?.Dispose();
                _phys?.Dispose(); _gfx?.Dispose(); _stat?.Dispose();
                _p = _g = _s = null;
                _phys = _gfx = _stat = null;
            }

            private static string Text(MemoryMappedViewAccessor v, int offset, int len)
            {
                var b = new byte[len];
                v.ReadArray(offset, b, 0, len);
                int n = Array.IndexOf(b, (byte)0);
                return Encoding.ASCII.GetString(b, 0, n < 0 ? len : n).Trim();
            }

            public static Dictionary<string, object> Read()
            {
                if (!Open()) return new Dictionary<string, object> { ["ok"] = false, ["message"] = "Waiting for AC EVO - start a session" };
                if (!_versionLogged && _s != null)
                {
                    _versionLogged = true;
                    Updater.Log("AC EVO data: shared memory version " + Text(_s, 0, 15) + ", game " + Text(_s, 15, 15));
                }

                int gear = _p.ReadInt32(16);
                int rpm = _p.ReadInt32(20);
                float speed = _p.ReadSingle(28);
                float gas = _p.ReadSingle(4), brake = _p.ReadSingle(8);
                if (gear < 0 || gear > 12 || rpm < 0 || rpm > 30000 || speed < -5 || speed > 600)
                    return new Dictionary<string, object> { ["ok"] = false, ["message"] = "AC EVO's data layout has changed - Thin Place VR needs an update" };

                var tyres = new List<object>();
                foreach (int baseOff in new[] { 220, 476, 732, 988 }) // FL FR RL RR
                {
                    tyres.Add(new Dictionary<string, object>
                    {
                        ["psi"] = Math.Round(_g.ReadSingle(baseOff + 8), 1),
                        ["temp"] = Math.Round(_g.ReadSingle(baseOff + 28)),
                        ["tempL"] = Math.Round(_g.ReadSingle(baseOff + 24)),
                        ["tempR"] = Math.Round(_g.ReadSingle(baseOff + 32)),
                        ["brake"] = Math.Round(_g.ReadSingle(baseOff + 16)),
                        ["normTemp"] = Math.Round(_g.ReadSingle(baseOff + 124), 2),
                        ["normPsi"] = Math.Round(_g.ReadSingle(baseOff + 104), 2),
                        ["lock"] = _g.ReadByte(baseOff + 4) != 0,
                    });
                }

                return new Dictionary<string, object>
                {
                    ["ok"] = true,
                    ["status"] = _g.ReadInt32(4),
                    ["gear"] = gear == 0 ? "R" : gear == 1 ? "N" : (gear - 1).ToString(),
                    ["rpm"] = rpm,
                    ["rpmPct"] = Math.Round(_g.ReadSingle(72), 3),
                    ["shiftUp"] = _g.ReadByte(43) != 0,
                    ["limiter"] = _g.ReadByte(42) != 0,
                    ["speed"] = (int)Math.Round(speed),
                    ["gas"] = Math.Round(gas, 2),
                    ["brake"] = Math.Round(brake, 2),
                    ["tc"] = _g.ReadByte(45) != 0,
                    ["abs"] = _g.ReadByte(46) != 0,
                    ["deltaMs"] = _g.ReadInt32(184),
                    ["lapMs"] = _g.ReadInt32(188),
                    ["predictedMs"] = _g.ReadInt32(192),
                    ["lastMs"] = _g.ReadInt32(2396),
                    ["bestMs"] = _g.ReadInt32(2400),
                    ["lap"] = _g.ReadInt32(2384),
                    ["pos"] = _g.ReadUInt32(2388),
                    ["cars"] = _g.ReadUInt32(2392),
                    ["flag"] = _g.ReadInt32(2404),
                    ["fuel"] = Math.Round(_g.ReadSingle(196), 1),
                    ["fuelPerLap"] = Math.Round(_g.ReadSingle(1460), 2),
                    ["fuelLaps"] = Math.Round(_g.ReadSingle(1464), 1),
                    ["maxFuel"] = Math.Round(_g.ReadSingle(3928), 1),
                    ["tyres"] = tyres,
                    ["track"] = _s != null ? Text(_s, 136, 33) : "",
                };
            }
        }

        // ---- Elite Dangerous -----------------------------------------------------------------------
        // The game writes Status.json in Saved Games whenever something changes.
        private static class Elite
        {
            private static DateTime _lastWrite;
            private static Dictionary<string, object> _last;

            private static string Folder() => Path.Combine(
                Environment.GetEnvironmentVariable("USERPROFILE") ?? "", "Saved Games", "Frontier Developments", "Elite Dangerous");

            public static Dictionary<string, object> Read()
            {
                string file = Path.Combine(Folder(), "Status.json");
                if (!File.Exists(file)) return new Dictionary<string, object> { ["ok"] = false, ["message"] = "Waiting for Elite Dangerous" };
                var t = File.GetLastWriteTimeUtc(file);
                if (_last != null && t == _lastWrite) return _last;
                string text;
                using (var fs = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                using (var sr = new StreamReader(fs)) text = sr.ReadToEnd();
                if (text.Trim().Length == 0) return _last ?? new Dictionary<string, object> { ["ok"] = false, ["message"] = "Waiting for Elite Dangerous" };
                var s = new JavaScriptSerializer().DeserializeObject(text) as Dictionary<string, object>;
                if (s == null) return _last;

                long flags = Convert.ToInt64(s.TryGetValue("Flags", out object f) ? f : 0);
                bool Bit(int n) => (flags & (1L << n)) != 0;
                var d = new Dictionary<string, object>
                {
                    ["ok"] = true,
                    ["docked"] = Bit(0), ["landed"] = Bit(1), ["gear"] = Bit(2), ["shields"] = Bit(3),
                    ["supercruise"] = Bit(4), ["faOff"] = Bit(5), ["hardpoints"] = Bit(6), ["wing"] = Bit(7),
                    ["lights"] = Bit(8), ["scoop"] = Bit(9), ["silent"] = Bit(10), ["fuelScooping"] = Bit(11),
                    ["massLocked"] = Bit(16), ["fsdCharging"] = Bit(17), ["fsdCooldown"] = Bit(18),
                    ["lowFuel"] = Bit(19), ["overheating"] = Bit(20), ["danger"] = Bit(22), ["interdicted"] = Bit(23),
                    ["inSrv"] = Bit(26), ["analysis"] = Bit(27), ["nightVision"] = Bit(28),
                    ["legal"] = s.TryGetValue("LegalState", out object ls) ? Convert.ToString(ls) : "",
                    ["cargo"] = s.TryGetValue("Cargo", out object cg) ? Convert.ToDouble(cg) : 0.0,
                    ["balance"] = s.TryGetValue("Balance", out object bal) ? Convert.ToDouble(bal) : -1.0,
                };
                if (s.TryGetValue("Pips", out object pips) && pips is System.Collections.ArrayList pl && pl.Count == 3)
                    d["pips"] = new[] { Convert.ToInt32(pl[0]), Convert.ToInt32(pl[1]), Convert.ToInt32(pl[2]) };
                if (s.TryGetValue("Fuel", out object fu) && fu is Dictionary<string, object> fd)
                {
                    d["fuelMain"] = fd.TryGetValue("FuelMain", out object fm) ? Math.Round(Convert.ToDouble(fm), 2) : 0.0;
                    d["fuelReservoir"] = fd.TryGetValue("FuelReservoir", out object fr) ? Math.Round(Convert.ToDouble(fr), 2) : 0.0;
                }
                if (s.TryGetValue("Destination", out object de) && de is Dictionary<string, object> dd)
                    d["destination"] = dd.TryGetValue("Name", out object dn) ? Convert.ToString(dn) : "";
                if (s.TryGetValue("Altitude", out object alt)) d["altitude"] = Convert.ToDouble(alt);
                if (s.TryGetValue("Heading", out object hd)) d["heading"] = Convert.ToDouble(hd);
                _lastWrite = t;
                _last = d;
                return d;
            }
        }
    }
}
