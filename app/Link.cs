using System;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Text;

namespace ThinPlaceVR
{
    // The app's side of the live link with the VR layer. Layout must match src/shared.h exactly.
    internal sealed class Link : IDisposable
    {
        private const string Name = "Local\\ThinPlaceVR.Link";
        private const uint Magic = 0x52565054;

        // Byte offsets of each field (pack 4, see src/shared.h).
        private const int OffMagic = 0, OffVersion = 4, OffHeartbeat = 8, OffSteering = 16, OffWheelFound = 20,
            OffEnabled = 24, OffSelected = 28, OffEditMode = 32, OffGame = 36, OffRuntime = 164, OffWheelBase = 228,
            OffCmdSeq = 324, OffCmdAction = 328, OffCmdTarget = 332, OffCmdFine = 336,
            OffPanelSeq = 340, OffPanelWindow = 344, OffPanelCmd = 348, Size = 512;

        private uint _lastPanelSeq;
        private bool _panelSeqKnown;

        private MemoryMappedFile _file;
        private MemoryMappedViewAccessor _view;

        public sealed class Status
        {
            public bool GameRunning;
            public float SteeringDeg;
            public bool WheelFound;
            public bool WindowsEnabled;
            public int Selected;
            public bool EditMode;
            public string Game = "", Runtime = "", WheelBase = "";
        }

        private bool Open()
        {
            if (_view != null) return true;
            try
            {
                // Created by whichever side starts first; both see the same memory.
                _file = MemoryMappedFile.CreateOrOpen(Name, Size, MemoryMappedFileAccess.ReadWrite);
                _view = _file.CreateViewAccessor(0, Size);
                if (_view.ReadUInt32(OffMagic) != Magic)
                {
                    _view.Write(OffMagic, Magic);
                    _view.Write(OffVersion, 2u);
                }
                return true;
            }
            catch
            {
                Dispose();
                return false;
            }
        }

        [DllImport("kernel32.dll")]
        private static extern ulong GetTickCount64();

        public Status Read()
        {
            var s = new Status();
            if (!Open()) return s;
            ulong beat = _view.ReadUInt64(OffHeartbeat);
            ulong now = GetTickCount64();
            s.GameRunning = beat != 0 && now >= beat && now - beat < 2000;
            if (!s.GameRunning) return s;
            s.SteeringDeg = _view.ReadSingle(OffSteering);
            s.WheelFound = _view.ReadInt32(OffWheelFound) != 0;
            s.WindowsEnabled = _view.ReadInt32(OffEnabled) != 0;
            s.Selected = _view.ReadInt32(OffSelected);
            s.EditMode = _view.ReadInt32(OffEditMode) != 0;
            s.Game = ReadText(OffGame, 128);
            s.Runtime = ReadText(OffRuntime, 64);
            s.WheelBase = ReadText(OffWheelBase, 96);
            return s;
        }

        // Ask the layer to do something on its next frame. Returns false if no game is running.
        public bool Send(int action, int target, bool fine)
        {
            if (!Open() || !Read().GameRunning) return false;
            _view.Write(OffCmdAction, action);
            _view.Write(OffCmdTarget, target);
            _view.Write(OffCmdFine, fine ? 1 : 0);
            System.Threading.Thread.MemoryBarrier();
            _view.Write(OffCmdSeq, _view.ReadUInt32(OffCmdSeq) + 1);
            return true;
        }

        // A scroll request from the headset ([ and ] in edit mode), if a new one has arrived.
        public bool TakePanelCommand(out int window, out int cmd)
        {
            window = -1;
            cmd = 0;
            if (!Open()) return false;
            uint seq = _view.ReadUInt32(OffPanelSeq);
            if (!_panelSeqKnown) { _lastPanelSeq = seq; _panelSeqKnown = true; return false; }
            if (seq == _lastPanelSeq) return false;
            _lastPanelSeq = seq;
            window = _view.ReadInt32(OffPanelWindow);
            cmd = _view.ReadInt32(OffPanelCmd);
            return true;
        }

        private string ReadText(int offset, int max)
        {
            var bytes = new byte[max];
            _view.ReadArray(offset, bytes, 0, max);
            int n = Array.IndexOf(bytes, (byte)0);
            return Encoding.UTF8.GetString(bytes, 0, n < 0 ? max : n);
        }

        public void Dispose()
        {
            _view?.Dispose();
            _file?.Dispose();
            _view = null;
            _file = null;
        }
    }
}
