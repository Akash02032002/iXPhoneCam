param(
    [switch]$OpenSetupPage
)

$ErrorActionPreference = "Stop"

$source = @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class WaveOutDevices {
    [DllImport("winmm.dll", CharSet = CharSet.Auto)]
    private static extern uint waveOutGetNumDevs();

    [DllImport("winmm.dll", CharSet = CharSet.Auto)]
    private static extern uint waveOutGetDevCaps(uint uDeviceID, out WaveOutCaps pwoc, int cbwoc);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
    private struct WaveOutCaps {
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

    public static string[] Names() {
        var names = new List<string>();
        uint count = waveOutGetNumDevs();
        for (uint i = 0; i < count; i++) {
            WaveOutCaps caps;
            if (waveOutGetDevCaps(i, out caps, Marshal.SizeOf(typeof(WaveOutCaps))) == 0 &&
                !String.IsNullOrWhiteSpace(caps.szPname)) {
                names.Add(caps.szPname);
            }
        }
        return names.ToArray();
    }
}
"@

Add-Type -TypeDefinition $source

$candidatePatterns = @(
    "CABLE Input",
    "VB-Audio",
    "VoiceMeeter Input",
    "VoiceMeeter AUX Input",
    "VoiceMeeter VAIO",
    "Virtual Cable"
)

$outputs = [WaveOutDevices]::Names()
$bridge = $outputs | Where-Object {
    $name = $_
    $candidatePatterns | Where-Object { $name -like "*$_*" }
} | Select-Object -First 1

if ($bridge) {
    $zoomMic = if ($bridge -match "CABLE Input|VB-Audio") {
        "CABLE Output"
    } elseif ($bridge -match "VoiceMeeter") {
        "VoiceMeeter Output"
    } else {
        "the matching virtual cable recording device"
    }

    Write-Host "PhoneCam Virtual Audio bridge found:"
    Write-Host "  PhoneCam output: $bridge"
    Write-Host "  Zoom microphone: $zoomMic"
    exit 0
}

Write-Host "No PhoneCam Virtual Audio bridge was found."
Write-Host "Install a signed virtual audio cable driver, then restart PhoneCamUI."
Write-Host "Recommended setup: VB-CABLE creates CABLE Input for PhoneCam and CABLE Output for Zoom."

if ($OpenSetupPage) {
    Start-Process "https://vb-audio.com/Cable/index.htm"
}

exit 1
