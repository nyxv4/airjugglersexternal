using NyxExternal.SDK;
using NyxExternal.SDK.structures;
using NyxExternal.games.universal;
using Offsets;
using Options;
using System;
using System.Threading;
using SDKInstance = NyxExternal.SDK.Instance;

namespace NyxExternal.games.universal.knockback
{
    internal static class knockbackhack
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
                    var s = Settings.Knockback;
                    if (SDKInstance.Mem == null || !s.Enabled || !s.KbKey.IsPressed()) { Thread.Sleep(20); continue; }

                    var root = nyxutil.LocalRoot();
                    if (!root.IsValid) { Thread.Sleep(30); continue; }
                    long prim = SDKInstance.Mem.ReadPtr(root.Address + Offsets.BasePart.Primitive);
                    if (prim == 0) { Thread.Sleep(20); continue; }

                    var vel = SDKInstance.Mem.Read<Vector3>(prim + Offsets.Primitive.AssemblyLinearVelocity);
                    SDKInstance.Mem.Write<Vector3>(prim + Offsets.Primitive.AssemblyLinearVelocity, new Vector3
                    {
                        x = vel.x * s.Dampen,
                        y = vel.y * s.Dampen,
                        z = vel.z * s.Dampen
                    });

                    Thread.Sleep(5);
                }
                catch { Thread.Sleep(50); }
            }
        }
    }
}
