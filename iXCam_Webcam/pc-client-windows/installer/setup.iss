; PhoneCam Windows Installer Script (Inno Setup)
; Download Inno Setup: https://jrsoftware.org/isinfo.php

[Setup]
AppName=PhoneCam
AppVersion=1.0.0
AppPublisher=PhoneCam
DefaultDirName={autopf}\PhoneCam
DefaultGroupName=PhoneCam
OutputBaseFilename=PhoneCamSetup
Compression=lzma
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupIconFile=..\ui\PhoneCamUI\phonecam.ico
UninstallDisplayIcon={app}\PhoneCamUI.exe

[Types]
Name: "full"; Description: "Full installation"
Name: "compact"; Description: "Compact installation"

[Components]
Name: "main"; Description: "PhoneCam Application"; Types: full compact; Flags: fixed
Name: "driver"; Description: "Virtual Camera Driver"; Types: full

[Files]
; Main application
Source: "..\build\Release\PhoneCamService.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\ui\PhoneCamUI\bin\Release\net8.0-windows\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

; Virtual camera driver
Source: "..\build\Release\PhoneCamDriver.dll"; DestDir: "{app}"; Components: driver; Flags: ignoreversion regserver

; FFmpeg DLLs
Source: "C:\ffmpeg\bin\avcodec-*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\ffmpeg\bin\avformat-*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\ffmpeg\bin\avutil-*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\ffmpeg\bin\swscale-*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "C:\ffmpeg\bin\swresample-*.dll"; DestDir: "{app}"; Flags: ignoreversion

; ADB
Source: "adb\*"; DestDir: "{app}\adb"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\PhoneCam"; Filename: "{app}\PhoneCamUI.exe"
Name: "{group}\Uninstall PhoneCam"; Filename: "{uninstallexe}"
Name: "{autodesktop}\PhoneCam"; Filename: "{app}\PhoneCamUI.exe"

[Run]
; Register DirectShow filter
Filename: "regsvr32.exe"; Parameters: "/s ""{app}\PhoneCamDriver.dll"""; \
    StatusMsg: "Registering virtual camera driver..."; Components: driver; \
    Flags: runhidden

[UninstallRun]
; Unregister DirectShow filter
Filename: "regsvr32.exe"; Parameters: "/u /s ""{app}\PhoneCamDriver.dll"""; \
    Flags: runhidden

[Registry]
; Add app to PATH for ADB access
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; \
    ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}\adb"; \
    Check: NeedsAddPath('{app}\adb')

[Code]
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if not RegQueryStringValue(HKEY_LOCAL_MACHINE,
    'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
    'Path', OrigPath)
  then begin
    Result := True;
    exit;
  end;
  Result := Pos(';' + Param + ';', ';' + OrigPath + ';') = 0;
end;
