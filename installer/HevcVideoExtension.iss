; Inno Setup installer for the Windows HEVC Media Foundation extension.
; The GitHub Actions workflow sets PACKAGE_DIR to the assembled package folder.

#define PackageDir GetEnv("PACKAGE_DIR")

[Setup]
AppId={{7E9F9CB7-4E0A-4E33-98EA-3F4C3E7E8FA5}
AppName=HEVC Video Extension
AppVersion=0.1.0
AppPublisher=hangxxchang-sketch
DefaultDirName={autopf}\HEVC Video Extension
DefaultGroupName=HEVC Video Extension
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
DisableProgramGroupPage=yes
Uninstallable=yes
OutputBaseFilename=HevcVideoExtension-Setup-windows-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "{#PackageDir}\bin\*"; DestDir: "{app}\bin"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackageDir}\scripts\*"; DestDir: "{app}\scripts"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PackageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Run]
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\bin\HevcVideoExtension.dll"""; StatusMsg: "Registering HEVC Media Foundation decoder"; Flags: runhidden waituntilterminated

[UninstallRun]
Filename: "{sys}\regsvr32.exe"; Parameters: "/s /u ""{app}\bin\HevcVideoExtension.dll"""; RunOnceId: "UnregisterHEVC"; Flags: runhidden waituntilterminated
