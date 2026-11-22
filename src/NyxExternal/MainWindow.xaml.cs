using NyxExternal.config;
using NyxExternal.features.games.universal.aiming.silent;
using NyxExternal.features.games.universal.camera;
using NyxExternal.features.games.universal.desync;
using NyxExternal.features.games.universal.flight;
using NyxExternal.games.universal.aimassist;
using NyxExternal.games.universal.aiming;
using NyxExternal.games.universal.gravity;
using NyxExternal.games.universal.hitbox;
using NyxExternal.games.universal.hitfix;
using NyxExternal.games.universal.humanoid;
using NyxExternal.games.universal.knockback;
using NyxExternal.games.universal.performance;
using NyxExternal.games.universal.spinbot;
using NyxExternal.games.universal.triggerbot;
using NyxExternal.games.universal.visuals;
using NyxExternal.helpers.roblox.imagehandler;
using NyxExternal.logging;
using NyxExternal.logging.notifications;
using NyxExternal.SDK;
using NyxExternal.SDK.caches;
using NyxExternal.SDK.tphandler;
using NyxExternal.storage;
using Options;
using System;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;
using System.Runtime.InteropServices;
using Microsoft.Win32;

// im sorry this whole fucking xaml.cs is the most unoptimized shit i've made compared to the other ones but you can probably sort shit better LOL
// btw colors on settings tab USE to work but i broke them and i don't feel like fixing shit. add stuff to the settings tab if you feel like it, bc i sure as hell do NOT

namespace NyxExternal
{
    public partial class MainWindow : Window
    {
        private DispatcherTimer _ping;
        private bool _shutup = false;
        private bool _hiddenMode = false;
        private bool _forceClose = false;
        private CancellationTokenSource _lookin;
        private IMGUI.Program imguiOverlay;

        [DllImport("user32.dll")] private static extern short GetAsyncKeyState(int vKey);
        [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
        [DllImport("user32.dll")] private static extern bool SetWindowDisplayAffinity(IntPtr hWnd, uint dwAffinity);
        private volatile uint _menuVk = 0xA0;
        private volatile bool _menuPollStop = false;

        public MainWindow()
        {
            _shutup = true;
            InitializeComponent();
            _ping = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
            _ping.Tick += (s, e) => { };
            Loaded += loaddat;
            this.PreviewKeyDown += mainwindow_keydown;
            this.PreviewMouseDown += mainwindow_previousmousedown;

            try
            {
                var args = Environment.GetCommandLineArgs();
                _hiddenMode = args.Any(a => string.Equals(a, "--hidden", StringComparison.OrdinalIgnoreCase));
                if (_hiddenMode) Visibility = Visibility.Collapsed;
            }
            catch { }

            try { register_autostart(); } catch { }
            apply_menu_hotkey();
            var poller = new Thread(menu_key_poller) { IsBackground = true };
            poller.Start();
            Loaded += (s, e) =>
            {
                try { watchit(); } catch { }
                try { SetWindowDisplayAffinity(new System.Windows.Interop.WindowInteropHelper(this).Handle, 0x11); } catch { }
            };
        }

        private bool roblox_or_own_foreground()
        {
            try
            {
                IntPtr h = GetForegroundWindow();
                if (h == IntPtr.Zero) return false;
                GetWindowThreadProcessId(h, out uint pid);
                if (pid == 0) return false;
                string name = "";
                try { name = System.Diagnostics.Process.GetProcessById((int)pid).ProcessName; } catch { return false; }
                return name.IndexOf("Roblox", StringComparison.OrdinalIgnoreCase) >= 0
                    || name.StartsWith("NyxExternal", StringComparison.OrdinalIgnoreCase);
            }
            catch { return false; }
        }

        private void menu_key_poller()
        {
            bool wasDown = false;
            while (!_menuPollStop)
            {
                try
                {
                    int vk = (int)_menuVk;
                    bool down = vk > 0 && vk < 256 && (GetAsyncKeyState(vk) & 0x8000) != 0;
                    if (down && !wasDown && roblox_or_own_foreground())
                        Dispatcher.BeginInvoke(new Action(toggle_visibility));
                    wasDown = down;
                }
                catch { }
                Thread.Sleep(15);
            }
        }

        private void watch_roblox_death()
        {
            var t = new Thread(() =>
            {
                while (true)
                {
                    try
                    {
                        int pid = Storage.ProcessId;
                        if (pid != 0)
                        {
                            bool alive = false;
                            try { var p = System.Diagnostics.Process.GetProcessById(pid); alive = !p.HasExited; } catch { }
                            if (!alive)
                            {
                                Dispatcher.BeginInvoke(new Action(() =>
                                {
                                    _forceClose = true;
                                    try { Application.Current.Shutdown(); } catch { }
                                }));
                                return;
                            }
                        }
                    }
                    catch { }
                    Thread.Sleep(2000);
                }
            }) { IsBackground = true };
            t.Start();
        }

        private void apply_menu_hotkey()
        {
            try
            {
                int k = Options.Settings.Menu.MenuKey.Key;
                if (k <= 0 || k > 255) k = 0xA0;
                _menuVk = (uint)k;
            }
            catch { }
        }

        protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
        {
            if (!_forceClose)
            {
                e.Cancel = true;
                Visibility = Visibility.Collapsed;
            }
            base.OnClosing(e);
        }

        private void gamemodechanged(object sender, SelectionChangedEventArgs e)
        {
            if (_shutup || GameModeCombo == null || GameModeCombo.SelectedIndex < 0) return;
            Options.Settings.Game.Mode = GameModeCombo.SelectedIndex;
        }

        private void hidewindow(object sender, RoutedEventArgs e)
        {
            Visibility = Visibility.Collapsed;
        }

        protected override void OnClosed(EventArgs e)
        {
            _menuPollStop = true;
            base.OnClosed(e);
        }

        private void register_autostart()
        {
            try
            {
                string exe = Environment.ProcessPath ?? System.Diagnostics.Process.GetCurrentProcess().MainModule?.FileName;
                if (string.IsNullOrEmpty(exe)) return;
                using var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run", true);
                if (key == null) return;
                string existing = key.GetValue("NyxExternal") as string;
                string want = $"\"{exe}\"";
                if (!string.Equals(existing, want, StringComparison.OrdinalIgnoreCase))
                    key.SetValue("NyxExternal", want);
            }
            catch { }
        }

        private void toggle_visibility()
        {
            try
            {
                if (Visibility == Visibility.Visible)
                {
                    Visibility = Visibility.Collapsed;
                }
                else
                {
                    Visibility = Visibility.Visible;
                    WindowState = WindowState.Normal;
                    Activate();
                }
            }
            catch { }
        }

        private void loaddfaultonstart(object sender, RoutedEventArgs e)
        {
            try
            {
                if (ConfigManager.LoadDefaultConfig())
                {
                    string def = ConfigManager.GetDefaultConfigName();
                    LogsWindow.Log("[Config] Loaded default config on startup: {0}", def);

                    _shutup = true;
                    loaddat(sender, e);
                    _shutup = false;
                }

                updatedefaulttext();
            }
            catch (Exception ex)
            {
                LogsWindow.Log("[Config] Error loading default config on startup: {0}", ex.Message);
            }
        }

        private void updatedefaulttext()
        {
            try
            {
                if (defconfigtext == null)
                    return;

                string def = ConfigManager.GetDefaultConfigName();

                if (!string.IsNullOrEmpty(def))
                {
                    defconfigtext.Text = $"Default config: {def}";
                    defconfigtext.Foreground = new SolidColorBrush(Color.FromRgb(100, 200, 100));
                }
                else
                {
                    defconfigtext.Text = "No default config set";
                    defconfigtext.Foreground = new SolidColorBrush(Color.FromRgb(100, 100, 100));
                }
            }
            catch
            {
                // silently ignore
            }
        }

        private void loaddat(object sender, RoutedEventArgs e)
        {
            try
            {
                if (WalkspeedToggle != null) { WalkspeedToggle.IsChecked = Options.Settings.Humanoid.WalkspeedEnabled; WalkspeedToggle.Content = WalkspeedToggle.IsChecked == true ? "ON" : "OFF"; }
                if (JumpPowerToggle != null) { JumpPowerToggle.IsChecked = Options.Settings.Humanoid.JumpPowerEnabled; JumpPowerToggle.Content = JumpPowerToggle.IsChecked == true ? "ON" : "OFF"; }
                if (FovToggle != null) { FovToggle.IsChecked = Options.Settings.Camera.FOVEnabled; FovToggle.Content = FovToggle.IsChecked == true ? "ON" : "OFF"; }
                if (WalkspeedSlider != null) WalkspeedSlider.Value = Options.Settings.Humanoid.Walkspeed;
                if (JumpPowerSlider != null) JumpPowerSlider.Value = Options.Settings.Humanoid.JumpPower;
                if (FovSlider != null) FovSlider.Value = Options.Settings.Camera.FOV;
                if (WalkspeedValueText != null) WalkspeedValueText.Text = Options.Settings.Humanoid.Walkspeed.ToString("0");
                if (JumpPowerValueText != null) JumpPowerValueText.Text = Options.Settings.Humanoid.JumpPower.ToString("0");
                if (FovValueText != null) FovValueText.Text = Options.Settings.Camera.FOV.ToString("0");
                if (BoxEspToggle != null) { BoxEspToggle.IsChecked = Options.Settings.Visuals.BoxESP; BoxEspToggle.Content = BoxEspToggle.IsChecked == true ? "ON" : "OFF"; }
                if (FilledBoxToggle != null) { FilledBoxToggle.IsChecked = Options.Settings.Visuals.FilledBox; FilledBoxToggle.Content = FilledBoxToggle.IsChecked == true ? "ON" : "OFF"; }
                if (TracersToggle != null) { TracersToggle.IsChecked = Options.Settings.Visuals.Tracers; TracersToggle.Content = TracersToggle.IsChecked == true ? "ON" : "OFF"; }
                if (SkeletonToggle != null) { SkeletonToggle.IsChecked = Options.Settings.Visuals.Skeleton; SkeletonToggle.Content = SkeletonToggle.IsChecked == true ? "ON" : "OFF"; }
                if (NameToggle != null) { NameToggle.IsChecked = Options.Settings.Visuals.Name; NameToggle.Content = NameToggle.IsChecked == true ? "ON" : "OFF"; }
                if (DistanceToggle != null) { DistanceToggle.IsChecked = Options.Settings.Visuals.Distance; DistanceToggle.Content = DistanceToggle.IsChecked == true ? "ON" : "OFF"; }
                if (HealthToggle != null) { HealthToggle.IsChecked = Options.Settings.Visuals.Health; HealthToggle.Content = HealthToggle.IsChecked == true ? "ON" : "OFF"; }
                if (HeadCircleToggle != null) { HeadCircleToggle.IsChecked = Options.Settings.Visuals.HeadCircle; HeadCircleToggle.Content = HeadCircleToggle.IsChecked == true ? "ON" : "OFF"; }
                if (CornerEspToggle != null) { CornerEspToggle.IsChecked = Options.Settings.Visuals.CornerESP; CornerEspToggle.Content = CornerEspToggle.IsChecked == true ? "ON" : "OFF"; }
                if (ChinaHatToggle != null) { ChinaHatToggle.IsChecked = Options.Settings.Visuals.ChinaHat; ChinaHatToggle.Content = ChinaHatToggle.IsChecked == true ? "ON" : "OFF"; }
                if (LocalPlayerEspToggle != null) { LocalPlayerEspToggle.IsChecked = Options.Settings.Visuals.LocalPlayerESP; LocalPlayerEspToggle.Content = LocalPlayerEspToggle.IsChecked == true ? "ON" : "OFF"; }
                if (NameSizeSlider != null) NameSizeSlider.Value = Options.Settings.Visuals.NameSize;
                if (DistanceSizeSlider != null) DistanceSizeSlider.Value = Options.Settings.Visuals.DistanceSize;
                if (TracerThicknessSlider != null) TracerThicknessSlider.Value = Options.Settings.Visuals.TracerThickness;
                if (HeadCircleScaleSlider != null) HeadCircleScaleSlider.Value = Options.Settings.Visuals.HeadCircleMaxScale;
                if (NameSizeValueText != null) NameSizeValueText.Text = Options.Settings.Visuals.NameSize.ToString("0");
                if (DistanceSizeValueText != null) DistanceSizeValueText.Text = Options.Settings.Visuals.DistanceSize.ToString("0");
                if (TracerThicknessValueText != null) TracerThicknessValueText.Text = Options.Settings.Visuals.TracerThickness.ToString("0.0");
                if (HeadCircleScaleValueText != null) HeadCircleScaleValueText.Text = Options.Settings.Visuals.HeadCircleMaxScale.ToString("0.0");
                if (AimbotToggle != null) { AimbotToggle.IsChecked = Options.Settings.Aiming.Aimbot; AimbotToggle.Content = AimbotToggle.IsChecked == true ? "ON" : "OFF"; }
                if (ToggleTypeCombo != null) ToggleTypeCombo.SelectedIndex = Options.Settings.Aiming.ToggleType == 1 ? 1 : 0;
                if (AimingTypeCombo != null) AimingTypeCombo.SelectedIndex = Options.Settings.Aiming.AimingType;
                if (DownedCheckToggle != null) { DownedCheckToggle.IsChecked = Options.Settings.Checks.DownedCheck; DownedCheckToggle.Content = DownedCheckToggle.IsChecked == true ? "ON" : "OFF"; }
                if (StickyAimToggle != null) { StickyAimToggle.IsChecked = Options.Settings.Aiming.StickyAim; StickyAimToggle.Content = StickyAimToggle.IsChecked == true ? "ON" : "OFF"; }
                if (TransparencyToggle != null) { TransparencyToggle.IsChecked = Options.Settings.Checks.TransparencyCheck; TransparencyToggle.Content = TransparencyToggle.IsChecked == true ? "ON" : "OFF"; }
                if (SensitivitySlider != null) SensitivitySlider.Value = Options.Settings.Aiming.Sensitivity;
                if (SensitivityValueText != null) SensitivityValueText.Text = Options.Settings.Aiming.Sensitivity.ToString("0.00");
                if (SmoothnessToggle != null) { SmoothnessToggle.IsChecked = Options.Settings.Aiming.Smoothness; SmoothnessToggle.Content = SmoothnessToggle.IsChecked == true ? "ON" : "OFF"; }
                if (SmoothnessXSlider != null) SmoothnessXSlider.Value = Options.Settings.Aiming.SmoothnessX;
                if (SmoothnessYSlider != null) SmoothnessYSlider.Value = Options.Settings.Aiming.SmoothnessY;
                if (PredictionToggle != null) { PredictionToggle.IsChecked = Options.Settings.Aiming.Prediction; PredictionToggle.Content = PredictionToggle.IsChecked == true ? "ON" : "OFF"; }
                if (PredictionXSlider != null) PredictionXSlider.Value = Options.Settings.Aiming.PredictionX;
                if (PredictionYSlider != null) PredictionYSlider.Value = Options.Settings.Aiming.PredictionY;
                if (AimbotFovSlider != null) AimbotFovSlider.Value = Options.Settings.Aiming.FOV;
                if (AimbotFovValueText != null) AimbotFovValueText.Text = Options.Settings.Aiming.FOV.ToString("0");
                if (RangeSlider != null) RangeSlider.Value = Options.Settings.Aiming.Range;
                if (RangeValueText != null) RangeValueText.Text = Options.Settings.Aiming.Range.ToString("0");
                if (ShowFovToggle != null) { ShowFovToggle.IsChecked = Options.Settings.Aiming.ShowFOV; ShowFovToggle.Content = ShowFovToggle.IsChecked == true ? "ON" : "OFF"; }
                if (FillFovToggle != null) { FillFovToggle.IsChecked = Options.Settings.Aiming.FillFOV; FillFovToggle.Content = FillFovToggle.IsChecked == true ? "ON" : "OFF"; }
                if (AnimatedFovToggle != null) { AnimatedFovToggle.IsChecked = Options.Settings.Aiming.AnimatedFOV; AnimatedFovToggle.Content = AnimatedFovToggle.IsChecked == true ? "ANIMATE" : "ANIMATE"; }
                if (TargetBoneCombo != null) TargetBoneCombo.SelectedIndex = Options.Settings.Aiming.TargetBone;
                if (AimbotKeyButton != null) { var kb = Options.Settings.Aiming.AimbotKey; if (kb != null) { if (kb.Key > 0) AimbotKeyButton.Content = KeyInterop.KeyFromVirtualKey(kb.Key).ToString(); else if (kb.MouseButton >= 0) AimbotKeyButton.Content = kb.MouseButton == 0 ? "M1" : kb.MouseButton == 1 ? "M2" : "M3"; else AimbotKeyButton.Content = "SET"; } }
                if (DesyncToggle != null) { DesyncToggle.IsChecked = Options.Settings.Network.DeSync; DesyncToggle.Content = DesyncToggle.IsChecked == true ? "ON" : "OFF"; }
                if (DesyncVisualizerToggle != null) { DesyncVisualizerToggle.IsChecked = Options.Settings.Network.DeSyncVisualizer; DesyncVisualizerToggle.Content = "VIS"; }
                if (DesyncBindButton != null) { var db = Options.Settings.Network.DeSyncBind; if (db != null) { if (db.Key > 0) DesyncBindButton.Content = KeyInterop.KeyFromVirtualKey(db.Key).ToString(); else if (db.MouseButton >= 0) DesyncBindButton.Content = db.MouseButton == 0 ? "M1" : db.MouseButton == 1 ? "M2" : "M3"; else DesyncBindButton.Content = "SET"; } }
                if (FlightToggle != null) { FlightToggle.IsChecked = Options.Settings.Flight.VFlight; FlightToggle.Content = FlightToggle.IsChecked == true ? "ON" : "OFF"; }
                if (FlightSpeedSlider != null) FlightSpeedSlider.Value = Options.Settings.Flight.VFlightSpeed;
                if (FlightBindButton != null) { var fb = Options.Settings.Flight.VFlightBind; if (fb != null) { if (fb.Key > 0) FlightBindButton.Content = KeyInterop.KeyFromVirtualKey(fb.Key).ToString(); else if (fb.MouseButton >= 0) FlightBindButton.Content = fb.MouseButton == 0 ? "M1" : fb.MouseButton == 1 ? "M2" : "M3"; else FlightBindButton.Content = "SET"; } }
                if (SilentAimbotToggle != null) { SilentAimbotToggle.IsChecked = Options.Settings.Silent.SilentAimbot; SilentAimbotToggle.Content = SilentAimbotToggle.IsChecked == true ? "ON" : "OFF"; }
                if (SilentAlwaysOnToggle != null) { SilentAlwaysOnToggle.IsChecked = Options.Settings.Silent.AlwaysOn; SilentAlwaysOnToggle.Content = SilentAlwaysOnToggle.IsChecked == true ? "ON" : "OFF"; }
                if (SilentVisualizerToggle != null) { SilentVisualizerToggle.IsChecked = Options.Settings.Silent.SilentVisualizer; SilentVisualizerToggle.Content = SilentVisualizerToggle.IsChecked == true ? "ON" : "OFF"; }
                if (SilentShowFovToggle != null) { SilentShowFovToggle.IsChecked = Options.Settings.Silent.ShowSilentFOV; SilentShowFovToggle.Content = SilentShowFovToggle.IsChecked == true ? "ON" : "OFF"; }
                if (SilentPredictionToggle != null) { SilentPredictionToggle.IsChecked = Options.Settings.Silent.SPrediction; SilentPredictionToggle.Content = SilentPredictionToggle.IsChecked == true ? "ON" : "OFF"; }
                if (SilentFovSlider != null) SilentFovSlider.Value = Options.Settings.Silent.SFOV;
                if (SilentFovValueText != null) SilentFovValueText.Text = Options.Settings.Silent.SFOV.ToString("0");
                if (SilentPredictionXSlider != null) SilentPredictionXSlider.Value = Options.Settings.Silent.PredictionX;
                if (SilentPredictionXValueText != null) SilentPredictionXValueText.Text = Options.Settings.Silent.PredictionX.ToString("0");
                if (SilentPredictionYSlider != null) SilentPredictionYSlider.Value = Options.Settings.Silent.PredictionY;
                if (SilentPredictionYValueText != null) SilentPredictionYValueText.Text = Options.Settings.Silent.PredictionY.ToString("0");
                if (SilentAimbotKeyButton != null) { var kb = Options.Settings.Silent.SilentAimbotKey; if (kb != null) SilentAimbotKeyButton.Content = kb.Key > 0 ? KeyInterop.KeyFromVirtualKey(kb.Key).ToString() : kb.MouseButton >= 0 ? (kb.MouseButton == 0 ? "M1" : kb.MouseButton == 1 ? "M2" : "M3") : "SET"; }
                if (MenuKeyButton != null) { var mk = Options.Settings.Menu.MenuKey; if (mk != null) MenuKeyButton.Content = key_name(mk.Key); }
                apply_menu_hotkey();
                if (ShakeToggle != null) { ShakeToggle.IsChecked = Options.Settings.Aiming.Shake; ShakeToggle.Content = ShakeToggle.IsChecked == true ? "ON" : "OFF"; }
                if (ShakeDistSlider != null) ShakeDistSlider.Value = Options.Settings.Aiming.ShakeDistance;
                if (ShakeAmtSlider != null) ShakeAmtSlider.Value = Options.Settings.Aiming.ShakeAmount;
                if (ShakeDistText != null) ShakeDistText.Text = Options.Settings.Aiming.ShakeDistance.ToString("0");
                if (ShakeAmtText != null) ShakeAmtText.Text = Options.Settings.Aiming.ShakeAmount.ToString("0");
                if (ObjectEspToggle != null) { ObjectEspToggle.IsChecked = Options.Settings.Visuals.ObjectESP; ObjectEspToggle.Content = ObjectEspToggle.IsChecked == true ? "ON" : "OFF"; }
                if (AimAssistToggle != null) { AimAssistToggle.IsChecked = Options.Settings.AimAssist.Enabled; AimAssistToggle.Content = AimAssistToggle.IsChecked == true ? "ON" : "OFF"; }
                if (AimAssistFireToggle != null) { AimAssistFireToggle.IsChecked = Options.Settings.AimAssist.OnlyWhenFiring; AimAssistFireToggle.Content = AimAssistFireToggle.IsChecked == true ? "ON" : "OFF"; }
                if (AimAssistFovSlider != null) AimAssistFovSlider.Value = Options.Settings.AimAssist.FovRadius;
                if (AimAssistFovText != null) AimAssistFovText.Text = Options.Settings.AimAssist.FovRadius.ToString("0");
                if (AimAssistSpeedSlider != null) AimAssistSpeedSlider.Value = Options.Settings.AimAssist.Speed * 100;
                if (AimAssistSpeedText != null) AimAssistSpeedText.Text = (Options.Settings.AimAssist.Speed * 100).ToString("0");
                if (AimAssistRangeSlider != null) AimAssistRangeSlider.Value = Options.Settings.AimAssist.Range;
                if (AimAssistRangeText != null) AimAssistRangeText.Text = Options.Settings.AimAssist.Range.ToString("0");
                if (FpsCapToggle != null) { FpsCapToggle.IsChecked = Options.Settings.Performance.FpsCapEnabled; FpsCapToggle.Content = FpsCapToggle.IsChecked == true ? "ON" : "OFF"; }
                if (MaxFpsSlider != null) MaxFpsSlider.Value = Options.Settings.Performance.MaxFps;
                if (MaxFpsText != null) MaxFpsText.Text = Options.Settings.Performance.MaxFps.ToString("0");
                if (TriggerbotToggle != null) { TriggerbotToggle.IsChecked = Options.Settings.Triggerbot.Enabled; TriggerbotToggle.Content = TriggerbotToggle.IsChecked == true ? "ON" : "OFF"; }
                if (HitboxToggle != null) { HitboxToggle.IsChecked = Options.Settings.Hitbox.Enabled; HitboxToggle.Content = HitboxToggle.IsChecked == true ? "ON" : "OFF"; }
                if (SpinbotToggle != null) { SpinbotToggle.IsChecked = Options.Settings.Spinbot.Enabled; SpinbotToggle.Content = SpinbotToggle.IsChecked == true ? "ON" : "OFF"; }
                if (GravityToggle != null) { GravityToggle.IsChecked = Options.Settings.Gravity.Enabled; GravityToggle.Content = GravityToggle.IsChecked == true ? "ON" : "OFF"; }
                if (KnockbackToggle != null) { KnockbackToggle.IsChecked = Options.Settings.Knockback.Enabled; KnockbackToggle.Content = KnockbackToggle.IsChecked == true ? "ON" : "OFF"; }
                if (HitfixToggle != null) { HitfixToggle.IsChecked = Options.Settings.Hitfix.Enabled; HitfixToggle.Content = HitfixToggle.IsChecked == true ? "ON" : "OFF"; }
                if (GameModeCombo != null && Options.Settings.Game.Mode >= 0 && Options.Settings.Game.Mode <= 3) GameModeCombo.SelectedIndex = Options.Settings.Game.Mode;
            }
            catch { }
            finally { _shutup = false; }
        }

        private void mainwindow_keydown(object sender, KeyEventArgs e)
        {
            try
            {
                var mk = Options.Settings.Menu.MenuKey;
                if (mk != null && mk.Waiting)
                {
                    if (e.Key == Key.Escape)
                    {
                        mk.Waiting = false;
                        if (MenuKeyButton != null) MenuKeyButton.Content = key_name(mk.Key);
                        e.Handled = true;
                        return;
                    }

                    int vk = KeyInterop.VirtualKeyFromKey(e.Key);
                    mk.Key = vk;
                    mk.MouseButton = -1;
                    mk.Waiting = false;
                    if (MenuKeyButton != null) MenuKeyButton.Content = e.Key.ToString();
                    apply_menu_hotkey();
                    e.Handled = true;
                    return;
                }

                var kb = Options.Settings.Aiming.AimbotKey;
                if (kb != null && kb.Waiting)
                {
                    int vk = KeyInterop.VirtualKeyFromKey(e.Key);
                    kb.Key = vk;
                    kb.MouseButton = -1;
                    kb.Waiting = false;
                    if (AimbotKeyButton != null) AimbotKeyButton.Content = e.Key.ToString();
                    e.Handled = true;
                    return;
                }

                var dk = Options.Settings.Network.DeSyncBind;
                if (dk != null && dk.Waiting)
                {
                    if (e.Key == Key.Escape)
                    {
                        dk.Waiting = false;
                        if (DesyncBindButton != null) DesyncBindButton.Content = "SET";
                        e.Handled = true;
                        return;
                    }

                    int vk = KeyInterop.VirtualKeyFromKey(e.Key);
                    dk.Key = vk;
                    dk.MouseButton = -1;
                    dk.Waiting = false;
                    if (DesyncBindButton != null) DesyncBindButton.Content = e.Key.ToString();
                    e.Handled = true;
                    return;
                }

                var fk = Options.Settings.Flight.VFlightBind;
                if (fk != null && fk.Waiting)
                {
                    if (e.Key == Key.Escape)
                    {
                        fk.Waiting = false;
                        if (FlightBindButton != null) FlightBindButton.Content = "SET";
                        e.Handled = true;
                        return;
                    }

                    int vk = KeyInterop.VirtualKeyFromKey(e.Key);
                    fk.Key = vk;
                    fk.MouseButton = -1;
                    fk.Waiting = false;
                    if (FlightBindButton != null) FlightBindButton.Content = e.Key.ToString();
                    e.Handled = true;
                    return;
                }

                var sk = Options.Settings.Silent.SilentAimbotKey;
                if (sk != null && sk.Waiting)
                {
                    if (e.Key == Key.Escape)
                    {
                        sk.Waiting = false;
                        if (SilentAimbotKeyButton != null) SilentAimbotKeyButton.Content = "SET";
                        e.Handled = true;
                        return;
                    }

                    int vk = KeyInterop.VirtualKeyFromKey(e.Key);
                    sk.Key = vk;
                    sk.MouseButton = -1;
                    sk.Waiting = false;
                    if (SilentAimbotKeyButton != null) SilentAimbotKeyButton.Content = e.Key.ToString();
                    e.Handled = true;
                    return;
                }
            }
            catch { }
        }

        private void mainwindow_previousmousedown(object sender, MouseButtonEventArgs e)
        {
            try
            {
                var kb = Options.Settings.Aiming.AimbotKey;
                if (kb != null && kb.Waiting)
                {
                    int mb = e.ChangedButton == MouseButton.Left ? 0 : e.ChangedButton == MouseButton.Right ? 1 : e.ChangedButton == MouseButton.Middle ? 2 : -1;
                    if (mb >= 0)
                    {
                        kb.MouseButton = mb;
                        kb.Key = 0;
                        kb.Waiting = false;
                        if (AimbotKeyButton != null) AimbotKeyButton.Content = mb == 0 ? "M1" : mb == 1 ? "M2" : "M3";
                        e.Handled = true;
                        return;
                    }
                }

                var dk = Options.Settings.Network.DeSyncBind;
                if (dk != null && dk.Waiting)
                {
                    int mb = e.ChangedButton == MouseButton.Left ? 0 : e.ChangedButton == MouseButton.Right ? 1 : e.ChangedButton == MouseButton.Middle ? 2 : -1;
                    if (mb >= 0)
                    {
                        dk.MouseButton = mb;
                        dk.Key = 0;
                        dk.Waiting = false;
                        if (DesyncBindButton != null) DesyncBindButton.Content = mb == 0 ? "M1" : mb == 1 ? "M2" : "M3";
                        e.Handled = true;
                        return;
                    }
                }

                var fk = Options.Settings.Flight.VFlightBind;
                if (fk != null && fk.Waiting)
                {
                    int mb = e.ChangedButton == MouseButton.Left ? 0 : e.ChangedButton == MouseButton.Right ? 1 : e.ChangedButton == MouseButton.Middle ? 2 : -1;
                    if (mb >= 0)
                    {
                        fk.MouseButton = mb;
                        fk.Key = 0;
                        fk.Waiting = false;
                        if (FlightBindButton != null) FlightBindButton.Content = mb == 0 ? "M1" : mb == 1 ? "M2" : "M3";
                        e.Handled = true;
                        return;
                    }
                }

                var sk = Options.Settings.Silent.SilentAimbotKey;
                if (sk != null && sk.Waiting)
                {
                    int mb = e.ChangedButton == MouseButton.Left ? 0 : e.ChangedButton == MouseButton.Right ? 1 : e.ChangedButton == MouseButton.Middle ? 2 : -1;
                    if (mb >= 0)
                    {
                        sk.MouseButton = mb;
                        sk.Key = 0;
                        sk.Waiting = false;
                        if (SilentAimbotKeyButton != null) SilentAimbotKeyButton.Content = mb == 0 ? "M1" : mb == 1 ? "M2" : "M3";
                        e.Handled = true;
                        return;
                    }
                }
            }
            catch { }
        }

        private async void hookitup(object sender, RoutedEventArgs e)
        {
            StatusText.Text = "ATTACHING";
            StatusText.Foreground = (SolidColorBrush)Application.Current.Resources["AccentBrush"];
            StatusIndicator.Fill = Brushes.Orange;

            try
            {
                notify.Notify("Attaching!", "Looking for the game...");
                LogsWindow.ShowConsole();

                var m = new Memory();
                bool ok = m.Attach("RobloxPlayerBeta") || m.Attach("RobloxPlayer");

                if (!ok)
                {
                    LogsWindow.Log("Process not found. Starting the watcher...");
                    StatusText.Text = "WAITING";
                    StatusIndicator.Fill = Brushes.Orange;
                    watchit();
                    return;
                }

                await fixit(m);
            }
            catch { StatusText.Text = "ERROR"; }
        }

        private void watchit()
        {
            try
            {
                _lookin?.Cancel();
                _lookin = new CancellationTokenSource();
                var t = _lookin.Token;

                Task.Run(async () =>
                {
                    while (!t.IsCancellationRequested)
                    {
                        try
                        {
                            var m = new Memory();
                            if (m.Attach("RobloxPlayerBeta") || m.Attach("RobloxPlayer"))
                            {
                                LogsWindow.Log("Found it! hooking up...");
                                await fixit(m);
                                break;
                            }
                        }
                        catch { }
                        await Task.Delay(1000, t).ContinueWith(_ => { });
                    }
                }, t);
            }
            catch { }
        }

        private async Task fixit(Memory m)
        {
            try
            {
                LogsWindow.Log("Linked. Base: 0x{0:X}", m.Base);
                Dispatcher.Invoke(() => { try { BaseValueText.Text = $"0x{m.Base:X16}"; } catch { } });

                Storage.Initialize(m);

                try
                {
                    if (!string.IsNullOrEmpty(Storage.LocalPlayerName))
                        Dispatcher.Invoke(() => { try { ProfileNameText.Text = Storage.LocalPlayerName; } catch { } });

                    var url = await handler.GetAvatarHeadshotUrlAsync(Storage.LocalPlayerUserId);
                    if (!string.IsNullOrEmpty(url))
                    {
                        byte[] data = null;
                        try { using (var hc = new HttpClient()) data = await hc.GetByteArrayAsync(url); } catch { }

                        if (data != null && data.Length > 0)
                        {
                            Dispatcher.Invoke(() =>
                            {
                                try
                                {
                                    using (var ms = new System.IO.MemoryStream(data))
                                    {
                                        var bmp = new BitmapImage();
                                        bmp.BeginInit();
                                        bmp.CacheOption = BitmapCacheOption.OnLoad;
                                        bmp.StreamSource = ms;
                                        bmp.EndInit();
                                        bmp.Freeze();
                                        ProfileEllipse.Fill = new ImageBrush(bmp) { Stretch = Stretch.UniformToFill };
                                    }
                                }
                                catch { }
                            });
                        }
                    }
                }
                catch { }

                Dispatcher.Invoke(() =>
                {
                    if (Storage.BaseAddress != 0) BaseValueText.Text = $"0x{Storage.BaseAddress:X16}";
                    if (PidValueText != null) PidValueText.Text = Storage.ProcessId != 0 ? Storage.ProcessId.ToString() : "N/A";
                    if (VersionValueText != null) VersionValueText.Text = string.IsNullOrEmpty(Storage.RobloxVersion) ? "(unknown)" : Storage.RobloxVersion;
                });

                if (Storage.IsInitialized)
                {
                    notify.Notify("Success", "System is live.");
                    Dispatcher.Invoke(() =>
                    {
                        StatusText.Text = "ACTIVE";
                        StatusIndicator.Fill = Brushes.LimeGreen;
                        StatusIndicator.Effect = new System.Windows.Media.Effects.DropShadowEffect() { Color = Colors.LimeGreen, BlurRadius = 8, ShadowDepth = 0 };
                        if (_hiddenMode || Visibility != Visibility.Visible)
                        {
                            Visibility = Visibility.Visible;
                            WindowState = WindowState.Normal;
                            Activate();
                        }
                    });

                    _lookin?.Cancel();
                    _lookin = null;

                    try { player.Start(); playerobjects.Start(); HumanoidModule.Start(); TPHandler.Start(); CameraModule.Start(); visuals.Start(); aiming.Start(); desync.Start(); flight.Start(); silentaiming.Start(); triggerbot.Start(); hitbox.Start(); spinbot.Start(); gravityhack.Start(); knockbackhack.Start(); hitfix.Start(); aimassist.Start(); fpscap.Start(); } catch { }

                    try { watch_roblox_death(); } catch { }

                    var refresher = new Thread(() =>
                    {
                        while (true)
                        {
                            try { Storage.RefreshInstances(); } catch { }
                            Thread.Sleep(1500);
                        }
                    }) { IsBackground = true };
                    refresher.Start();


                    Dispatcher.Invoke(() =>
                    {
                        ((Storyboard)Resources["InlineStatusFadeIn"]).Begin();
                        CachesIndicator.Fill = StorageIndicator.Fill = VmmIndicator.Fill = Brushes.LimeGreen;

                        var wrap = FindName("InlineStatusWrap") as FrameworkElement;
                        if (wrap != null)
                        {
                            wrap.Visibility = Visibility.Visible;
                            var tr = wrap.RenderTransform as TranslateTransform ?? new TranslateTransform(0, -8);
                            wrap.RenderTransform = tr;
                            tr.BeginAnimation(TranslateTransform.YProperty, new DoubleAnimation(-8, 0, TimeSpan.FromMilliseconds(220)) { EasingFunction = new CubicEase { EasingMode = EasingMode.EaseOut } });
                            wrap.BeginAnimation(UIElement.OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(200)));

                            Action<string, int> show = (name, d) =>
                            {
                                var el = FindName(name) as FrameworkElement;
                                if (el == null) return;
                                el.Visibility = Visibility.Visible;
                                el.BeginAnimation(UIElement.OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(160)) { BeginTime = TimeSpan.FromMilliseconds(d) });
                            };
                            show("CachesPanel", 80); show("StoragePanel", 160); show("VmmPanel", 240);
                        }
                    });
                }
                else
                {
                    Dispatcher.Invoke(() => { StatusText.Text = "ACTIVE (partial)"; StatusIndicator.Fill = Brushes.Yellow; });
                }
            }
            catch { Dispatcher.Invoke(() => { StatusText.Text = "ERROR"; }); }
        }

        private static string key_name(int vk)
        {
            if (vk <= 0) return "SET";
            try { return KeyInterop.KeyFromVirtualKey(vk).ToString(); } catch { return vk.ToString("X2"); }
        }

        private void menukey(object sender, RoutedEventArgs e)
        {
            if (_shutup) return;
            var mk = Options.Settings.Menu.MenuKey;
            if (mk == null) return;
            mk.Waiting = true;
            if (MenuKeyButton != null) MenuKeyButton.Content = "PRESS...";
        }

        private void aimbottgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Aiming.Aimbot = tb.IsChecked == true; }

        private void toggletype(object sender, SelectionChangedEventArgs e) { if (_shutup || ToggleTypeCombo == null) return; Options.Settings.Aiming.ToggleType = ToggleTypeCombo.SelectedIndex == 1 ? 1 : 0; }

        private void aimingtype(object sender, SelectionChangedEventArgs e) { if (_shutup || AimingTypeCombo == null) return; Options.Settings.Aiming.AimingType = AimingTypeCombo.SelectedIndex; }

        private void aimbotkey(object sender, RoutedEventArgs e) { if (_shutup) return; var kb = Options.Settings.Aiming.AimbotKey; if (kb == null) return; kb.Waiting = true; if (AimbotKeyButton != null) AimbotKeyButton.Content = "PRESS..."; }

        private void globalteamcheck(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Checks.TeamCheck = tb.IsChecked == true; }

        private void downedcheck(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Checks.DownedCheck = tb.IsChecked == true; }

        private void stickyaim(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Aiming.StickyAim = tb.IsChecked == true; }

        private void transparency(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Checks.TransparencyCheck = tb.IsChecked == true; }

        private void sensitivity(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || SensitivitySlider == null) return; Options.Settings.Aiming.Sensitivity = (float)SensitivitySlider.Value; if (SensitivityValueText != null) SensitivityValueText.Text = Options.Settings.Aiming.Sensitivity.ToString("0.00"); }

        private void smoothness(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Aiming.Smoothness = tb.IsChecked == true; }

        private void smoothx(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || SmoothnessXSlider == null) return; Options.Settings.Aiming.SmoothnessX = (float)SmoothnessXSlider.Value; }

        private void smoothy(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || SmoothnessYSlider == null) return; Options.Settings.Aiming.SmoothnessY = (float)SmoothnessYSlider.Value; }

        private void prediction(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Aiming.Prediction = tb.IsChecked == true; }

        private void predx(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || PredictionXSlider == null) return; Options.Settings.Aiming.PredictionX = (float)PredictionXSlider.Value; }

        private void predy(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || PredictionYSlider == null) return; Options.Settings.Aiming.PredictionY = (float)PredictionYSlider.Value; }

        private void aimbotfov(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || AimbotFovSlider == null) return; Options.Settings.Aiming.FOV = (float)AimbotFovSlider.Value; if (AimbotFovValueText != null) AimbotFovValueText.Text = Options.Settings.Aiming.FOV.ToString("0"); }

        private void range(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || RangeSlider == null) return; Options.Settings.Aiming.Range = (float)RangeSlider.Value; if (RangeValueText != null) RangeValueText.Text = Options.Settings.Aiming.Range.ToString("0"); }

        private void showfov(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Aiming.ShowFOV = tb.IsChecked == true; }

        private void shaketgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Aiming.Shake = tb.IsChecked == true; }
        private void shakedist(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || ShakeDistSlider == null) return; Options.Settings.Aiming.ShakeDistance = (float)ShakeDistSlider.Value; if (ShakeDistText != null) ShakeDistText.Text = Options.Settings.Aiming.ShakeDistance.ToString("0"); }
        private void shakeamt(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || ShakeAmtSlider == null) return; Options.Settings.Aiming.ShakeAmount = (float)ShakeAmtSlider.Value; if (ShakeAmtText != null) ShakeAmtText.Text = Options.Settings.Aiming.ShakeAmount.ToString("0"); }

        private void objectesptgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Visuals.ObjectESP = tb.IsChecked == true; }
        private void aimassigtgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.AimAssist.Enabled = tb.IsChecked == true; }
        private void aimassistfiretgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.AimAssist.OnlyWhenFiring = tb.IsChecked == true; }
        private void aimassistfov(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || AimAssistFovSlider == null) return; Options.Settings.AimAssist.FovRadius = (float)AimAssistFovSlider.Value; if (AimAssistFovText != null) AimAssistFovText.Text = AimAssistFovSlider.Value.ToString("0"); }
        private void aimassistspeed(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || AimAssistSpeedSlider == null) return; Options.Settings.AimAssist.Speed = (float)(AimAssistSpeedSlider.Value / 100.0); if (AimAssistSpeedText != null) AimAssistSpeedText.Text = AimAssistSpeedSlider.Value.ToString("0"); }
        private void aimassistrange(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || AimAssistRangeSlider == null) return; Options.Settings.AimAssist.Range = (float)AimAssistRangeSlider.Value; if (AimAssistRangeText != null) AimAssistRangeText.Text = AimAssistRangeSlider.Value.ToString("0"); }
        private void fpscaptgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Performance.FpsCapEnabled = tb.IsChecked == true; }
        private void maxfps(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || MaxFpsSlider == null) return; Options.Settings.Performance.MaxFps = (int)MaxFpsSlider.Value; if (MaxFpsText != null) MaxFpsText.Text = MaxFpsSlider.Value.ToString("0"); }
        private void triggerbotgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Triggerbot.Enabled = tb.IsChecked == true; }
        private void hitboxgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Hitbox.Enabled = tb.IsChecked == true; }
        private void spinbotgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Spinbot.Enabled = tb.IsChecked == true; }
        private void gravitygl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Gravity.Enabled = tb.IsChecked == true; }
        private void knockbackgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Knockback.Enabled = tb.IsChecked == true; }
        private void hitfixgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Hitfix.Enabled = tb.IsChecked == true; }

        private void fillfov(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Aiming.FillFOV = tb.IsChecked == true; }

        private void animatedfov(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ANIMATE" : "ANIMATE"; Options.Settings.Aiming.AnimatedFOV = tb.IsChecked == true; }

        private void targetbone(object sender, SelectionChangedEventArgs e) { if (_shutup || TargetBoneCombo == null) return; Options.Settings.Aiming.TargetBone = TargetBoneCombo.SelectedIndex; }

        private void startvibing(object sender, RoutedEventArgs e) { ((Storyboard)Resources["FadeInSequence"]).Begin(); ((Storyboard)Resources["PulseGlow"]).Begin(); settheme(Colors.White); }

        private void bye(object sender, RoutedEventArgs e) { _forceClose = true; try { HumanoidModule.Stop(); CameraModule.Stop(); visuals.Stop(); TPHandler.Stop(); aiming.Stop(); desync.Stop(); flight.Stop(); silentaiming.Stop(); triggerbot.Stop(); hitbox.Stop(); spinbot.Stop(); gravityhack.Stop(); knockbackhack.Stop(); hitfix.Stop(); aimassist.Stop(); fpscap.Stop(); IMGUI.Program.kill();} catch { } Application.Current.Shutdown(); }

        protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e) { if (e.ChangedButton == MouseButton.Left) DragMove(); }


        private void oncheck(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = "ON"; if (tb.Name == "WalkspeedToggle") Options.Settings.Humanoid.WalkspeedEnabled = true; else if (tb.Name == "JumpPowerToggle") Options.Settings.Humanoid.JumpPowerEnabled = true; else if (tb.Name == "FovToggle") Options.Settings.Camera.FOVEnabled = true; }

        private void offcheck(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = "OFF"; if (tb.Name == "WalkspeedToggle") Options.Settings.Humanoid.WalkspeedEnabled = false; else if (tb.Name == "JumpPowerToggle") Options.Settings.Humanoid.JumpPowerEnabled = false; else if (tb.Name == "FovToggle") Options.Settings.Camera.FOVEnabled = false; }

        private void cyanvibe(object sender, RoutedEventArgs e) => settheme(Color.FromRgb(0, 255, 255));
        private void redvibe(object sender, RoutedEventArgs e) => settheme(Color.FromRgb(255, 50, 50));
        private void purplevibe(object sender, RoutedEventArgs e) => settheme(Color.FromRgb(180, 50, 255));
        private void greenvibe(object sender, RoutedEventArgs e) => settheme(Color.FromRgb(50, 255, 100));
        private void whitevibe(object sender, RoutedEventArgs e) => settheme(Colors.White);

        private void settheme(Color c) { Application.Current.Resources["AccentColor"] = c; Application.Current.Resources["AccentBrush"] = new SolidColorBrush(c); if (StatusText.Text == "ACTIVE") StatusText.Foreground = new SolidColorBrush(c); }

        private void showwalkconfig(object sender, RoutedEventArgs e) => WalkConfig.Visibility = WalkConfig.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;
        private void showjumpconfig(object sender, RoutedEventArgs e) => JumpConfig.Visibility = JumpConfig.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;
        private void showfovconfig(object sender, RoutedEventArgs e) => FovConfig.Visibility = FovConfig.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;
        private void showconsole(object sender, RoutedEventArgs e) => LogsWindow.ShowConsole();

        private void speedy(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || WalkspeedSlider == null) return; Options.Settings.Humanoid.Walkspeed = (float)WalkspeedSlider.Value; if (WalkspeedValueText != null) WalkspeedValueText.Text = Options.Settings.Humanoid.Walkspeed.ToString("0"); }

        private void jumpy(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || JumpPowerSlider == null) return; Options.Settings.Humanoid.JumpPower = (float)JumpPowerSlider.Value; if (JumpPowerValueText != null) JumpPowerValueText.Text = Options.Settings.Humanoid.JumpPower.ToString("0"); }

        private void fieldofview(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || FovSlider == null) return; Options.Settings.Camera.FOV = (float)FovSlider.Value; if (FovValueText != null) FovValueText.Text = Options.Settings.Camera.FOV.ToString("0"); }
        private void boxespone(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "ON"; Options.Settings.Visuals.BoxESP = true; }
        private void boxespoff(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "OFF"; Options.Settings.Visuals.BoxESP = false; }
        private void boxfillon(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "ON"; Options.Settings.Visuals.FilledBox = true; }
        private void boxfilloff(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "OFF"; Options.Settings.Visuals.FilledBox = false; }
        private void showtracerconfig(object sender, RoutedEventArgs e) => TracerConfig.Visibility = TracerConfig.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;
        private void tracersize(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || TracerThicknessSlider == null) return; Options.Settings.Visuals.TracerThickness = (float)TracerThicknessSlider.Value; if (TracerThicknessValueText != null) TracerThicknessValueText.Text = Options.Settings.Visuals.TracerThickness.ToString("0.0"); }
        private void traceron(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "ON"; Options.Settings.Visuals.Tracers = true; }
        private void traceroff(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "OFF"; Options.Settings.Visuals.Tracers = false; }
        private void skeletonon(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "ON"; Options.Settings.Visuals.Skeleton = true; }
        private void skeletonoff(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "OFF"; Options.Settings.Visuals.Skeleton = false; }
        private void shownameconfig(object sender, RoutedEventArgs e) => NameConfig.Visibility = NameConfig.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;
        private void namesize(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || NameSizeSlider == null) return; Options.Settings.Visuals.NameSize = (float)NameSizeSlider.Value; if (NameSizeValueText != null) NameSizeValueText.Text = Options.Settings.Visuals.NameSize.ToString("0"); }
        private void nameon(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "ON"; Options.Settings.Visuals.Name = true; }
        private void nameoff(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "OFF"; Options.Settings.Visuals.Name = false; }
        private void showdistconfig(object sender, RoutedEventArgs e) => DistanceConfig.Visibility = DistanceConfig.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;
        private void distsize(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || DistanceSizeSlider == null) return; Options.Settings.Visuals.DistanceSize = (float)DistanceSizeSlider.Value; if (DistanceSizeValueText != null) DistanceSizeValueText.Text = Options.Settings.Visuals.DistanceSize.ToString("0"); }
        private void diston(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "ON"; Options.Settings.Visuals.Distance = true; }
        private void distoff(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "OFF"; Options.Settings.Visuals.Distance = false; }
        private void healthon(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "ON"; Options.Settings.Visuals.Health = true; }
        private void healthoff(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "OFF"; Options.Settings.Visuals.Health = false; }
        private void showheadconfig(object sender, RoutedEventArgs e) => HeadCircleConfig.Visibility = HeadCircleConfig.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;
        private void headsize(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || HeadCircleScaleSlider == null) return; Options.Settings.Visuals.HeadCircleMaxScale = (float)HeadCircleScaleSlider.Value; if (HeadCircleScaleValueText != null) HeadCircleScaleValueText.Text = Options.Settings.Visuals.HeadCircleMaxScale.ToString("0.0"); }
        private void headon(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "ON"; Options.Settings.Visuals.HeadCircle = true; }
        private void headoff(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "OFF"; Options.Settings.Visuals.HeadCircle = false; }
        private void corneron(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "ON"; Options.Settings.Visuals.CornerESP = true; }
        private void corneroff(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "OFF"; Options.Settings.Visuals.CornerESP = false; }
        private void haton(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "ON"; Options.Settings.Visuals.ChinaHat = true; }
        private void hatoff(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "OFF"; Options.Settings.Visuals.ChinaHat = false; }
        private void localon(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "ON"; Options.Settings.Visuals.LocalPlayerESP = true; }
        private void localoff(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) tb.Content = "OFF"; Options.Settings.Visuals.LocalPlayerESP = false; }
        private void boxtgl(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) { tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Visuals.BoxESP = tb.IsChecked == true; } }
        private void filltgl(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) { tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Visuals.FilledBox = tb.IsChecked == true; } }
        private void tracecuh(object sender, RoutedEventArgs e) => showtracerconfig(sender, e);
        private void tracetgl(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) { tb.Content = tb.IsChecked == true ? "ON" : "OFF"; if (tb.IsChecked == true) traceron(sender, e); else traceroff(sender, e); } }
        private void tracew(object sender, RoutedPropertyChangedEventArgs<double> e) => tracersize(sender, e);
        private void namecuh(object sender, RoutedEventArgs e) => shownameconfig(sender, e);
        private void nametgl(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) { tb.Content = tb.IsChecked == true ? "ON" : "OFF"; if (tb.IsChecked == true) nameon(sender, e); else nameoff(sender, e); } }
        private void distcuh(object sender, RoutedEventArgs e) => showdistconfig(sender, e);
        private void disttgl(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) { tb.Content = tb.IsChecked == true ? "ON" : "OFF"; if (tb.IsChecked == true) diston(sender, e); else distoff(sender, e); } }
        private void circlecuh(object sender, RoutedEventArgs e) => showheadconfig(sender, e);
        private void circletgl(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) { tb.Content = tb.IsChecked == true ? "ON" : "OFF"; if (tb.IsChecked == true) headon(sender, e); else headoff(sender, e); } }
        private void circlescale(object sender, RoutedPropertyChangedEventArgs<double> e) => headsize(sender, e);
        private void cornertgl(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) { tb.Content = tb.IsChecked == true ? "ON" : "OFF"; if (tb.IsChecked == true) corneron(sender, e); else corneroff(sender, e); } }
        private void hattgl(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) { tb.Content = tb.IsChecked == true ? "ON" : "OFF"; if (tb.IsChecked == true) haton(sender, e); else hatoff(sender, e); } }
        private void localtgl(object sender, RoutedEventArgs e) { if (sender is ToggleButton tb) { tb.Content = tb.IsChecked == true ? "ON" : "OFF"; if (tb.IsChecked == true) localon(sender, e); else localoff(sender, e); } }
        private void whiteplz(object sender, RoutedEventArgs e) => whitevibe(sender, e);
        private void cyanplz(object sender, RoutedEventArgs e) => cyanvibe(sender, e);
        private void redplz(object sender, RoutedEventArgs e) => redvibe(sender, e);
        private void purpleplz(object sender, RoutedEventArgs e) => purplevibe(sender, e);
        private void greenplz(object sender, RoutedEventArgs e) => greenvibe(sender, e);
        private void showlogs(object sender, RoutedEventArgs e) => showconsole(sender, e);
        private void desynckey(object sender, RoutedEventArgs e) { if (_shutup) return; var kb = Options.Settings.Network.DeSyncBind; if (kb == null) return; kb.Waiting = true; var b = FindName("DesyncBindButton") as Button; if (b != null) b.Content = "PRESS..."; try { Dispatcher.BeginInvoke(new Action(() => { try { Keyboard.Focus(this); this.Focus(); } catch { } })); } catch { } }
        private void desyncvisualizer(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "VIS" : "VIS"; Options.Settings.Network.DeSyncVisualizer = tb.IsChecked == true; }
        private void desynctgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Network.DeSync = tb.IsChecked == true; }
        private void flightkey(object sender, RoutedEventArgs e) { if (_shutup) return; var kb = Options.Settings.Flight.VFlightBind; if (kb == null) return; kb.Waiting = true; var b = FindName("FlightBindButton") as Button; if (b != null) b.Content = "PRESS..."; try { Dispatcher.BeginInvoke(new Action(() => { try { Keyboard.Focus(this); this.Focus(); } catch { } })); } catch { } }
        private void flighttgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Flight.VFlight = tb.IsChecked == true; }
        private void flightspeed(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || FlightSpeedSlider == null) return; Options.Settings.Flight.VFlightSpeed = (float)FlightSpeedSlider.Value; }
        private void silentaimbottgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Silent.SilentAimbot = tb.IsChecked == true; }
        private void silentalwaysontgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Silent.AlwaysOn = tb.IsChecked == true; }
        private void silentvisualizertgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Silent.SilentVisualizer = tb.IsChecked == true; }
        private void silentshowfovtgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Silent.ShowSilentFOV = tb.IsChecked == true; }
        private void silentpredictiontgl(object sender, RoutedEventArgs e) { if (_shutup || !(sender is ToggleButton tb)) return; tb.Content = tb.IsChecked == true ? "ON" : "OFF"; Options.Settings.Silent.SPrediction = tb.IsChecked == true; }
        private void silentfov(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || SilentFovSlider == null) return; Options.Settings.Silent.SFOV = (float)SilentFovSlider.Value; if (SilentFovValueText != null) SilentFovValueText.Text = Options.Settings.Silent.SFOV.ToString("0"); }
        private void silentpredx(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || SilentPredictionXSlider == null) return; Options.Settings.Silent.PredictionX = (float)SilentPredictionXSlider.Value; if (SilentPredictionXValueText != null) SilentPredictionXValueText.Text = Options.Settings.Silent.PredictionX.ToString("0"); }
        private void silentpredy(object sender, RoutedPropertyChangedEventArgs<double> e) { if (_shutup || SilentPredictionYSlider == null) return; Options.Settings.Silent.PredictionY = (float)SilentPredictionYSlider.Value; if (SilentPredictionYValueText != null) SilentPredictionYValueText.Text = Options.Settings.Silent.PredictionY.ToString("0"); }
        private void silentkey(object sender, RoutedEventArgs e) { if (_shutup) return; var kb = Options.Settings.Silent.SilentAimbotKey; if (kb == null) return; kb.Waiting = true; if (SilentAimbotKeyButton != null) SilentAimbotKeyButton.Content = "PRESS..."; }
        private void savecfg(object sender, RoutedEventArgs e) { try { string configName = confignametext?.Text?.Trim(); if (string.IsNullOrEmpty(configName)) configName = "default"; if (ConfigManager.SaveConfig(configName)) { refreshcfglist(); updatedefaulttext(); notify.Notify("Config saved", $"Configuration '{configName}' saved successfully", 2000); } else { notify.Notify("Save failed", $"Failed to save configuration '{configName}'", 2000); } } catch { } }
        private void loadcfg(object sender, RoutedEventArgs e) { try { string configName = confignametext?.Text?.Trim(); if (string.IsNullOrEmpty(configName)) configName = "default"; if (ConfigManager.LoadConfig(configName)) { _shutup = true; loaddat(sender, e); _shutup = false; notify.Notify("Config loaded", $"Configuration '{configName}' loaded successfully", 2000); } else { notify.Notify("Load failed", $"Failed to load configuration '{configName}'", 2000); } } catch { } }
        private void resetcfg(object sender, RoutedEventArgs e) { try { ConfigManager.ResetToDefaults(); _shutup = true; loaddat(sender, e); _shutup = false; notify.Notify("Reset complete", "All settings reset to default values", 2000); } catch { } }
        private void deletecfg(object sender, RoutedEventArgs e) { try { string configName = confignametext?.Text?.Trim(); if (string.IsNullOrEmpty(configName)) { notify.Notify("Invalid name", "Please enter a config name to delete", 2000); return; } if (ConfigManager.DeleteConfig(configName)) { refreshcfglist(); string defaultConfig = ConfigManager.GetDefaultConfigName(); if (defaultConfig != null && defaultConfig.Equals(configName, StringComparison.OrdinalIgnoreCase)) ConfigManager.SetDefaultConfigName(null); updatedefaulttext(); if (confignametext != null) confignametext.Text = ""; notify.Notify("Config deleted", $"Configuration '{configName}' deleted", 2000); } else { notify.Notify("Delete failed", $"Configuration '{configName}' not found", 2000); } } catch { } }
        private void togglecfglist(object sender, RoutedEventArgs e) { try { if (configlistdropdown != null && confignametext != null) { configlistdropdown.Visibility = Visibility.Visible; confignametext.Visibility = Visibility.Collapsed; refreshcfglist(); configlistdropdown.Focus(); configlistdropdown.IsDropDownOpen = false; configlistdropdown.IsDropDownOpen = true; } } catch { } }
        private void cfglistchanged(object sender, SelectionChangedEventArgs e) { try { if (configlistdropdown != null && configlistdropdown.SelectedItem is ComboBoxItem item) { string configName = item.Content?.ToString(); if (!string.IsNullOrEmpty(configName) && confignametext != null) confignametext.Text = configName; } } catch { } }
        private void setdefaultcfg(object sender, RoutedEventArgs e) { try { string configName = confignametext?.Text?.Trim(); if (string.IsNullOrEmpty(configName)) { notify.Notify("Invalid name", "Please enter a config name to set as default", 2000); return; } string[] configs = ConfigManager.GetAvailableConfigs(); if (!Array.Exists(configs, c => c.Equals(configName, StringComparison.OrdinalIgnoreCase))) { notify.Notify("Config not found", $"Configuration '{configName}' does not exist. Save it first.", 2000); return; } if (ConfigManager.SetDefaultConfigName(configName)) { updatedefaulttext(); notify.Notify("Default set", $"'{configName}' is now the default config", 2000); } else { notify.Notify("Failed", $"Failed to set '{configName}' as default", 2000); } } catch { } }
        private void opencfgfolder(object sender, RoutedEventArgs e) { try { string configDir = ConfigManager.GetConfigDirectory(); System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo { FileName = "explorer.exe", Arguments = configDir, UseShellExecute = true }); } catch { notify.Notify("Error", "Failed to open config folder", 2000); } }
        private void refreshcfglist() { try { if (configlistdropdown == null) return; configlistdropdown.Items.Clear(); string[] configs = ConfigManager.GetAvailableConfigs(); foreach (string config in configs) configlistdropdown.Items.Add(new ComboBoxItem { Content = config }); } catch { } }
    }

}
