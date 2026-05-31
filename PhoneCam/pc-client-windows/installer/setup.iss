; PhoneCam Windows Installer Script (Inno Setup)
; Download Inno Setup: https://jrsoftware.org/isinfo.php

[Setup]
AppName=PhoneCam
AppVersion=1.0.0
AppPublisher=PhoneCam
DefaultDirName={autopf}\PhoneCam
DefaultGroupName=PhoneCam
OutputDir=..\..\dist
OutputBaseFilename=PhoneCamSetup-PC
Compression=lzma
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayIcon={app}\PhoneCamUI.exe
LicenseFile=LICENSE.txt
PrivilegesRequired=admin
DisableProgramGroupPage=yes

[Types]
Name: "full"; Description: "Full installation"
Name: "compact"; Description: "Compact installation"

[Components]
Name: "main"; Description: "PhoneCam Application"; Types: full compact; Flags: fixed
Name: "driver"; Description: "Virtual Camera Driver"; Types: full

[Tasks]
Name: "desktopicon"; Description: "Create a PhoneCam desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: checkedonce

[Files]
; Self-contained Windows application package
Source: "..\..\dist\PhoneCam-PC-SelfContained\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\PhoneCam"; Filename: "{app}\PhoneCamUI.exe"
Name: "{group}\Uninstall PhoneCam"; Filename: "{uninstallexe}"
Name: "{autodesktop}\PhoneCam"; Filename: "{app}\PhoneCamUI.exe"; Tasks: desktopicon

[Run]
; Register DirectShow filter
Filename: "regsvr32.exe"; Parameters: "/s ""{app}\PhoneCamDriver.dll"""; \
    StatusMsg: "Registering virtual camera driver..."; Components: driver; \
    Flags: runhidden
Filename: "{app}\PhoneCamUI.exe"; Description: "Launch PhoneCam"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Unregister DirectShow filter
Filename: "regsvr32.exe"; Parameters: "/u /s ""{app}\PhoneCamDriver.dll"""; \
    Flags: runhidden
