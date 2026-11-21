using System;
using System.Net;
using System.Reflection;
using System.Text.RegularExpressions;
using NyxExternal.storage;
using NyxExternal.logging;

// Offsets are fetched from imtheo.lol (theo's dumper) at runtime.
namespace FFlagOffsets
{
    public static class Info
    {
        public static string ClientVersion = "Fetching...";
    }

    public static class FFlagList
    {
        public static long Pointer = 0;
        public static long ToFlag = 0;
        public static long ToValue = 0;
    }

    public static class FFlags
    {
        public static long NextGenReplicatorEnabledWrite4 = 0;
    }

    public static class Sync
    {
        public static bool Fetch()
        {
            try
            {
                using (WebClient client = new WebClient())
                {
                    string fullVersion = "version-" + Storage.RobloxVersion;
                    string url = $"https://imtheo.lol/Offsets/{fullVersion}/FFlags.cs";
                    string fallbackUrl = "https://imtheo.lol/Offsets/FFlags.cs";

                    string data = "";
                    try
                    {
                        data = client.DownloadString(url);
                    }
                    catch
                    {
                        LogsWindow.Log("[FFlags] Versioned URL failed, trying fallback...");
                        data = client.DownloadString(fallbackUrl);
                    }

                    if (string.IsNullOrEmpty(data)) return false;

                    var vMatch = Regex.Match(data, @"ClientVersion = ""([^""]+)""");
                    if (vMatch.Success) Info.ClientVersion = vMatch.Groups[1].Value;

                    Assembly assembly = typeof(Info).Assembly;

                    var classMatches = Regex.Matches(data, @"public static class (\w+)\s*\{([\s\S]*?)\}");

                    foreach (Match classMatch in classMatches)
                    {
                        string className = classMatch.Groups[1].Value;
                        string classContent = classMatch.Groups[2].Value;

                        if (className == "Info" || className == "Sync") continue;

                        Type targetType = assembly.GetType("FFlagOffsets." + className);
                        if (targetType == null) continue;

                        var fieldMatches = Regex.Matches(classContent, @"public const long\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+);");

                        foreach (Match fieldMatch in fieldMatches)
                        {
                            string fieldName = fieldMatch.Groups[1].Value;
                            string valueStr = fieldMatch.Groups[2].Value;

                            long value = valueStr.StartsWith("0x")
                                ? Convert.ToInt64(valueStr, 16)
                                : Convert.ToInt64(valueStr);

                            FieldInfo field = targetType.GetField(fieldName, BindingFlags.Public | BindingFlags.Static);
                            if (field != null)
                            {
                                field.SetValue(null, value);
                            }
                        }
                    }

                    return true;
                }
            }
            catch (Exception ex)
            {
                LogsWindow.Log("[FFlags] Critical Error: " + ex.Message);
                return false;
            }
        }
    }
}
