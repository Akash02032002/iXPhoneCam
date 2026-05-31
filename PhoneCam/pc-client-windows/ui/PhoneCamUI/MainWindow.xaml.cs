using System;
using System.Diagnostics;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace PhoneCamUI
{
    /// <summary>
    /// WPF control panel for PhoneCam.
    /// Launches PhoneCamService.exe and sends control commands via its stdin.
    /// Supports USB, WiFi, and Bluetooth connections with audio + video streaming.
    /// </summary>
    public partial class MainWindow : Window
    {
        private Process? _serviceProcess;
        private StreamWriter? _serviceStdin;
        private CancellationTokenSource? _cts;
        private bool _isConnected;
        private bool _phoneSpeakerEnabled;
        private bool _autoFocusEnabled;
        private bool _isRefreshingBluetoothDevices;
        private AudioBridgeOutput? _audioRoute;
        private string? _usbAdbSerial;

        private readonly record struct VideoProfile(string Label, int Width, int Height, int Bitrate, int Fps);
        private readonly record struct AudioBridgeOutput(string OutputName, string ZoomMicrophoneName, string SpeakerOutputName);
        private readonly record struct BluetoothDeviceOption(string Name, string Address)
        {
            public override string ToString() => $"{Name} ({Address})";
        }
        private readonly record struct AdbDevice(string Serial, string State);

        [DllImport("winmm.dll", CharSet = CharSet.Auto)]
        private static extern uint waveOutGetNumDevs();

        [DllImport("winmm.dll", CharSet = CharSet.Auto)]
        private static extern uint waveOutGetDevCaps(uint uDeviceID, out WaveOutCaps pwoc, int cbwoc);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
        private struct WaveOutCaps
        {
            public ushort wMid;
            public ushort wPid;
            public uint vDriverVersion;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
            public string szPname;
            public uint dwFormats;
            public ushort wChannels;
            public ushort wReserved1;
            public uint dwSupport;
        }

        private static readonly VideoProfile[] VideoProfiles =
        {
            new("480p", 640, 480, 800_000, 10),
            new("720p", 1280, 720, 3_500_000, 30),
            new("1080p", 1920, 1080, 6_000_000, 30)
        };

        public MainWindow()
        {
            InitializeComponent();
            _audioRoute = DetectAudioRoute();
            UpdateAudioRouteStatus();
            UpdateConnectionMode();
        }

        private void OnConnectionModeChanged(object sender, RoutedEventArgs e)
        {
            UpdateConnectionMode();
        }

        private void UpdateConnectionMode()
        {
            if (txtAddressLabel == null) return;

            if (rbUsb?.IsChecked == true)
            {
                txtAddressLabel.Text = "Address (auto for USB):";
                txtAddress.Text = "127.0.0.1";
                txtAddress.IsEnabled = false;
                txtDeviceStatus.Text = "USB is recommended for Zoom quality.";
            }
            else if (rbWifi?.IsChecked == true)
            {
                txtAddressLabel.Text = "Phone WiFi IP:";
                txtAddress.Text = "";
                txtAddress.IsEnabled = true;
                txtDeviceStatus.Text = "Enter the phone IP shown in the PhoneCam mobile app.";
            }
            else if (rbBluetooth?.IsChecked == true)
            {
                txtAddressLabel.Text = "Bluetooth MAC (AA:BB:CC:DD:EE:FF):";
                txtAddress.Text = "";
                txtAddress.IsEnabled = true;
                pnlBluetoothDevices.Visibility = Visibility.Visible;
                txtDeviceStatus.Text = "Open PhoneCam on the phone, select Bluetooth, tap Connect, then connect here.";
                _ = RefreshBluetoothDevicesAsync();
                return;
            }

            pnlBluetoothDevices.Visibility = Visibility.Collapsed;
        }

        private async void OnConnectClick(object sender, RoutedEventArgs e)
        {
            if (_isConnected)
            {
                Disconnect();
                return;
            }

            string mode;
            string address = txtAddress.Text.Trim();
            int port = int.TryParse(txtPort.Text, out int p) ? p : 4747;

            VideoProfile profile = GetSelectedProfile(cboResolution.SelectedIndex);

            if (rbUsb?.IsChecked == true)
            {
                mode = "--usb";
            }
            else if (rbBluetooth?.IsChecked == true)
            {
                mode = $"--bluetooth {address}";
                if (string.IsNullOrEmpty(address))
                {
                    MessageBox.Show("Please enter a Bluetooth MAC address.",
                        "PhoneCam", MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }

                if (address.EndsWith(":D7", StringComparison.OrdinalIgnoreCase))
                {
                    MessageBox.Show("That looks like the WiFi MAC from your screenshot. Use the Bluetooth address ending in D6: 48:9D:D1:BE:AF:D6.",
                        "PhoneCam", MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }
            }
            else
            {
                mode = $"--wifi {address}";
                if (string.IsNullOrEmpty(address))
                {
                    MessageBox.Show("Please enter the phone's WiFi IP address.",
                        "PhoneCam", MessageBoxButton.OK, MessageBoxImage.Warning);
                    return;
                }
            }

            string args = $"{mode} --port {port} --width {profile.Width} --height {profile.Height} --fps {profile.Fps}";
            if (_audioRoute is AudioBridgeOutput bridge)
            {
                args += $" --phone-mic-output {QuoteArg(bridge.OutputName)}";
            }

            SetBusy("Preparing...");

            try
            {
                if (rbUsb?.IsChecked == true)
                {
                    txtDeviceStatus.Text = "Checking USB phone and starting PhoneCam mobile app...";
                    string prepSummary = await Task.Run(() => PrepareUsbPhone(port));
                    txtDeviceStatus.Text = prepSummary;
                }

                SetBusy("Connecting...");
                await Task.Run(() => StartService(args));
                ApplyVideoProfile(profile);
                ApplyAutoFocusMode(GetSelectedAutoFocusMode());

                _isConnected = true;
                btnConnect.Content = "Disconnect";
                btnConnect.IsEnabled = true;
                progressConnect.Visibility = Visibility.Collapsed;
                grpControls.IsEnabled = true;
                SetStatus("Connected", Brushes.LimeGreen);
                txtStats.Text = $"Streaming {profile.Label}. Select PhoneCam Virtual Camera in Zoom.";
                StartZoom();

                // Start monitoring
                _cts = new CancellationTokenSource();
                _ = MonitorService(_cts.Token);
            }
            catch (Exception ex)
            {
                SetStatus($"Failed: {ex.Message}", Brushes.Red);
                txtDeviceStatus.Text = ex.Message;
                txtStats.Text = "Connection failed. Check USB debugging and the phone permission prompt.";
                btnConnect.Content = "Connect Phone";
                btnConnect.IsEnabled = true;
                progressConnect.Visibility = Visibility.Collapsed;
            }
        }

        private void StartService(string arguments)
        {
            string exePath = ResolveServicePath();
            string adbPath = ResolveAdbPath();
            StopExistingServiceProcesses();

            ProcessStartInfo startInfo = new()
            {
                FileName = exePath,
                Arguments = arguments,
                WorkingDirectory = Path.GetDirectoryName(exePath) ?? AppDomain.CurrentDomain.BaseDirectory,
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                RedirectStandardInput = true
            };

            string? adbDirectory = Path.GetDirectoryName(adbPath);
            if (!string.IsNullOrWhiteSpace(adbDirectory) && Directory.Exists(adbDirectory))
            {
                string currentPath = startInfo.EnvironmentVariables["PATH"] ?? "";
                startInfo.EnvironmentVariables["PATH"] = adbDirectory + Path.PathSeparator + currentPath;
            }

            if (arguments.Contains("--usb", StringComparison.OrdinalIgnoreCase) &&
                !string.IsNullOrWhiteSpace(_usbAdbSerial))
            {
                startInfo.EnvironmentVariables["ANDROID_SERIAL"] = _usbAdbSerial;
            }

            _serviceProcess = new Process
            {
                StartInfo = startInfo
            };

            _serviceProcess.Start();
            _serviceProcess.BeginOutputReadLine();
            _serviceProcess.BeginErrorReadLine();
            _serviceStdin = _serviceProcess.StandardInput;
            _serviceStdin.AutoFlush = true;

            // Give it a moment to connect
            Thread.Sleep(2000);

            if (_serviceProcess.HasExited)
            {
                string error = _serviceProcess.StandardError.ReadToEnd();
                throw new Exception($"Service exited: {error}");
            }
        }

        private string PrepareUsbPhone(int port)
        {
            string adb = ResolveAdbPath();
            string devices = RunProcess(adb, "devices", 8000);
            var adbDevices = ParseAdbDevices(devices).ToList();
            AdbDevice? targetDevice = adbDevices
                .Where(device => device.State.Equals("device", StringComparison.OrdinalIgnoreCase))
                .OrderBy(device => device.Serial.StartsWith("emulator-", StringComparison.OrdinalIgnoreCase) ? 1 : 0)
                .FirstOrDefault();

            if (targetDevice == null || string.IsNullOrWhiteSpace(targetDevice.Value.Serial))
            {
                if (devices.Contains("unauthorized", StringComparison.OrdinalIgnoreCase))
                {
                    throw new Exception("Phone is connected but not authorized. Accept the USB debugging prompt on the phone.");
                }

                throw new Exception("No USB phone found. Connect the phone with USB debugging enabled.");
            }

            string serial = targetDevice.Value.Serial;
            string adbTarget = $"-s {QuoteArg(serial)}";
            _usbAdbSerial = serial;

            string apk = ResolveApkPath();
            if (File.Exists(apk))
            {
                RunProcess(adb, $"{adbTarget} install -r \"{apk}\"", 45000);
            }

            RunProcess(adb, $"{adbTarget} shell pm grant com.phonecam android.permission.CAMERA", 8000, ignoreErrors: true);
            RunProcess(adb, $"{adbTarget} shell pm grant com.phonecam android.permission.RECORD_AUDIO", 8000, ignoreErrors: true);
            RunProcess(adb, $"{adbTarget} shell am start -n com.phonecam/.ui.MainActivity --ez start_server true", 10000);
            RunProcess(adb, $"{adbTarget} forward tcp:{port} tcp:{port}", 8000, ignoreErrors: true);

            return $"Phone ready over USB ({serial}).";
        }

        private static IEnumerable<AdbDevice> ParseAdbDevices(string output)
        {
            foreach (string line in output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
            {
                if (line.StartsWith("List of devices", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }

                string[] parts = line.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length >= 2)
                {
                    yield return new AdbDevice(parts[0], parts[1]);
                }
            }
        }

        private async Task MonitorService(CancellationToken ct)
        {
            while (!ct.IsCancellationRequested && _serviceProcess != null)
            {
                await Task.Delay(3000, ct);

                if (_serviceProcess.HasExited)
                {
                    Dispatcher.Invoke(() =>
                    {
                        Disconnect();
                        SetStatus("Service stopped unexpectedly", Brushes.Red);
                    });
                    break;
                }
            }
        }

        private void Disconnect()
        {
            _cts?.Cancel();
            _cts = null;

            try
            {
                // Send quit command first for graceful shutdown
                SendServiceCommand("quit");
                Thread.Sleep(500);

                if (_serviceProcess != null && !_serviceProcess.HasExited)
                {
                    _serviceProcess.Kill();
                    _serviceProcess.WaitForExit(3000);
                }
            }
            catch { }

            _serviceStdin = null;
            _serviceProcess = null;
            _isConnected = false;
            _phoneSpeakerEnabled = false;
            _autoFocusEnabled = false;

            btnConnect.Content = "Connect Phone";
            btnConnect.IsEnabled = true;
            progressConnect.Visibility = Visibility.Collapsed;
            grpControls.IsEnabled = false;
            if (cboAutoFocusMode != null) cboAutoFocusMode.SelectedIndex = 0;
            if (btnPhoneSpeaker != null) btnPhoneSpeaker.Content = "Phone Speaker";
            SetStatus("Disconnected", Brushes.Red);
            txtStats.Text = "Ready";
        }

        private async void OnRefreshBluetoothDevices(object sender, RoutedEventArgs e)
        {
            await RefreshBluetoothDevicesAsync();
        }

        private void OnBluetoothDeviceChanged(object sender, SelectionChangedEventArgs e)
        {
            if (cboBluetoothDevices.SelectedItem is BluetoothDeviceOption device)
            {
                txtAddress.Text = device.Address;
                txtDeviceStatus.Text = $"Selected {device.Name}. Start PhoneCam Bluetooth server on the phone before connecting.";
            }
        }

        private async Task RefreshBluetoothDevicesAsync()
        {
            if (_isRefreshingBluetoothDevices || cboBluetoothDevices == null) return;

            _isRefreshingBluetoothDevices = true;
            cboBluetoothDevices.IsEnabled = false;
            txtDeviceStatus.Text = "Scanning paired Bluetooth devices...";

            try
            {
                string output = await Task.Run(() => RunProcess(ResolveServicePath(), "--list-bt", 10000));
                var devices = ParseBluetoothDevices(output).ToList();

                cboBluetoothDevices.ItemsSource = devices;
                cboBluetoothDevices.IsEnabled = true;

                if (devices.Count == 0)
                {
                    txtDeviceStatus.Text = "No paired Bluetooth phones found. Pair the phone in Windows Bluetooth settings first.";
                    return;
                }

                BluetoothDeviceOption selectedDevice = devices.FirstOrDefault(d =>
                    d.Name.Contains("Galaxy", StringComparison.OrdinalIgnoreCase) ||
                    d.Name.Contains("Tab", StringComparison.OrdinalIgnoreCase) ||
                    d.Address.Equals(txtAddress.Text.Trim(), StringComparison.OrdinalIgnoreCase));

                cboBluetoothDevices.SelectedItem = string.IsNullOrWhiteSpace(selectedDevice.Address)
                    ? devices[0]
                    : selectedDevice;
                txtDeviceStatus.Text = "Choose your phone from paired Bluetooth devices.";
            }
            catch (Exception ex)
            {
                cboBluetoothDevices.IsEnabled = true;
                txtDeviceStatus.Text = $"Bluetooth scan failed: {ex.Message}";
            }
            finally
            {
                _isRefreshingBluetoothDevices = false;
            }
        }

        private static IEnumerable<BluetoothDeviceOption> ParseBluetoothDevices(string output)
        {
            HashSet<string> seen = new(StringComparer.OrdinalIgnoreCase);
            Regex regex = new(@"(?:Found paired device:|\[\d+\])\s*(?<name>.*?)\s*\((?<address>[0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})\)",
                RegexOptions.Compiled);

            foreach (Match match in regex.Matches(output))
            {
                string address = match.Groups["address"].Value.ToUpperInvariant();
                if (!seen.Add(address)) continue;

                string name = match.Groups["name"].Value.Trim();
                if (string.IsNullOrWhiteSpace(name)) name = "Bluetooth device";
                yield return new BluetoothDeviceOption(name, address);
            }
        }

        private void SetStatus(string text, Brush color)
        {
            txtStatus.Text = text;
            statusDot.Fill = color;
        }

        private void SetBusy(string text)
        {
            SetStatus(text, Brushes.Orange);
            btnConnect.Content = text;
            btnConnect.IsEnabled = false;
            progressConnect.Visibility = Visibility.Visible;
            txtStats.Text = text;
        }

        private static VideoProfile GetSelectedProfile(int selectedIndex)
        {
            return selectedIndex >= 0 && selectedIndex < VideoProfiles.Length
                ? VideoProfiles[selectedIndex]
                : VideoProfiles[0];
        }

        private void ApplyVideoProfile(VideoProfile profile)
        {
            SendServiceCommand($"resolution {profile.Width} {profile.Height}");
            SendServiceCommand($"bitrate {profile.Bitrate}");
            SendServiceCommand($"fps {profile.Fps}");
            SendServiceCommand("keyframe");
        }

        private bool GetSelectedAutoFocusMode()
        {
            return cboAutoFocusMode.SelectedIndex == 1;
        }

        private void ApplyAutoFocusMode(bool enabled)
        {
            _autoFocusEnabled = enabled;
            SendServiceCommand(enabled ? "focus-on" : "focus-off");
        }

        private void OnApplyQuality(object sender, RoutedEventArgs e)
        {
            VideoProfile profile = GetSelectedProfile(cboResolution.SelectedIndex);
            if (!_isConnected)
            {
                txtStats.Text = $"{profile.Label} selected for next connection.";
                return;
            }

            ApplyVideoProfile(profile);
            txtStats.Text = $"Applied {profile.Label}.";
        }

        private void OnApplyAutoFocus(object sender, RoutedEventArgs e)
        {
            bool enabled = GetSelectedAutoFocusMode();

            if (!_isConnected)
            {
                _autoFocusEnabled = enabled;
                txtStats.Text = enabled
                    ? "Auto focus ON selected for next connection."
                    : "Auto focus OFF selected for next connection. Full scene will be shown.";
                return;
            }

            ApplyAutoFocusMode(enabled);
            txtStats.Text = enabled
                ? "Applied auto focus ON: center object focus mode."
                : "Applied auto focus OFF: full scene capture restored.";
        }

        private void UpdateAudioRouteStatus()
        {
            if (txtAudioRoute == null) return;

            if (_audioRoute is not AudioBridgeOutput route)
            {
                txtAudioRoute.Text = "Audio route: mobile mic -> Windows default output.";
                return;
            }

            txtAudioRoute.Text = string.IsNullOrWhiteSpace(route.ZoomMicrophoneName)
                ? $"Audio route: mobile mic -> {route.OutputName}."
                : $"Audio route: mobile mic -> {route.OutputName}. In Zoom choose {route.ZoomMicrophoneName} as microphone.";
        }

        private static AudioBridgeOutput? DetectAudioRoute()
        {
            var outputs = EnumerateWaveOutputDevices().ToList();

            string? speakerOutput = outputs.FirstOrDefault(name =>
                name.Contains("Speakers", StringComparison.OrdinalIgnoreCase));

            if (!string.IsNullOrWhiteSpace(speakerOutput))
            {
                return new AudioBridgeOutput(speakerOutput, "", "");
            }

            return null;
        }

        private static IEnumerable<string> EnumerateWaveOutputDevices()
        {
            uint count = waveOutGetNumDevs();
            for (uint i = 0; i < count; i++)
            {
                if (waveOutGetDevCaps(i, out WaveOutCaps caps, Marshal.SizeOf<WaveOutCaps>()) == 0 &&
                    !string.IsNullOrWhiteSpace(caps.szPname))
                {
                    yield return caps.szPname.Trim();
                }
            }
        }

        private static string QuoteArg(string value)
        {
            return $"\"{value.Replace("\"", "\\\"")}\"";
        }

        private static string ResolveServicePath()
        {
            string appDirectory = AppDomain.CurrentDomain.BaseDirectory;
            string packagedPath = System.IO.Path.Combine(appDirectory, "PhoneCamService.exe");
            if (System.IO.File.Exists(packagedPath))
            {
                return packagedPath;
            }

            string? repoPath = FindFromParents(appDirectory,
                "pc-client-windows", "build", "Release", "PhoneCamService.exe");

            return repoPath != null
                ? repoPath
                : "PhoneCamService.exe";
        }

        private static string ResolveAdbPath()
        {
            string? localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            string[] candidates =
            {
                "adb.exe",
                Path.Combine(localAppData, "Android", "Sdk", "platform-tools", "adb.exe"),
                Path.Combine(Environment.GetEnvironmentVariable("ANDROID_HOME") ?? "", "platform-tools", "adb.exe"),
                Path.Combine(Environment.GetEnvironmentVariable("ANDROID_SDK_ROOT") ?? "", "platform-tools", "adb.exe")
            };

            foreach (string candidate in candidates.Where(c => !string.IsNullOrWhiteSpace(c)))
            {
                if (File.Exists(candidate))
                {
                    return candidate;
                }
            }

            return "adb.exe";
        }

        private static string ResolveApkPath()
        {
            string appDirectory = AppDomain.CurrentDomain.BaseDirectory;
            return FindFromParents(appDirectory,
                "mobile-android", "app", "build", "outputs", "apk", "debug", "app-debug.apk")
                ?? Path.Combine(appDirectory, "app-debug.apk");
        }

        private static string? FindFromParents(string startDirectory, params string[] relativeParts)
        {
            DirectoryInfo? current = new(startDirectory);

            while (current != null)
            {
                string candidate = relativeParts.Aggregate(current.FullName, Path.Combine);
                if (File.Exists(candidate))
                {
                    return candidate;
                }

                current = current.Parent;
            }

            return null;
        }

        private static string RunProcess(string fileName, string arguments, int timeoutMs, bool ignoreErrors = false)
        {
            using Process process = new()
            {
                StartInfo = new ProcessStartInfo
                {
                    FileName = fileName,
                    Arguments = arguments,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true
                }
            };

            process.Start();
            string output = process.StandardOutput.ReadToEnd();
            string error = process.StandardError.ReadToEnd();

            if (!process.WaitForExit(timeoutMs))
            {
                try { process.Kill(); } catch { }
                throw new Exception($"{Path.GetFileName(fileName)} timed out.");
            }

            if (!ignoreErrors && process.ExitCode != 0)
            {
                string message = string.IsNullOrWhiteSpace(error) ? output : error;
                throw new Exception(message.Trim());
            }

            return string.IsNullOrWhiteSpace(output) ? error : output;
        }

        private static void StopExistingServiceProcesses()
        {
            foreach (Process process in Process.GetProcessesByName("PhoneCamService"))
            {
                try
                {
                    process.Kill();
                    process.WaitForExit(3000);
                }
                catch
                {
                    // Best effort: a stale service should not prevent the UI from trying to connect.
                }
            }
        }

        private static void StartZoom()
        {
            string zoom = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                "Zoom", "bin", "Zoom.exe");

            if (File.Exists(zoom))
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = zoom,
                    UseShellExecute = true
                });
            }
        }

        private void OnOpenZoomClick(object sender, RoutedEventArgs e)
        {
            StartZoom();
            txtStats.Text = "Zoom opened.";
        }

        /// <summary>
        /// Send a command string to PhoneCamService via its stdin.
        /// The service reads these and translates to wire protocol commands.
        /// </summary>
        private void SendServiceCommand(string command)
        {
            try
            {
                if (_serviceStdin != null && _serviceProcess != null && !_serviceProcess.HasExited)
                {
                    _serviceStdin.WriteLine(command);
                }
            }
            catch (Exception ex)
            {
                txtStats.Text = $"Command failed: {ex.Message}";
            }
        }

        private void OnFlipCamera(object sender, RoutedEventArgs e)
        {
            SendServiceCommand("flip");
            txtStats.Text = "Sent: Flip Camera";
        }

        private void OnRotateCamera(object sender, RoutedEventArgs e)
        {
            SendServiceCommand("rotate");
            txtStats.Text = "Sent: Rotate Camera";
        }

        private void OnToggleFlash(object sender, RoutedEventArgs e)
        {
            SendServiceCommand("flash");
            txtStats.Text = "Sent: Toggle Flash";
        }

        private void OnTogglePhoneSpeaker(object sender, RoutedEventArgs e)
        {
            _phoneSpeakerEnabled = !_phoneSpeakerEnabled;
            SendServiceCommand(_phoneSpeakerEnabled ? "phone-speaker-on" : "phone-speaker-off");
            btnPhoneSpeaker.Content = _phoneSpeakerEnabled ? "Stop Speaker" : "Phone Speaker";
            txtStats.Text = _phoneSpeakerEnabled
                ? "Reverse audio: PC speaker audio is playing on phone."
                : "Reverse audio: OFF";
        }

        protected override void OnClosed(EventArgs e)
        {
            Disconnect();
            base.OnClosed(e);
        }
    }
}
