using NyxExternal.SDK;
using NyxExternal.SDK.caches;
using NyxExternal.SDK.structures;
using NyxExternal.SDK.worldtoscreen;
using NyxExternal.storage;
using NyxExternal.games.universal;
using Offsets;
using Options;
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using SDKInstance = NyxExternal.SDK.Instance;

namespace NyxExternal.games.universal.aimassist
{
    internal static class aimassist
    {
        [DllImport("user32.dll", EntryPoint = "GetCursorPos")] private static extern bool get_pos(out WorldToScreenHelper.POINT p);
        [DllImport("user32.dll", EntryPoint = "mouse_event")] private static extern void mouse_go(uint f, int dx, int dy, uint d, int e);

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
            var cache = new Dictionary<long, long>();

            while (vibing)
            {
                try
                {
                    var s = Settings.AimAssist;
                    if (SDKInstance.Mem == null || !s.Enabled || !s.AssistKey.IsPressed()) { Thread.Sleep(15); continue; }
                    if (s.OnlyWhenFiring && (GetAsyncKeyState(0x01) & 0x8000) == 0) { Thread.Sleep(10); continue; }

                    get_pos(out var mouse);
                    var center = nyxutil.WindowCenter();
                    if (center.x == 0 && center.y == 0) { Thread.Sleep(15); continue; }

                    var local = nyxutil.LocalPos(cache);
                    Vector2 best = new Vector2 { x = -1, y = -1 };
                    float closest = float.MaxValue;

                    foreach (var p in nyxutil.Enemies())
                    {
                        var head = p.Head.IsValid ? p.Head : p.HumanoidRootPart;
                        if (!head.IsValid) continue;
                        var wpos = nyxutil.PartPos(head, cache);
                        if (!nyxutil.InRange(local, wpos, s.Range)) continue;

                        var s2d = WorldToScreenHelper.WorldToScreen(wpos);
                        if (s2d.x == -1) continue;

                        float d = (float)Math.Sqrt(Math.Pow(s2d.x - mouse.x, 2) + Math.Pow(s2d.y - mouse.y, 2));
                        if (d < closest) { closest = d; best = s2d; }
                    }

                    if (best.x != -1 && closest <= s.FovRadius)
                    {
                        float dx = (best.x - mouse.x) * s.Speed;
                        float dy = (best.y - mouse.y) * s.Speed;

                        int mx = (int)Math.Round(dx);
                        int my = (int)Math.Round(dy);

                        if (mx != 0 || my != 0)
                            mouse_go(0x0001, mx, my, 0, 0);
                    }

                    Thread.Sleep(5);
                }
                catch { Thread.Sleep(50); }
            }
        }

        [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int vKey);
    }
}
