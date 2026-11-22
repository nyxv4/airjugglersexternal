using NyxExternal.SDK;
using NyxExternal.SDK.caches;
using NyxExternal.SDK.structures;
using NyxExternal.games.universal;
using Offsets;
using Options;
using System;
using System.Collections.Generic;
using System.Threading;
using SDKInstance = NyxExternal.SDK.Instance;

namespace NyxExternal.games.universal.hitbox
{
    internal static class hitbox
    {
        private static bool vibing = false;
        private static Thread brain;
        private static readonly Dictionary<long, (long prim, Vector3 original)> originals = new();
        private static readonly object safety = new();

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
            restore_all();
        }

        private static IEnumerable<SDKInstance> PartsOf(RobloxPlayer p)
        {
            yield return p.Head;
            yield return p.HumanoidRootPart;
            yield return p.Torso;
            yield return p.Upper_Torso;
            yield return p.Left_Arm;
            yield return p.Right_Arm;
            yield return p.Left_Leg;
            yield return p.Right_Leg;
            yield return p.Left_Hand;
            yield return p.Right_Hand;
            yield return p.Left_Foot;
            yield return p.Right_Foot;
        }

        private static void restore_all()
        {
            lock (safety)
            {
                foreach (var kv in originals)
                {
                    try { SDKInstance.Mem.Write<Vector3>(kv.Value.prim + Offsets.Primitive.Size, kv.Value.original); } catch { }
                }
                originals.Clear();
            }
        }

        private static void run_it()
        {
            var cache = new Dictionary<long, long>();
            var expanded = new HashSet<long>();
            bool wasHeld = false;

            while (vibing)
            {
                try
                {
                    var s = Settings.Hitbox;
                    bool held = SDKInstance.Mem != null && s.Enabled && s.HitboxKey.IsPressed();

                    if (!held)
                    {
                        if (wasHeld || expanded.Count > 0) { restore_all(); expanded.Clear(); }
                        wasHeld = false;
                        Thread.Sleep(20);
                        continue;
                    }
                    wasHeld = true;

                    var local = nyxutil.LocalPos(cache);
                    var touched = new HashSet<long>();

                    foreach (var p in nyxutil.Enemies())
                    {
                        var root = p.HumanoidRootPart.IsValid ? p.HumanoidRootPart : p.Head;
                        if (!root.IsValid) continue;
                        var wpos = nyxutil.PartPos(root, cache);
                        if (wpos.x == 0 && wpos.y == 0 && wpos.z == 0) continue;
                        if (!nyxutil.InRange(local, wpos, s.Range)) continue;

                        foreach (var part in PartsOf(p))
                        {
                            if (!part.IsValid) continue;
                            long prim = SDKInstance.Mem.ReadPtr(part.Address + Offsets.BasePart.Primitive);
                            if (prim == 0) continue;
                            touched.Add(part.Address);

                            lock (safety)
                            {
                                if (!originals.TryGetValue(part.Address, out var orig))
                                {
                                    orig = (prim, SDKInstance.Mem.Read<Vector3>(prim + Offsets.Primitive.Size));
                                    originals[part.Address] = orig;
                                }
                                var sz = orig.original;
                                float ex = sz.x * s.Multiplier;
                                float ey = sz.y * s.Multiplier;
                                float ez = sz.z * s.Multiplier;

                                if (Options.Settings.Game.Mode == Options.GameSettings.Bedwars)
                                {
                                    const float BEDWARS_MAX = 14.4f;
                                    if (ex > BEDWARS_MAX) ex = BEDWARS_MAX;
                                    if (ey > BEDWARS_MAX) ey = BEDWARS_MAX;
                                    if (ez > BEDWARS_MAX) ez = BEDWARS_MAX;
                                }

                                SDKInstance.Mem.Write<Vector3>(prim + Offsets.Primitive.Size, new Vector3 { x = ex, y = ey, z = ez });
                            }
                        }
                    }

                    lock (safety)
                    {
                        var stale = new List<long>();
                        foreach (var kv in originals)
                            if (!touched.Contains(kv.Key)) stale.Add(kv.Key);
                        foreach (var k in stale)
                        {
                            try { SDKInstance.Mem.Write<Vector3>(originals[k].prim + Offsets.Primitive.Size, originals[k].original); } catch { }
                            originals.Remove(k);
                        }
                    }

                    Thread.Sleep(10);
                }
                catch { Thread.Sleep(50); }
            }
            restore_all();
        }
    }
}
