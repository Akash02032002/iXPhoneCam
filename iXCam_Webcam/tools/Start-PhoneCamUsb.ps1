param(
    [int]$Port = 4747,
    [int]$Width = 640,
    [int]$Height = 480,
    [switch]$NoZoom,
    [switch]$NoInstall
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$adbCandidates = @(
    "adb.exe",
    "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe",
    "$env:ANDROID_HOME\platform-tools\adb.exe",
    "$env:ANDROID_SDK_ROOT\platform-tools\adb.exe"
) | Where-Object { $_ -and $_.Trim() -ne "" }

$adb = $null
foreach ($candidate in $adbCandidates) {
    $cmd = Get-Command $candidate -ErrorAction SilentlyContinue
    if ($cmd) {
        $adb = $cmd.Source
        break
    }
    if (Test-Path $candidate) {
        $adb = (Resolve-Path $candidate).Path
        break
    }
}

if (-not $adb) {
    throw "adb.exe was not found. Install Android SDK Platform Tools or add adb.exe to PATH."
}

Write-Host "Using ADB: $adb"

$devices = & $adb devices
$deviceLine = $devices | Select-String -Pattern "`tdevice$" | Select-Object -First 1
if (-not $deviceLine) {
    Write-Host ($devices -join "`n")
    throw "No authorized Android phone found. Connect USB, enable USB debugging, and accept the phone prompt."
}

$package = (& $adb shell pm list packages com.phonecam) -join ""
if ($NoInstall -and -not $package.Contains("package:com.phonecam")) {
    throw "PhoneCam is not installed on the phone."
}

if (-not $NoInstall) {
    $apk = Join-Path $root "mobile-android\app\build\outputs\apk\debug\app-debug.apk"
    if (-not (Test-Path $apk)) {
        Push-Location (Join-Path $root "mobile-android")
        try {
            & .\gradlew.bat assembleDebug
        } finally {
            Pop-Location
        }
    }
    Write-Host "Installing or updating PhoneCam APK..."
    & $adb install -r $apk | Write-Host
}

Write-Host "Granting app permissions where Android allows it..."
& $adb shell pm grant com.phonecam android.permission.CAMERA 2>$null
& $adb shell pm grant com.phonecam android.permission.RECORD_AUDIO 2>$null

Write-Host "Starting PhoneCam on the phone..."
& $adb shell am start -n com.phonecam/.ui.MainActivity --ez start_server true | Write-Host
Start-Sleep -Seconds 2

Write-Host "Forwarding USB port $Port..."
& $adb forward "tcp:$Port" "tcp:$Port" | Write-Host

$serviceExe = Join-Path $root "pc-client-windows\build\Release\PhoneCamService.exe"
if (-not (Test-Path $serviceExe)) {
    Push-Location (Join-Path $root "pc-client-windows\build")
    try {
        cmake --build . --config Release --target PhoneCamService
    } finally {
        Pop-Location
    }
}

Get-Process PhoneCamService -ErrorAction SilentlyContinue | Stop-Process -Force

$outLog = Join-Path $root "svc_out.txt"
$errLog = Join-Path $root "svc_err.txt"
$args = "--usb --port $Port --width $Width --height $Height"
Write-Host "Starting PhoneCamService $args"
Start-Process -FilePath $serviceExe `
    -ArgumentList $args `
    -WorkingDirectory (Split-Path -Parent $serviceExe) `
    -WindowStyle Hidden `
    -RedirectStandardOutput $outLog `
    -RedirectStandardError $errLog

Start-Sleep -Seconds 3
$service = Get-Process PhoneCamService -ErrorAction SilentlyContinue
if (-not $service) {
    Get-Content $outLog -Tail 80 -ErrorAction SilentlyContinue
    Get-Content $errLog -Tail 80 -ErrorAction SilentlyContinue
    throw "PhoneCamService did not stay running."
}

Write-Host "PhoneCamService is running. Select 'PhoneCam Virtual Camera' in Zoom."

if (-not $NoZoom) {
    $zoom = "$env:APPDATA\Zoom\bin\Zoom.exe"
    if (Test-Path $zoom) {
        Start-Process -FilePath $zoom
    }
}
