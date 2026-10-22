using System;
using System.IO;
using System.Reflection;

namespace ThinPlaceVR
{
    // The same locations the VR layer uses (see src/log.cpp).
    internal static class Paths
    {
        // Documents\Thin Place VR - follows OneDrive redirection, just like the layer does.
        public static string DataDir =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "Thin Place VR");

        public static string ConfigFile => Path.Combine(DataDir, "config.json");

        // C:\ProgramData\Thin Place VR
        public static string LogDir =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "Thin Place VR");

        public static string LogFile => Path.Combine(LogDir, "layer.log");

        public static string AppDir => Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);

        public static string CheckTool => Path.Combine(AppDir, "ThinPlaceVRCheck.exe");

        public static string ManifestFile => Path.Combine(AppDir, "XR_APILAYER_SYMYOULATER_thin_place_vr.json");

        public static Version AppVersion
        {
            get
            {
                var v = Assembly.GetExecutingAssembly().GetName().Version;
                return new Version(v.Major, v.Minor, Math.Max(0, v.Build));
            }
        }
    }
}
