using NyxExternal.SDK;
using NyxExternal.storage;
using Offsets;
using Options;
using System;
using System.Threading;
using SDKInstance = NyxExternal.SDK.Instance;

namespace NyxExternal.games.universal.gravity
{
    internal static class gravityhack
    {
        private const float NORMAL_GRAVITY = 196.2f;

        private static bool vibing = false;
        private static Thread brain;
        private static bool wasHeld = false;

        public static void Start()
        {
            if (vibing) return;
            vibing = true;
            brain = new Thread(run_it) { IsBackground = true };
            brain.Start();
        }

        public static void Stop()
        {
            vibing = false;
            restore();
        }

        private static SDKInstance GetWorld()
        {
            var ws = Storage.WorkspaceInstance;
            if (!ws.IsValid) return new SDKInstance(0);
            var w = ws.FindFirstChildOfClass("World");
            return w.IsValid ? w : ws.FindFirstChild("World");
        }

        private static void restore()
        {
            try
            {
                var w = GetWorld();
                if (w.IsValid) SDKInstance.Mem.Write<float>(w.Address + Offsets.World.Gravity, NORMAL_GRAVITY);
            }
            catch { }
        }

        private static void run_it()
        {
            while (vibing)
            {
                try
                {
                    var s = Settings.Gravity;
                    bool held = SDKInstance.Mem != null && s.Enabled && s.GravKey.IsPressed();

                    if (!held)
                    {
                        if (wasHeld) { restore(); wasHeld = false; }
                        Thread.Sleep(30);
                        continue;
                    }
                    wasHeld = true;

                    var w = GetWorld();
                    if (w.IsValid) SDKInstance.Mem.Write<float>(w.Address + Offsets.World.Gravity, s.Value);
                    Thread.Sleep(30);
                }
                catch { Thread.Sleep(100); }
            }
            restore();
        }
    }
}
