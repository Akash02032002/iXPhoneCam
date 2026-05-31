using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
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
        private bool _audioEnabled = true;
        private bool _phoneSpeakerEnabled;
        private System.Windows.Threading.DispatcherTimer? _zoomTimer;
        private System.Windows.Threading.DispatcherTimer? _bitrateTimer;

        private readonly record struct Resolution(int Width, int Height);

        private static readonly Resolution[] Resolutions =
        {
            new(640, 480),
            new(1280, 720),
            new(1920, 1080)
        };

        public MainWindow()
        {
            InitializeComponent();
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
            }
            else if (rbWifi?.IsChecked == true)
            {
                txtAddressLabel.Text = "Phone WiFi IP:";
                txtAddress.Text = "";
                txtAddress.IsEnabled = true;
            }
            else if (rbBluetooth?.IsChecked == true)
            {
                txtAddressLabel.Text = "Bluetooth MAC (AA:BB:CC:DD:EE:FF):";
                txtAddress.Text = "";
                txtAddress.IsEnabled = true;
            }
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

            Resolution resolution = GetSelectedResolution(cboResolution.SelectedIndex);

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

            string micOutput = txtMicOutput.Text.Trim();
            string micOutputArg = string.IsNullOrWhiteSpace(micOutput)
                ? ""
                : $" --phone-mic-output \"{micOutput.Replace("\"", "\\\"")}\"";
            string args = $"{mode} --port {port} --width {resolution.Width} --height {resolution.Height}{micOutputArg}";

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

                _isConnected = true;
                btnConnect.Content = "Disconnect";
                btnConnect.IsEnabled = true;
                progressConnect.Visibility = Visibility.Collapsed;
                grpControls.IsEnabled = true;
                grpRemote.IsEnabled = true;
                SetStatus("Connected", Brushes.LimeGreen);
                txtStats.Text = "Streaming to PhoneCam Virtual Camera. Select it in Zoom.";
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
            string? deviceLine = devices.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries)
                .FirstOrDefault(line => line.EndsWith("\tdevice", StringComparison.OrdinalIgnoreCase));

            if (deviceLine == null)
            {
                if (devices.Contains("unauthorized", StringComparison.OrdinalIgnoreCase))
                {
                    throw new Exception("Phone is connected but not authorized. Accept the USB debugging prompt on the phone.");
                }

                throw new Exception("No USB phone found. Connect the phone with USB debugging enabled.");
            }

            string serial = deviceLine.Split('\t')[0].Trim();
            string apk = ResolveApkPath();
            if (File.Exists(apk))
            {
                RunProcess(adb, $"install -r \"{apk}\"", 45000);
            }

            RunProcess(adb, "shell pm grant com.phonecam android.permission.CAMERA", 8000, ignoreErrors: true);
            RunProcess(adb, "shell pm grant com.phonecam android.permission.RECORD_AUDIO", 8000, ignoreErrors: true);
            RunProcess(adb, "shell am start -n com.phonecam/.ui.MainActivity --ez start_server true", 10000);
            RunProcess(adb, $"forward tcp:{port} tcp:{port}", 8000, ignoreErrors: true);

            return $"Phone ready over USB ({serial}).";
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
            _audioEnabled = true;
            _phoneSpeakerEnabled = false;

            btnConnect.Content = "Connect Phone";
            btnConnect.IsEnabled = true;
            progressConnect.Visibility = Visibility.Collapsed;
            grpControls.IsEnabled = false;
            grpRemote.IsEnabled = false;
            if (btnAudioToggle != null) btnAudioToggle.Content = "Mute Audio";
            if (btnPhoneSpeaker != null) btnPhoneSpeaker.Content = "Phone Speaker";
            SetStatus("Disconnected", Brushes.Red);
            txtStats.Text = "Ready";
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

        private static Resolution GetSelectedResolution(int selectedIndex)
        {
            return selectedIndex >= 0 && selectedIndex < Resolutions.Length
                ? Resolutions[selectedIndex]
                : Resolutions[0];
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

        private void OnAutoFocus(object sender, RoutedEventArgs e)
        {
            SendServiceCommand("focus");
            txtStats.Text = "Sent: Auto Focus";
        }

        private void OnToggleAudio(object sender, RoutedEventArgs e)
        {
            _audioEnabled = !_audioEnabled;
            SendServiceCommand(_audioEnabled ? "audio-on" : "audio-off");
            btnAudioToggle.Content = _audioEnabled ? "Mute Audio" : "Unmute Audio";
            txtStats.Text = _audioEnabled ? "Audio: ON" : "Audio: OFF";
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

        private void OnRequestKeyframe(object sender, RoutedEventArgs e)
        {
            SendServiceCommand("keyframe");
            txtStats.Text = "Sent: Request Keyframe";
        }

        private void OnZoomChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (txtZoom == null) return;
            double zoomRatio = sldZoom.Value / 100.0;
            txtZoom.Text = $"{zoomRatio:F1}x";

            // Debounce: only send after user stops sliding for 200ms
            _zoomTimer?.Stop();
            _zoomTimer = new System.Windows.Threading.DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(200)
            };
            _zoomTimer.Tick += (s, args) =>
            {
                _zoomTimer?.Stop();
                int zoomX100 = (int)sldZoom.Value;
                SendServiceCommand($"zoom {zoomX100}");
                txtStats.Text = $"Sent: Zoom {zoomRatio:F1}x";
            };
            _zoomTimer.Start();
        }

        private void OnBitrateChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (txtBitrate == null) return;
            int kbps = (int)sldBitrate.Value;
            txtBitrate.Text = $"{kbps} kbps";

            // Debounce: only send after user stops sliding for 300ms
            _bitrateTimer?.Stop();
            _bitrateTimer = new System.Windows.Threading.DispatcherTimer
            {
                Interval = TimeSpan.FromMilliseconds(300)
            };
            _bitrateTimer.Tick += (s, args) =>
            {
                _bitrateTimer?.Stop();
                int bitrate = (int)sldBitrate.Value * 1000; // kbps to bps
                SendServiceCommand($"bitrate {bitrate}");
                txtStats.Text = $"Sent: Bitrate {kbps} kbps";
            };
            _bitrateTimer.Start();
        }

        private void OnApplyResolution(object sender, RoutedEventArgs e)
        {
            Resolution resolution = GetSelectedResolution(cboLiveResolution.SelectedIndex);
            SendServiceCommand($"resolution {resolution.Width} {resolution.Height}");
            txtStats.Text = $"Sent: Resolution {resolution.Width}x{resolution.Height}";
        }

        protected override void OnClosed(EventArgs e)
        {
            Disconnect();
            base.OnClosed(e);
        }
    }
}
