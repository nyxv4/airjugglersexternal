using NyxExternal.SDK;
using Offsets;
using Options;
using System;
using System.Threading;
using SDKInstance = NyxExternal.SDK.Instance;

namespace NyxExternal.games.universal.performance
{
    internal static class fpscap
    {
        private static bool vibing = false;
        private static Thread brain;

        public static void Start()
        {
            if (vibing) return;
            vibing = true;
            brain = new Thread(run_it) { IsBackground = true };
            brain.Start();
        }

        public static void Stop() => vibing = false;

        private static void run_it()
        {
            while (vibing)
            {
                try
                {
                    var s = Settings.Performance;
                    if (SDKInstance.Mem == null || !s.FpsCapEnabled)
                    {
                        Thread.Sleep(500);
                        continue;
                    }

                    long sched = SDKInstance.Mem.ReadPtr(SDKInstance.Mem.Base + Offsets.TaskScheduler.Pointer);
                    if (sched != 0)
                        SDKInstance.Mem.Write<int>(sched + Offsets.TaskScheduler.MaxFPS, s.MaxFps);

                    Thread.Sleep(1000);
                }
                catch { Thread.Sleep(1000); }
            }
        }
    }
}
