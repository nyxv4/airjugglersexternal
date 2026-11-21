using NyxExternal.SDK.caches;

namespace NyxExternal.features.games.universal.checks.downedcheck
{
    public static class DownedCheck
    {
        public static bool is_downed(RobloxPlayer p)
        {
            return p.Health <= 4;
        }
    }
}