using NyxExternal.SDK;
using NyxExternal.SDK.caches;
using NyxExternal.SDK.structures;
using NyxExternal.SDK.worldtoscreen;
using NyxExternal.storage;
using NyxExternal.games.universal;
using Offsets;
using Options;
using System;
using System.Runtime.InteropServices;
using System.Threading;
using SDKInstance = NyxExternal.SDK.Instance;

namespace NyxExternal.games.universal.triggerbot
{
    internal static class triggerbot
    {
        [DllImport("user32.dll")] private static extern void mouse_event(uint f, int dx, int dy, uint d, int e);

        private static bool vibing = false;
        private static Thread brain;
        private static DateTime last_click = DateTime.MinValue;

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
            var cache = new System.Collections.Generic.Dictionary<long, long>();

            while (vibing)
            {
                try
                {
                    var s = Settings.Triggerbot;
                    if (SDKInstance.Mem == null || !s.Enabled || !s.TriggerKey.IsPressed()) { Thread.Sleep(20); continue; }

                    var center = nyxutil.WindowCenter();
                    if (center.x == 0 && center.y == 0) { Thread.Sleep(20); continue; }

                    var local = nyxutil.LocalPos(cache);
                    Vector2 best = new Vector2 { x = -1, y = -1 };
                    float closest = float.MaxValue;

                    foreach (var p in nyxutil.Enemies())
                    {
                        var head = p.Head.IsValid ? p.Head : p.HumanoidRootPart;
                        if (!head.IsValid) continue;

                        var wpos = nyxutil.PartPos(head, cache);
                        if (wpos.x == 0 && wpos.y == 0 && wpos.z == 0) continue;
                        if (!nyxutil.InRange(local, wpos, s.Range)) continue;

                        var s2d = WorldToScreenHelper.WorldToScreen(wpos);
                        if (s2d.x == -1) continue;

                        float d = (float)Math.Sqrt(Math.Pow(s2d.x - center.x, 2) + Math.Pow(s2d.y - center.y, 2));
                        if (d < closest) { closest = d; best = s2d; }
                    }

                    if (best.x != -1 && closest <= s.FovRadius)
                    {
                        int delay = Math.Max(0, s.DelayMs);
                        if ((DateTime.UtcNow - last_click).TotalMilliseconds >= delay)
                        {
                            last_click = DateTime.UtcNow;
                            mouse_event(0x0002, 0, 0, 0, 0);
                            mouse_event(0x0004, 0, 0, 0, 0);
                        }
                    }

                    Thread.Sleep(5);
                }
                catch { Thread.Sleep(50); }
            }
        }
    }
}
