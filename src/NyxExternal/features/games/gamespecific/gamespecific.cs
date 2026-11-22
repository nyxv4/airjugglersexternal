using NyxExternal.SDK;
using NyxExternal.SDK.caches;
using Options;
using System;
using SDKInstance = NyxExternal.SDK.Instance;

namespace NyxExternal.features.games.gamespecific
{
    internal static class gamespecific
    {
        public static string GetTag(RobloxPlayer p)
        {
            try
            {
                switch (Settings.Game.Mode)
                {
                    case GameSettings.Bedwars: return BedwarsTag(p);
                    case GameSettings.MM2: return MM2Tag(p);
                    default: return "";
                }
            }
            catch { return ""; }
        }

        private static string BedwarsTag(RobloxPlayer p)
        {
            var player = new SDKInstance(p.address);
            if (!player.IsValid) return "";

            foreach (var attr in new[] { "Kit", "kit", "KitName", "CurrentKit" })
            {
                try
                {
                    string v = player.GetAttribute(attr);
                    if (!string.IsNullOrEmpty(v)) return $"[{v}]";
                }
                catch { }
            }

            if (p.Character.IsValid)
            {
                foreach (var attr in new[] { "Kit", "kit", "KitName" })
                {
                    try
                    {
                        string v = p.Character.GetAttribute(attr);
                        if (!string.IsNullOrEmpty(v)) return $"[{v}]";
                    }
                    catch { }
                }

                foreach (var child in p.Character.GetChildren())
                {
                    try
                    {
                        string cn = child.GetClass();
                        if (cn == "StringValue" || cn == "ObjectValue" || cn == "IntValue")
                        {
                            string n = child.GetName();
                            if (n.Equals("Kit", StringComparison.OrdinalIgnoreCase) || n.Contains("Kit"))
                            {
                                string v = child.GetAttribute("Value");
                                if (!string.IsNullOrEmpty(v)) return $"[{v}]";
                            }
                        }
                    }
                    catch { }
                }
            }

            return "";
        }

        private static string MM2Tag(RobloxPlayer p)
        {
            if (!p.Character.IsValid) return "";

            var tool = p.Character.FindFirstChildOfClass("Tool");
            if (!tool.IsValid) return "";

            string n = "";
            try { n = tool.GetName(); } catch { }
            if (string.IsNullOrEmpty(n)) return "";

            if (n.IndexOf("knife", StringComparison.OrdinalIgnoreCase) >= 0) return "MURDERER";
            if (n.IndexOf("gun", StringComparison.OrdinalIgnoreCase) >= 0) return "SHERIFF";
            return "";
        }
    }
}
