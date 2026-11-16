; Inno Setup script for Smart Trackpad.
;
; Build the Release executable first, then compile this script:
;   cmake --build build --config Release
;   iscc installer\SmartTrackpad.iss
;
; The install is per user (no elevation) into %LOCALAPPDATA%\SmartTrackpad,
; which is also where the application keeps its managed copy, configuration and
; log, so an installer-managed install and a self-installed one are the same
; thing.

#define AppName "Smart Trackpad"
#define AppVersion "1.2.0"
#define AppExe "SmartTrackpad.exe"

[Setup]
AppId={{8E4C2D51-6A3B-4C7E-9A21-7B5C0F1D2A64}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppName}
DefaultDirName={localappdata}\SmartTrackpad
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=SmartTrackpad-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "startup"; Description: "Start {#AppName} when I sign in"; GroupDescription: "Startup:"
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "..\build\Release\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\{#AppName} settings"; Filename: "{app}\{#AppExe}"; Parameters: "--settings"
Name: "{group}\{#AppName} zone test"; Filename: "{app}\{#AppExe}"; Parameters: "--zone-test"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{userstartup}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: startup
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Start {#AppName} now"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; The application writes its own Startup shortcut; make sure it is gone too.
Type: files; Name: "{userstartup}\SmartTrackpad.lnk"
; Managed copies and leftovers from a self-update.
Type: files; Name: "{app}\SmartTrackpad.exe.new"
Type: files; Name: "{app}\SmartTrackpad.exe.old"
