using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;

namespace ThinPlaceVR
{
    // Reads and writes config.json without disturbing anything the app doesn't edit
    // (window positions placed in the headset, for example). Every save re-reads the
    // file first, so changes the VR layer made in the meantime are kept.
    internal static class ConfigStore
    {
        private static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = int.MaxValue };

        public static bool Exists => File.Exists(Paths.ConfigFile);

        public static Dictionary<string, object> Load()
        {
            try
            {
                if (!Exists) return null;
                string text = ReadShared(Paths.ConfigFile);
                return Json.DeserializeObject(text) as Dictionary<string, object>;
            }
            catch
            {
                return null;
            }
        }

        // Apply a change to the latest copy of the file and save it.
        public static bool Update(Action<Dictionary<string, object>> change)
        {
            for (int attempt = 0; attempt < 5; attempt++)
            {
                try
                {
                    var cfg = Load();
                    if (cfg == null) return false;
                    change(cfg);
                    string tmp = Paths.ConfigFile + ".app.tmp";
                    File.WriteAllText(tmp, Pretty(cfg), new UTF8Encoding(false));
                    File.Copy(tmp, Paths.ConfigFile, true);
                    File.Delete(tmp);
                    return true;
                }
                catch (IOException)
                {
                    System.Threading.Thread.Sleep(100); // the game may be saving at the same moment
                }
                catch
                {
                    return false;
                }
            }
            return false;
        }

        private static string ReadShared(string path)
        {
            using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            using (var sr = new StreamReader(fs, Encoding.UTF8))
                return sr.ReadToEnd();
        }

        // ---- typed helpers -------------------------------------------------------------

        public static Dictionary<string, object> Obj(Dictionary<string, object> d, string key)
        {
            if (d.TryGetValue(key, out object o) && o is Dictionary<string, object> r) return r;
            var n = new Dictionary<string, object>();
            d[key] = n;
            return n;
        }

        public static ArrayList List(Dictionary<string, object> d, string key)
        {
            if (d.TryGetValue(key, out object o) && o is ArrayList a) return a;
            if (o is object[] arr) { var l = new ArrayList(arr); d[key] = l; return l; }
            var n = new ArrayList();
            d[key] = n;
            return n;
        }

        public static double Num(Dictionary<string, object> d, string key, double fallback)
        {
            if (d != null && d.TryGetValue(key, out object o) && o != null)
            {
                try { return Convert.ToDouble(o, CultureInfo.InvariantCulture); } catch { }
            }
            return fallback;
        }

        public static bool Bool(Dictionary<string, object> d, string key, bool fallback)
        {
            if (d != null && d.TryGetValue(key, out object o) && o is bool b) return b;
            return fallback;
        }

        public static string Str(Dictionary<string, object> d, string key, string fallback)
        {
            if (d != null && d.TryGetValue(key, out object o) && o is string s) return s;
            return fallback;
        }

        // ---- readable JSON output ------------------------------------------------------

        private static string Pretty(object value)
        {
            var sb = new StringBuilder();
            Write(sb, value, 0);
            sb.Append('\n');
            return sb.ToString();
        }

        private static void Write(StringBuilder sb, object v, int indent)
        {
            string pad = new string(' ', indent * 2);
            string padIn = new string(' ', (indent + 1) * 2);
            switch (v)
            {
                case null:
                    sb.Append("null");
                    break;
                case string s:
                    sb.Append(Json.Serialize(s));
                    break;
                case bool b:
                    sb.Append(b ? "true" : "false");
                    break;
                case IDictionary<string, object> dict:
                    if (dict.Count == 0) { sb.Append("{}"); break; }
                    sb.Append("{\n");
                    int i = 0;
                    foreach (var kv in dict)
                    {
                        sb.Append(padIn).Append(Json.Serialize(kv.Key)).Append(": ");
                        Write(sb, kv.Value, indent + 1);
                        if (++i < dict.Count) sb.Append(',');
                        sb.Append('\n');
                    }
                    sb.Append(pad).Append('}');
                    break;
                case IEnumerable list:
                    var items = new List<object>();
                    foreach (var o in list) items.Add(o);
                    bool simple = items.TrueForAll(o => !(o is IDictionary<string, object>) && !(o is ArrayList) && !(o is object[]));
                    if (items.Count == 0) { sb.Append("[]"); break; }
                    if (simple)
                    {
                        sb.Append('[');
                        for (int k = 0; k < items.Count; k++)
                        {
                            if (k > 0) sb.Append(", ");
                            Write(sb, items[k], indent + 1);
                        }
                        sb.Append(']');
                    }
                    else
                    {
                        sb.Append("[\n");
                        for (int k = 0; k < items.Count; k++)
                        {
                            sb.Append(padIn);
                            Write(sb, items[k], indent + 1);
                            if (k < items.Count - 1) sb.Append(',');
                            sb.Append('\n');
                        }
                        sb.Append(pad).Append(']');
                    }
                    break;
                case IFormattable f:
                    sb.Append(f.ToString(null, CultureInfo.InvariantCulture));
                    break;
                default:
                    sb.Append(Json.Serialize(v));
                    break;
            }
        }
    }
}
