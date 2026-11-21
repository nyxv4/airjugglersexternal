using NyxExternal.SDK;
using NyxExternal.SDK.caches;
using NyxExternal.SDK.structures;
using NyxExternal.SDK.worldtoscreen;
using NyxExternal.storage;
using NyxExternal.features.games.universal.checks.teamcheck;
using NyxExternal.features.games.universal.checks.downedcheck;
using NyxExternal.features.games.universal.checks.transparencycheck;
using Offsets;
using Options;
using System.Collections.Generic;
using System.Linq;
using SDKInstance = NyxExternal.SDK.Instance;

namespace NyxExternal.games.universal
{
    internal static class nyxutil
    {
        public static SDKInstance LocalRoot()
        {
            var lp = Storage.LocalPlayerInstance;
            if (!lp.IsValid) return new SDKInstance(0);
            var ch = lp.GetCharacter();
            if (!ch.IsValid) return new SDKInstance(0);
            var root = ch.FindFirstChild("HumanoidRootPart");
            return root.IsValid ? root : new SDKInstance(0);
        }

        public static Vector3 PartPos(SDKInstance part, Dictionary<long, long> cache)
        {
            if (!part.IsValid) return new Vector3();
            if (!cache.TryGetValue(part.Address, out long ptr))
            {
                ptr = SDKInstance.Mem.ReadPtr(part.Address + Offsets.BasePart.Primitive);
                if (ptr != 0) cache[part.Address] = ptr;
            }
            return ptr != 0 ? SDKInstance.Mem.Read<Vector3>(ptr + Offsets.Primitive.Position) : new Vector3();
        }

        public static Vector3 LocalPos(Dictionary<long, long> cache)
        {
            var root = LocalRoot();
            return root.IsValid ? PartPos(root, cache) : new Vector3();
        }

        public static bool IsEnemy(RobloxPlayer p)
        {
            var lp = Storage.LocalPlayerInstance;
            if (!lp.IsValid || p.address == 0 || p.address == lp.Address || p.Health <= 0) return false;
            if (Settings.Checks.TeamCheck && TeamCheck.isteammate(p)) return false;
            if (Settings.Checks.DownedCheck && DownedCheck.is_downed(p)) return false;
            if (Settings.Checks.TransparencyCheck && TransparencyCheck.is_clear(p)) return false;
            return true;
        }

        public static IEnumerable<RobloxPlayer> Enemies()
        {
            var list = playerobjects.CachedPlayerObjects;
            if (list == null) yield break;
            foreach (var p in list)
                if (IsEnemy(p)) yield return p;
        }

        public static bool InRange(Vector3 a, Vector3 b, float range)
        {
            float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            return dx * dx + dy * dy + dz * dz <= range * range;
        }

        public static Vector2 WindowCenter()
        {
            if (WorldToScreenHelper.GetWindowInfo(out var res, out var pos))
                return new Vector2 { x = pos.x + res.x / 2f, y = pos.y + res.y / 2f };
            return new Vector2();
        }
    }
}
