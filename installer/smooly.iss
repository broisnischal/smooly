; Inno Setup script for smooly. Compile with ISCC:  iscc installer\smooly.iss
; Pass the version on the command line:  iscc /DAppVersion=1.0.0 installer\smooly.iss

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define AppName "smooly"
#define AppPublisher "Nischal Dahal"
#define AppExe "smooly.exe"
#define AppUrl "https://github.com/nischal-dahal/smooly"

[Setup]
AppId={{B2E9A6C4-3F1D-4A8E-9C7B-2D5E1F0A6B33}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
DefaultDirName={autopf}\smooly
DefaultGroupName=smooly
DisableProgramGroupPage=yes
DisableDirPage=auto
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName=smooly
OutputDir=..\dist
OutputBaseFilename=smooly-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
CloseApplications=force
MinVersion=10.0

[Files]
Source: "..\smooly.exe";  DestDir: "{app}"; Flags: ignoreversion
Source: "..\cursors\*";   DestDir: "{app}\cursors"; Flags: recursesubdirs ignoreversion
Source: "..\fonts\*";     DestDir: "{app}\fonts";   Flags: recursesubdirs ignoreversion
Source: "..\README.md";   DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\LICENSE";     DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\smooly";            Filename: "{app}\{#AppExe}"
Name: "{autostartup}\smooly";      Filename: "{app}\{#AppExe}"; Tasks: startup

[Tasks]
Name: "startup"; Description: "Start smooly automatically when Windows starts"; GroupDescription: "Startup:"

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch smooly now"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{userappdata}\smooly"
