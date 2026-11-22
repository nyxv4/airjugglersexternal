using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using NyxExternal.helpers.keybind;
using Options;

namespace NyxExternal.config
{
    public class ConfigData
    {
        public HumanoidConfig Humanoid { get; set; } = new();
        public CameraConfig Camera { get; set; } = new();
        public VisualsConfig Visuals { get; set; } = new();
        public AimingConfig Aiming { get; set; } = new();
        public SilentConfig Silent { get; set; } = new();
        public ChecksConfig Checks { get; set; } = new();
        public NetworkConfig Network { get; set; } = new();
        public FlightConfig Flight { get; set; } = new();
        public TriggerbotConfig Triggerbot { get; set; } = new();
        public HitboxConfig Hitbox { get; set; } = new();
        public SpinbotConfig Spinbot { get; set; } = new();
        public GravityConfig Gravity { get; set; } = new();
        public KnockbackConfig Knockback { get; set; } = new();
        public HitfixConfig Hitfix { get; set; } = new();
        public MenuConfig Menu { get; set; } = new();
        public GameConfig Game { get; set; } = new();
        public AimAssistConfig AimAssist { get; set; } = new();
        public PerformanceConfig Performance { get; set; } = new();
    }

    public class HumanoidConfig
    {
        public bool WalkspeedEnabled { get; set; }
        public bool JumpPowerEnabled { get; set; }
        public float Walkspeed { get; set; } = 16f;
        public float JumpPower { get; set; } = 50f;
    }

    public class CameraConfig
    {
        public bool FOVEnabled { get; set; }
        public float FOV { get; set; } = 70f;
    }

    public class VisualsConfig
    {
        public bool BoxESP { get; set; }
        public bool FilledBox { get; set; }
        public bool Box { get; set; }
        public bool BoxFill { get; set; }
        public bool Tracers { get; set; }
        public bool Skeleton { get; set; }
        public bool Name { get; set; }
        public bool Distance { get; set; }
        public bool Health { get; set; }
        public bool ESP3D { get; set; }
        public bool HeadCircle { get; set; }
        public bool CornerESP { get; set; }
        public bool RemoveBorders { get; set; }
        public bool ChinaHat { get; set; }
        public bool LocalPlayerESP { get; set; }
        public bool WeaponESP { get; set; } = true;
        public bool TeamColorESP { get; set; } = true;
        public bool ObjectESP { get; set; }
        public int TracersStart { get; set; }
        public float NameSize { get; set; } = 12f;
        public float DistanceSize { get; set; } = 15f;
        public float TracerThickness { get; set; } = 1.5f;
        public float HeadCircleMaxScale { get; set; } = 2.5f;
    }

    public class AimingConfig
    {
        public KeyBindConfig AimbotKey { get; set; } = new("Aimbot");
        public int AimingType { get; set; }
        public int ToggleType { get; set; }
        public bool Aimbot { get; set; }
        public bool StickyAim { get; set; }
        public float Sensitivity { get; set; } = 1.0f;
        public bool Smoothness { get; set; }
        public float SmoothnessX { get; set; }
        public float SmoothnessY { get; set; } = 0.05f;
        public bool Prediction { get; set; }
        public float PredictionY { get; set; } = 2f;
        public float PredictionX { get; set; } = 2f;
        public float FOV { get; set; } = 100f;
        public bool ShowFOV { get; set; }
        public bool FillFOV { get; set; }
        public bool AnimatedFOV { get; set; }
        public float Range { get; set; } = 100f;
        public int TargetBone { get; set; }
        public bool Shake { get; set; }
        public float ShakeDistance { get; set; } = 15f;
        public float ShakeAmount { get; set; } = 5f;
    }

    public class SilentConfig
    {
        public KeyBindConfig SilentAimbotKey { get; set; } = new("SilentAimbotKey");
        public bool SilentAimbot { get; set; }
        public bool AlwaysOn { get; set; }
        public bool SilentVisualizer { get; set; }
        public bool ShowSilentFOV { get; set; }
        public bool SPrediction { get; set; }
        public float SilentFOV { get; set; } = 100f;
        public float PredictionY { get; set; } = 2f;
        public float PredictionX { get; set; } = 2f;
        public float SFOV { get; set; } = 150f;
    }

    public class ChecksConfig
    {
        public bool TeamCheck { get; set; }
        public bool DownedCheck { get; set; }
        public bool TransparencyCheck { get; set; }
        public bool WallCheck { get; set; }
    }

    public class NetworkConfig
    {
        public KeyBindConfig DeSyncBind { get; set; } = new("DeSyncBind");
        public bool DeSync { get; set; }
        public bool DeSyncVisualizer { get; set; }
    }

    public class FlightConfig
    {
        public KeyBindConfig VFlightBind { get; set; } = new("VFlightBind");
        public bool VFlight { get; set; }
        public float VFlightSpeed { get; set; } = 1.5f;
    }

    public class TriggerbotConfig
    {
        public KeyBindConfig TriggerKey { get; set; } = new("Triggerbot");
        public bool Enabled { get; set; } = true;
        public float FovRadius { get; set; } = 40f;
        public int DelayMs { get; set; } = 50;
        public float Range { get; set; } = 300f;
    }

    public class HitboxConfig
    {
        public KeyBindConfig HitboxKey { get; set; } = new("Hitbox");
        public bool Enabled { get; set; } = true;
        public float Multiplier { get; set; } = 2.5f;
        public float Range { get; set; } = 300f;
    }

    public class SpinbotConfig
    {
        public KeyBindConfig SpinKey { get; set; } = new("Spinbot");
        public bool Enabled { get; set; } = true;
        public float Speed { get; set; } = 720f;
    }

    public class GravityConfig
    {
        public KeyBindConfig GravKey { get; set; } = new("Gravity");
        public bool Enabled { get; set; } = true;
        public float Value { get; set; } = 30f;
    }

    public class KnockbackConfig
    {
        public KeyBindConfig KbKey { get; set; } = new("Knockback");
        public bool Enabled { get; set; } = true;
        public float Dampen { get; set; } = 0.2f;
    }

    public class HitfixConfig
    {
        public KeyBindConfig HitFixKey { get; set; } = new("HitFix");
        public bool Enabled { get; set; } = true;
        public float FovRadius { get; set; } = 150f;
        public float Strength { get; set; } = 0.6f;
        public float Range { get; set; } = 300f;
    }

    public class MenuConfig
    {
        public KeyBindConfig MenuKey { get; set; } = new("MenuKey");
    }

    public class GameConfig
    {
        public int Mode { get; set; }
    }

    public class AimAssistConfig
    {
        public KeyBindConfig AssistKey { get; set; } = new("AimAssist");
        public bool Enabled { get; set; } = true;
        public float FovRadius { get; set; } = 60f;
        public float Speed { get; set; } = 0.12f;
        public float Range { get; set; } = 300f;
        public bool OnlyWhenFiring { get; set; }
    }

    public class PerformanceConfig
    {
        public bool FpsCapEnabled { get; set; }
        public int MaxFps { get; set; } = 240;
    }

    public class KeyBindConfig
    {
        public int Key { get; set; }
        public int MouseButton { get; set; } = -1;
        public int ControllerButton { get; set; } = -1;
        public bool Waiting { get; set; }
        public string Label { get; set; } = string.Empty;

        public KeyBindConfig() { }
        public KeyBindConfig(string l) => Label = l ?? "";
    }

    public static class ConfigManager
    {
        private static readonly JsonSerializerOptions options = new()
        {
            WriteIndented = true,
            PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
            DefaultIgnoreCondition = JsonIgnoreCondition.Never
        };

        private static string BasePath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "NyxExternal");
        private static string DefaultFilePath => Path.Combine(BasePath, "default_config.txt");

        public static string GetConfigDirectory()
        {
            string dir = Path.Combine(BasePath, "configs");
            if (!Directory.Exists(dir)) Directory.CreateDirectory(dir);
            return dir;
        }

        public static string GetDefaultConfigName()
        {
            try { return File.Exists(DefaultFilePath) ? File.ReadAllText(DefaultFilePath).Trim() : null; }
            catch { return null; }
        }

        public static bool SetDefaultConfigName(string name)
        {
            try
            {
                if (string.IsNullOrWhiteSpace(name)) { if (File.Exists(DefaultFilePath)) File.Delete(DefaultFilePath); return true; }
                File.WriteAllText(DefaultFilePath, name.Trim());
                return true;
            }
            catch { return false; }
        }

        public static bool SaveConfig(string name)
        {
            try
            {
                var path = Path.Combine(GetConfigDirectory(), name.EndsWith(".cfg") ? name : name + ".cfg");
                File.WriteAllText(path, JsonSerializer.Serialize(Export(), options));
                return true;
            }
            catch { return false; }
        }

        public static bool LoadConfig(string name)
        {
            try
            {
                var path = Path.Combine(GetConfigDirectory(), name.EndsWith(".cfg") ? name : name + ".cfg");
                if (!File.Exists(path)) return false;
                var data = JsonSerializer.Deserialize<ConfigData>(File.ReadAllText(path), options);
                if (data == null) return false;
                Import(data);
                return true;
            }
            catch { return false; }
        }

        public static bool LoadDefaultConfig()
        {
            string def = GetDefaultConfigName();
            return !string.IsNullOrEmpty(def) && LoadConfig(def);
        }

        public static void ResetToDefaults() => Import(new ConfigData());

        public static string[] GetAvailableConfigs()
        {
            try
            {
                var dir = GetConfigDirectory();
                var files = Directory.GetFiles(dir, "*.cfg");
                for (int i = 0; i < files.Length; i++) files[i] = Path.GetFileNameWithoutExtension(files[i]);
                return files;
            }
            catch { return Array.Empty<string>(); }
        }

        public static bool DeleteConfig(string name)
        {
            try
            {
                var path = Path.Combine(GetConfigDirectory(), name.EndsWith(".cfg") ? name : name + ".cfg");
                if (File.Exists(path)) File.Delete(path);
                return true;
            }
            catch { return false; }
        }

        private static ConfigData Export()
        {
            return new ConfigData
            {
                Humanoid = new() { WalkspeedEnabled = Settings.Humanoid.WalkspeedEnabled, JumpPowerEnabled = Settings.Humanoid.JumpPowerEnabled, Walkspeed = Settings.Humanoid.Walkspeed, JumpPower = Settings.Humanoid.JumpPower },
                Camera = new() { FOVEnabled = Settings.Camera.FOVEnabled, FOV = Settings.Camera.FOV },
                Visuals = new()
                {
                    BoxESP = Settings.Visuals.BoxESP,
                    FilledBox = Settings.Visuals.FilledBox,
                    Box = Settings.Visuals.Box,
                    BoxFill = Settings.Visuals.BoxFill,
                    Tracers = Settings.Visuals.Tracers,
                    Skeleton = Settings.Visuals.Skeleton,
                    Name = Settings.Visuals.Name,
                    Distance = Settings.Visuals.Distance,
                    Health = Settings.Visuals.Health,
                    ESP3D = Settings.Visuals.ESP3D,
                    HeadCircle = Settings.Visuals.HeadCircle,
                    CornerESP = Settings.Visuals.CornerESP,
                    RemoveBorders = Settings.Visuals.RemoveBorders,
                    ChinaHat = Settings.Visuals.ChinaHat,
                    LocalPlayerESP = Settings.Visuals.LocalPlayerESP,
                    WeaponESP = Settings.Visuals.WeaponESP,
                    TeamColorESP = Settings.Visuals.TeamColorESP,
                    ObjectESP = Settings.Visuals.ObjectESP,
                    TracersStart = Settings.Visuals.TracersStart,
                    NameSize = Settings.Visuals.NameSize,
                    DistanceSize = Settings.Visuals.DistanceSize,
                    TracerThickness = Settings.Visuals.TracerThickness,
                    HeadCircleMaxScale = Settings.Visuals.HeadCircleMaxScale
                },
                Aiming = new()
                {
                    AimbotKey = new() { Key = Settings.Aiming.AimbotKey.Key, MouseButton = Settings.Aiming.AimbotKey.MouseButton, ControllerButton = Settings.Aiming.AimbotKey.ControllerButton, Waiting = Settings.Aiming.AimbotKey.Waiting, Label = Settings.Aiming.AimbotKey.Label },
                    AimingType = Settings.Aiming.AimingType,
                    ToggleType = Settings.Aiming.ToggleType,
                    Aimbot = Settings.Aiming.Aimbot,
                    StickyAim = Settings.Aiming.StickyAim,
                    Sensitivity = Settings.Aiming.Sensitivity,
                    Smoothness = Settings.Aiming.Smoothness,
                    SmoothnessX = Settings.Aiming.SmoothnessX,
                    SmoothnessY = Settings.Aiming.SmoothnessY,
                    Prediction = Settings.Aiming.Prediction,
                    PredictionY = Settings.Aiming.PredictionY,
                    PredictionX = Settings.Aiming.PredictionX,
                    FOV = Settings.Aiming.FOV,
                    ShowFOV = Settings.Aiming.ShowFOV,
                    FillFOV = Settings.Aiming.FillFOV,
                    AnimatedFOV = Settings.Aiming.AnimatedFOV,
                    Range = Settings.Aiming.Range,
                    TargetBone = Settings.Aiming.TargetBone,
                    Shake = Settings.Aiming.Shake,
                    ShakeDistance = Settings.Aiming.ShakeDistance,
                    ShakeAmount = Settings.Aiming.ShakeAmount
                },
                Silent = new()
                {
                    SilentAimbotKey = new() { Key = Settings.Silent.SilentAimbotKey.Key, MouseButton = Settings.Silent.SilentAimbotKey.MouseButton, ControllerButton = Settings.Silent.SilentAimbotKey.ControllerButton, Waiting = Settings.Silent.SilentAimbotKey.Waiting, Label = Settings.Silent.SilentAimbotKey.Label },
                    SilentAimbot = Settings.Silent.SilentAimbot,
                    AlwaysOn = Settings.Silent.AlwaysOn,
                    SilentVisualizer = Settings.Silent.SilentVisualizer,
                    ShowSilentFOV = Settings.Silent.ShowSilentFOV,
                    SPrediction = Settings.Silent.SPrediction,
                    SilentFOV = Settings.Silent.SilentFOV,
                    PredictionY = Settings.Silent.PredictionY,
                    PredictionX = Settings.Silent.PredictionX,
                    SFOV = Settings.Silent.SFOV
                },
                Checks = new() { TeamCheck = Settings.Checks.TeamCheck, DownedCheck = Settings.Checks.DownedCheck, TransparencyCheck = Settings.Checks.TransparencyCheck, WallCheck = Settings.Checks.WallCheck },
                Network = new()
                {
                    DeSyncBind = new() { Key = Settings.Network.DeSyncBind.Key, MouseButton = Settings.Network.DeSyncBind.MouseButton, ControllerButton = Settings.Network.DeSyncBind.ControllerButton, Waiting = Settings.Network.DeSyncBind.Waiting, Label = Settings.Network.DeSyncBind.Label },
                    DeSync = Settings.Network.DeSync,
                    DeSyncVisualizer = Settings.Network.DeSyncVisualizer
                },
                Flight = new()
                {
                    VFlightBind = new() { Key = Settings.Flight.VFlightBind.Key, MouseButton = Settings.Flight.VFlightBind.MouseButton, ControllerButton = Settings.Flight.VFlightBind.ControllerButton, Waiting = Settings.Flight.VFlightBind.Waiting, Label = Settings.Flight.VFlightBind.Label },
                    VFlight = Settings.Flight.VFlight,
                    VFlightSpeed = Settings.Flight.VFlightSpeed
                },
                Triggerbot = new()
                {
                    TriggerKey = new() { Key = Settings.Triggerbot.TriggerKey.Key, MouseButton = Settings.Triggerbot.TriggerKey.MouseButton, ControllerButton = Settings.Triggerbot.TriggerKey.ControllerButton, Waiting = Settings.Triggerbot.TriggerKey.Waiting, Label = Settings.Triggerbot.TriggerKey.Label },
                    Enabled = Settings.Triggerbot.Enabled,
                    FovRadius = Settings.Triggerbot.FovRadius,
                    DelayMs = Settings.Triggerbot.DelayMs,
                    Range = Settings.Triggerbot.Range
                },
                Hitbox = new()
                {
                    HitboxKey = new() { Key = Settings.Hitbox.HitboxKey.Key, MouseButton = Settings.Hitbox.HitboxKey.MouseButton, ControllerButton = Settings.Hitbox.HitboxKey.ControllerButton, Waiting = Settings.Hitbox.HitboxKey.Waiting, Label = Settings.Hitbox.HitboxKey.Label },
                    Enabled = Settings.Hitbox.Enabled,
                    Multiplier = Settings.Hitbox.Multiplier,
                    Range = Settings.Hitbox.Range
                },
                Spinbot = new()
                {
                    SpinKey = new() { Key = Settings.Spinbot.SpinKey.Key, MouseButton = Settings.Spinbot.SpinKey.MouseButton, ControllerButton = Settings.Spinbot.SpinKey.ControllerButton, Waiting = Settings.Spinbot.SpinKey.Waiting, Label = Settings.Spinbot.SpinKey.Label },
                    Enabled = Settings.Spinbot.Enabled,
                    Speed = Settings.Spinbot.Speed
                },
                Gravity = new()
                {
                    GravKey = new() { Key = Settings.Gravity.GravKey.Key, MouseButton = Settings.Gravity.GravKey.MouseButton, ControllerButton = Settings.Gravity.GravKey.ControllerButton, Waiting = Settings.Gravity.GravKey.Waiting, Label = Settings.Gravity.GravKey.Label },
                    Enabled = Settings.Gravity.Enabled,
                    Value = Settings.Gravity.Value
                },
                Knockback = new()
                {
                    KbKey = new() { Key = Settings.Knockback.KbKey.Key, MouseButton = Settings.Knockback.KbKey.MouseButton, ControllerButton = Settings.Knockback.KbKey.ControllerButton, Waiting = Settings.Knockback.KbKey.Waiting, Label = Settings.Knockback.KbKey.Label },
                    Enabled = Settings.Knockback.Enabled,
                    Dampen = Settings.Knockback.Dampen
                },
                Hitfix = new()
                {
                    HitFixKey = new() { Key = Settings.Hitfix.HitFixKey.Key, MouseButton = Settings.Hitfix.HitFixKey.MouseButton, ControllerButton = Settings.Hitfix.HitFixKey.ControllerButton, Waiting = Settings.Hitfix.HitFixKey.Waiting, Label = Settings.Hitfix.HitFixKey.Label },
                    Enabled = Settings.Hitfix.Enabled,
                    FovRadius = Settings.Hitfix.FovRadius,
                    Strength = Settings.Hitfix.Strength,
                    Range = Settings.Hitfix.Range
                },
                Menu = new()
                {
                    MenuKey = new() { Key = Settings.Menu.MenuKey.Key, MouseButton = Settings.Menu.MenuKey.MouseButton, ControllerButton = Settings.Menu.MenuKey.ControllerButton, Waiting = Settings.Menu.MenuKey.Waiting, Label = Settings.Menu.MenuKey.Label }
                },
                Game = new() { Mode = Settings.Game.Mode },
                AimAssist = new()
                {
                    AssistKey = new() { Key = Settings.AimAssist.AssistKey.Key, MouseButton = Settings.AimAssist.AssistKey.MouseButton, ControllerButton = Settings.AimAssist.AssistKey.ControllerButton, Waiting = Settings.AimAssist.AssistKey.Waiting, Label = Settings.AimAssist.AssistKey.Label },
                    Enabled = Settings.AimAssist.Enabled,
                    FovRadius = Settings.AimAssist.FovRadius,
                    Speed = Settings.AimAssist.Speed,
                    Range = Settings.AimAssist.Range,
                    OnlyWhenFiring = Settings.AimAssist.OnlyWhenFiring
                },
                Performance = new() { FpsCapEnabled = Settings.Performance.FpsCapEnabled, MaxFps = Settings.Performance.MaxFps }
            };
        }

        private static void Import(ConfigData c)
        {
            Settings.Humanoid.WalkspeedEnabled = c.Humanoid.WalkspeedEnabled;
            Settings.Humanoid.JumpPowerEnabled = c.Humanoid.JumpPowerEnabled;
            Settings.Humanoid.Walkspeed = c.Humanoid.Walkspeed;
            Settings.Humanoid.JumpPower = c.Humanoid.JumpPower;

            Settings.Camera.FOVEnabled = c.Camera.FOVEnabled;
            Settings.Camera.FOV = c.Camera.FOV;

            Settings.Visuals.BoxESP = c.Visuals.BoxESP;
            Settings.Visuals.FilledBox = c.Visuals.FilledBox;
            Settings.Visuals.Box = c.Visuals.Box;
            Settings.Visuals.BoxFill = c.Visuals.BoxFill;
            Settings.Visuals.Tracers = c.Visuals.Tracers;
            Settings.Visuals.Skeleton = c.Visuals.Skeleton;
            Settings.Visuals.Name = c.Visuals.Name;
            Settings.Visuals.Distance = c.Visuals.Distance;
            Settings.Visuals.Health = c.Visuals.Health;
            Settings.Visuals.ESP3D = c.Visuals.ESP3D;
            Settings.Visuals.HeadCircle = c.Visuals.HeadCircle;
            Settings.Visuals.CornerESP = c.Visuals.CornerESP;
            Settings.Visuals.RemoveBorders = c.Visuals.RemoveBorders;
            Settings.Visuals.ChinaHat = c.Visuals.ChinaHat;
            Settings.Visuals.LocalPlayerESP = c.Visuals.LocalPlayerESP;
            Settings.Visuals.WeaponESP = c.Visuals.WeaponESP;
            Settings.Visuals.TeamColorESP = c.Visuals.TeamColorESP;
            Settings.Visuals.ObjectESP = c.Visuals.ObjectESP;
            Settings.Visuals.TracersStart = c.Visuals.TracersStart;
            Settings.Visuals.NameSize = c.Visuals.NameSize;
            Settings.Visuals.DistanceSize = c.Visuals.DistanceSize;
            Settings.Visuals.TracerThickness = c.Visuals.TracerThickness;
            Settings.Visuals.HeadCircleMaxScale = c.Visuals.HeadCircleMaxScale;

            Settings.Aiming.AimbotKey.Key = c.Aiming.AimbotKey.Key;
            Settings.Aiming.AimbotKey.MouseButton = c.Aiming.AimbotKey.MouseButton;
            Settings.Aiming.AimbotKey.ControllerButton = c.Aiming.AimbotKey.ControllerButton;
            Settings.Aiming.AimbotKey.Label = c.Aiming.AimbotKey.Label;
            Settings.Aiming.AimingType = c.Aiming.AimingType;
            Settings.Aiming.ToggleType = c.Aiming.ToggleType;
            Settings.Aiming.Aimbot = c.Aiming.Aimbot;
            Settings.Aiming.StickyAim = c.Aiming.StickyAim;
            Settings.Aiming.Sensitivity = c.Aiming.Sensitivity;
            Settings.Aiming.Smoothness = c.Aiming.Smoothness;
            Settings.Aiming.SmoothnessX = c.Aiming.SmoothnessX;
            Settings.Aiming.SmoothnessY = c.Aiming.SmoothnessY;
            Settings.Aiming.Prediction = c.Aiming.Prediction;
            Settings.Aiming.PredictionY = c.Aiming.PredictionY;
            Settings.Aiming.PredictionX = c.Aiming.PredictionX;
            Settings.Aiming.FOV = c.Aiming.FOV;
            Settings.Aiming.ShowFOV = c.Aiming.ShowFOV;
            Settings.Aiming.FillFOV = c.Aiming.FillFOV;
            Settings.Aiming.AnimatedFOV = c.Aiming.AnimatedFOV;
            Settings.Aiming.Range = c.Aiming.Range;
            Settings.Aiming.TargetBone = c.Aiming.TargetBone;
            Settings.Aiming.Shake = c.Aiming.Shake;
            Settings.Aiming.ShakeDistance = c.Aiming.ShakeDistance;
            Settings.Aiming.ShakeAmount = c.Aiming.ShakeAmount;

            Settings.Silent.SilentAimbotKey.Key = c.Silent.SilentAimbotKey.Key;
            Settings.Silent.SilentAimbotKey.MouseButton = c.Silent.SilentAimbotKey.MouseButton;
            Settings.Silent.SilentAimbotKey.ControllerButton = c.Silent.SilentAimbotKey.ControllerButton;
            Settings.Silent.SilentAimbotKey.Label = c.Silent.SilentAimbotKey.Label;
            Settings.Silent.SilentAimbot = c.Silent.SilentAimbot;
            Settings.Silent.AlwaysOn = c.Silent.AlwaysOn;
            Settings.Silent.SilentVisualizer = c.Silent.SilentVisualizer;
            Settings.Silent.ShowSilentFOV = c.Silent.ShowSilentFOV;
            Settings.Silent.SPrediction = c.Silent.SPrediction;
            Settings.Silent.SilentFOV = c.Silent.SilentFOV;
            Settings.Silent.PredictionY = c.Silent.PredictionY;
            Settings.Silent.PredictionX = c.Silent.PredictionX;
            Settings.Silent.SFOV = c.Silent.SFOV;

            Settings.Checks.TeamCheck = c.Checks.TeamCheck;
            Settings.Checks.DownedCheck = c.Checks.DownedCheck;
            Settings.Checks.TransparencyCheck = c.Checks.TransparencyCheck;
            Settings.Checks.WallCheck = c.Checks.WallCheck;

            Settings.Network.DeSyncBind.Key = c.Network.DeSyncBind.Key;
            Settings.Network.DeSyncBind.MouseButton = c.Network.DeSyncBind.MouseButton;
            Settings.Network.DeSyncBind.ControllerButton = c.Network.DeSyncBind.ControllerButton;
            Settings.Network.DeSyncBind.Label = c.Network.DeSyncBind.Label;
            Settings.Network.DeSync = c.Network.DeSync;
            Settings.Network.DeSyncVisualizer = c.Network.DeSyncVisualizer;

            Settings.Flight.VFlightBind.Key = c.Flight.VFlightBind.Key;
            Settings.Flight.VFlightBind.MouseButton = c.Flight.VFlightBind.MouseButton;
            Settings.Flight.VFlightBind.ControllerButton = c.Flight.VFlightBind.ControllerButton;
            Settings.Flight.VFlightBind.Label = c.Flight.VFlightBind.Label;
            Settings.Flight.VFlight = c.Flight.VFlight;
            Settings.Flight.VFlightSpeed = c.Flight.VFlightSpeed;

            Settings.Triggerbot.TriggerKey.Key = c.Triggerbot.TriggerKey.Key;
            Settings.Triggerbot.TriggerKey.MouseButton = c.Triggerbot.TriggerKey.MouseButton;
            Settings.Triggerbot.TriggerKey.ControllerButton = c.Triggerbot.TriggerKey.ControllerButton;
            Settings.Triggerbot.TriggerKey.Label = c.Triggerbot.TriggerKey.Label;
            Settings.Triggerbot.Enabled = c.Triggerbot.Enabled;
            Settings.Triggerbot.FovRadius = c.Triggerbot.FovRadius;
            Settings.Triggerbot.DelayMs = c.Triggerbot.DelayMs;
            Settings.Triggerbot.Range = c.Triggerbot.Range;

            Settings.Hitbox.HitboxKey.Key = c.Hitbox.HitboxKey.Key;
            Settings.Hitbox.HitboxKey.MouseButton = c.Hitbox.HitboxKey.MouseButton;
            Settings.Hitbox.HitboxKey.ControllerButton = c.Hitbox.HitboxKey.ControllerButton;
            Settings.Hitbox.HitboxKey.Label = c.Hitbox.HitboxKey.Label;
            Settings.Hitbox.Enabled = c.Hitbox.Enabled;
            Settings.Hitbox.Multiplier = c.Hitbox.Multiplier;
            Settings.Hitbox.Range = c.Hitbox.Range;

            Settings.Spinbot.SpinKey.Key = c.Spinbot.SpinKey.Key;
            Settings.Spinbot.SpinKey.MouseButton = c.Spinbot.SpinKey.MouseButton;
            Settings.Spinbot.SpinKey.ControllerButton = c.Spinbot.SpinKey.ControllerButton;
            Settings.Spinbot.SpinKey.Label = c.Spinbot.SpinKey.Label;
            Settings.Spinbot.Enabled = c.Spinbot.Enabled;
            Settings.Spinbot.Speed = c.Spinbot.Speed;

            Settings.Gravity.GravKey.Key = c.Gravity.GravKey.Key;
            Settings.Gravity.GravKey.MouseButton = c.Gravity.GravKey.MouseButton;
            Settings.Gravity.GravKey.ControllerButton = c.Gravity.GravKey.ControllerButton;
            Settings.Gravity.GravKey.Label = c.Gravity.GravKey.Label;
            Settings.Gravity.Enabled = c.Gravity.Enabled;
            Settings.Gravity.Value = c.Gravity.Value;

            Settings.Knockback.KbKey.Key = c.Knockback.KbKey.Key;
            Settings.Knockback.KbKey.MouseButton = c.Knockback.KbKey.MouseButton;
            Settings.Knockback.KbKey.ControllerButton = c.Knockback.KbKey.ControllerButton;
            Settings.Knockback.KbKey.Label = c.Knockback.KbKey.Label;
            Settings.Knockback.Enabled = c.Knockback.Enabled;
            Settings.Knockback.Dampen = c.Knockback.Dampen;

            Settings.Hitfix.HitFixKey.Key = c.Hitfix.HitFixKey.Key;
            Settings.Hitfix.HitFixKey.MouseButton = c.Hitfix.HitFixKey.MouseButton;
            Settings.Hitfix.HitFixKey.ControllerButton = c.Hitfix.HitFixKey.ControllerButton;
            Settings.Hitfix.HitFixKey.Label = c.Hitfix.HitFixKey.Label;
            Settings.Hitfix.Enabled = c.Hitfix.Enabled;
            Settings.Hitfix.FovRadius = c.Hitfix.FovRadius;
            Settings.Hitfix.Strength = c.Hitfix.Strength;
            Settings.Hitfix.Range = c.Hitfix.Range;

            Settings.Menu.MenuKey.Key = c.Menu.MenuKey.Key;
            Settings.Menu.MenuKey.MouseButton = c.Menu.MenuKey.MouseButton;
            Settings.Menu.MenuKey.ControllerButton = c.Menu.MenuKey.ControllerButton;
            Settings.Menu.MenuKey.Label = c.Menu.MenuKey.Label;

            Settings.Game.Mode = c.Game.Mode;

            Settings.AimAssist.AssistKey.Key = c.AimAssist.AssistKey.Key;
            Settings.AimAssist.AssistKey.MouseButton = c.AimAssist.AssistKey.MouseButton;
            Settings.AimAssist.AssistKey.ControllerButton = c.AimAssist.AssistKey.ControllerButton;
            Settings.AimAssist.AssistKey.Label = c.AimAssist.AssistKey.Label;
            Settings.AimAssist.Enabled = c.AimAssist.Enabled;
            Settings.AimAssist.FovRadius = c.AimAssist.FovRadius;
            Settings.AimAssist.Speed = c.AimAssist.Speed;
            Settings.AimAssist.Range = c.AimAssist.Range;
            Settings.AimAssist.OnlyWhenFiring = c.AimAssist.OnlyWhenFiring;

            Settings.Performance.FpsCapEnabled = c.Performance.FpsCapEnabled;
            Settings.Performance.MaxFps = c.Performance.MaxFps;
        }
    }
}
