using NyxExternal.SDK;
using NyxExternal.SDK.structures;
using NyxExternal.games.universal;
using Offsets;
using Options;
using System;
using System.Threading;
using SDKInstance = NyxExternal.SDK.Instance;

namespace NyxExternal.games.universal.spinbot
{
    internal static class spinbot
    {
        private static bool vibing = false;
        private static Thread brain;
        private static float angle = 0f;
        private static DateTime last = DateTime.UtcNow;

        public static void Start()
        {
            if (vibing) return;
            vibing = true;
            brain = new Thread(run_it) { IsBackground = true };
            brain.Start();
        }

        public static void Stop() => vibing = false;

        private static Vector3 RotY(Vector3 v, float rad)
        {
            float c = (float)Math.Cos(rad), s = (float)Math.Sin(rad);
            return new Vector3 { x = v.x * c + v.z * s, y = v.y, z = -v.x * s + v.z * c };
        }

        private static void run_it()
        {
            while (vibing)
            {
                try
                {
                    var s = Settings.Spinbot;
                    bool held = SDKInstance.Mem != null && s.Enabled && s.SpinKey.IsPressed();

                    var now = DateTime.UtcNow;
                    float dt = (float)(now - last).TotalSeconds;
                    last = now;
                    angle += s.Speed * dt;
                    if (angle > 360f) angle -= 360f;

                    if (!held) { Thread.Sleep(20); continue; }

                    var root = nyxutil.LocalRoot();
                    if (!root.IsValid) { Thread.Sleep(50); continue; }
                    long prim = SDKInstance.Mem.ReadPtr(root.Address + Offsets.BasePart.Primitive);
                    if (prim == 0) { Thread.Sleep(20); continue; }

                    var cf = SDKInstance.Mem.Read<sCFrame>(prim + 0x11C);
                    float rad = angle * (float)(Math.PI / 180.0);

                    var right = RotY(new Vector3 { x = cf.r00, y = cf.r10, z = cf.r20 }, rad);
                    var up = RotY(new Vector3 { x = cf.r01, y = cf.r11, z = cf.r21 }, rad);
                    var look = RotY(new Vector3 { x = -cf.r02, y = -cf.r12, z = -cf.r22 }, rad);

                    var nc = new sCFrame
                    {
                        r00 = right.x, r10 = right.y, r20 = right.z,
                        r01 = up.x, r11 = up.y, r21 = up.z,
                        r02 = -look.x, r12 = -look.y, r22 = -look.z,
                        x = cf.x, y = cf.y, z = cf.z
                    };

                    SDKInstance.Mem.Write(prim + 0x11C, nc);
                    Thread.Sleep(1);
                }
                catch { Thread.Sleep(50); }
            }
        }
    }
}
