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
using System.Threading;
using SDKInstance = NyxExternal.SDK.Instance;

namespace NyxExternal.games.universal.hitfix
{
    internal static class hitfix
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

        private static Vector3 CamLook()
        {
            var cam = Storage.CameraInstance;
            if (!cam.IsValid) return new Vector3 { x = 0, y = 0, z = -1 };
            var rot = SDKInstance.Mem.Read<Matrix3x3>(cam.Address + Offsets.Camera.Rotation);
            return new Vector3 { x = -rot.r02, y = -rot.r12, z = -rot.r22 };
        }

        private static Vector3 CamPos()
        {
            var cam = Storage.CameraInstance;
            if (!cam.IsValid) return new Vector3();
            return SDKInstance.Mem.Read<Vector3>(cam.Address + Offsets.Camera.Position);
        }

        private static void run_it()
        {
            var cache = new Dictionary<long, long>();
            RobloxPlayer locked = default;

            while (vibing)
            {
                try
                {
                    var s = Settings.Hitfix;
                    if (SDKInstance.Mem == null || !s.Enabled || !s.HitFixKey.IsPressed())
                    {
                        locked = default;
                        Thread.Sleep(20);
                        continue;
                    }

                    var center = nyxutil.WindowCenter();
                    if (center.x == 0 && center.y == 0) { Thread.Sleep(20); continue; }

                    var local = nyxutil.LocalPos(cache);
                    var camPos = CamPos();
                    var look = CamLook();

                    if (locked.address == 0 || locked.Health <= 0 || !nyxutil.IsEnemy(locked))
                    {
                        locked = default;
                        float closest = float.MaxValue;

                        foreach (var p in nyxutil.Enemies())
                        {
                            var root = p.HumanoidRootPart.IsValid ? p.HumanoidRootPart : p.Head;
                            if (!root.IsValid) continue;
                            var wpos = nyxutil.PartPos(root, cache);
                            if (!nyxutil.InRange(local, wpos, s.Range)) continue;

                            var s2d = WorldToScreenHelper.WorldToScreen(wpos);
                            if (s2d.x == -1) continue;

                            float d = (float)Math.Sqrt(Math.Pow(s2d.x - center.x, 2) + Math.Pow(s2d.y - center.y, 2));
                            if (d < closest && d <= s.FovRadius) { closest = d; locked = p; }
                        }
                    }

                    if (locked.address != 0)
                    {
                        var root = locked.HumanoidRootPart.IsValid ? locked.HumanoidRootPart : locked.Head;
                        if (root.IsValid)
                        {
                            var wpos = nyxutil.PartPos(root, cache);
                            float dist = (float)Math.Sqrt(Math.Pow(wpos.x - camPos.x, 2) + Math.Pow(wpos.y - camPos.y, 2) + Math.Pow(wpos.z - camPos.z, 2));
                            if (dist > 0.1f)
                            {
                                var aim = new Vector3
                                {
                                    x = camPos.x + look.x * dist,
                                    y = camPos.y + look.y * dist,
                                    z = camPos.z + look.z * dist
                                };

                                long prim = SDKInstance.Mem.ReadPtr(root.Address + Offsets.BasePart.Primitive);
                                if (prim != 0)
                                {
                                    var cur = SDKInstance.Mem.Read<Vector3>(prim + Offsets.Primitive.Position);
                                    float k = Math.Clamp(s.Strength, 0f, 1f);
                                    SDKInstance.Mem.Write<Vector3>(prim + Offsets.Primitive.Position, new Vector3
                                    {
                                        x = cur.x + (aim.x - cur.x) * k,
                                        y = cur.y + (aim.y - cur.y) * k,
                                        z = cur.z + (aim.z - cur.z) * k
                                    });
                                }
                            }
                        }
                    }

                    Thread.Sleep(1);
                }
                catch { Thread.Sleep(50); }
            }
        }
    }
}
