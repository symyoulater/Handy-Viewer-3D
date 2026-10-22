using System;
using System.IO;
using System.Reflection;

namespace HandyViewer3D
{
    // The same locations the VR layer uses (see src/log.cpp).
    internal static class Paths
    {
        // Documents\Handy Viewer 3D - follows OneDrive redirection, just like the layer does.
        public static string DataDir =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "Handy Viewer 3D");

        public static string ConfigFile => Path.Combine(DataDir, "config.json");

        // C:\ProgramData\Handy Viewer 3D
        public static string LogDir =>
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "Handy Viewer 3D");

        public static string LogFile => Path.Combine(LogDir, "layer.log");

        public static string AppDir => Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);

        public static string CheckTool => Path.Combine(AppDir, "HandyViewer3DCheck.exe");

        public static string ManifestFile => Path.Combine(AppDir, "XR_APILAYER_SYMYOULATER_handy_viewer.json");

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
