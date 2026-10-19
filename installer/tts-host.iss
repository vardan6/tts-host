; Per-user offline Windows installer (docs/design/architecture.md#distribution,
; ADR 0010). Installs the release tree, minus portable.marker, into
; %LOCALAPPDATA%\Programs\TTS Host without elevation. Bundled models stay in
; the read-only application models\ folder; downloads, updates, and imports
; live in %LOCALAPPDATA%\TTS Host\models and survive upgrades and uninstall.
; scripts/release-win.ps1 compiles it:
;   ISCC /DSourceDir=<release tree> /DAppVersion=<x.y.z> /DOutputDir=<dir>
;        /DLaunchOnLoginDefault=<0|1> installer\tts-host.iss

#ifndef SourceDir
  #error Define SourceDir as the installed release tree
#endif
#ifndef AppVersion
  #error Define AppVersion
#endif
#ifndef OutputDir
  #define OutputDir "."
#endif
; config.json's startup.launchOnLogin, the pre-ticked default (Login startup).
#ifndef LaunchOnLoginDefault
  #define LaunchOnLoginDefault 1
#endif

[Setup]
AppId={{6F1D3A52-8C4B-4E57-9B0E-2D7A41C9E8B3}
AppName=TTS Host
AppVersion={#AppVersion}
AppPublisher=TTS Host
DefaultDirName={userpf}\TTS Host
DefaultGroupName=TTS Host
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=tts-host-{#AppVersion}-setup
SetupIconFile=..\resources\tts-host.ico
UninstallDisplayIcon={app}\tts-host.exe
LicenseFile={#SourceDir}\licenses\TTS-Host-LICENSE.txt
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
CloseApplications=yes
RestartApplications=no
WizardStyle=modern

[Tasks]
#if LaunchOnLoginDefault
Name: startup; Description: "Start TTS Host when I sign in to Windows"
#else
Name: startup; Description: "Start TTS Host when I sign in to Windows"; Flags: unchecked
#endif

[InstallDelete]
; An upgrade replaces the application tree wholesale, so files a newer release
; dropped do not linger. Per-user data is never touched.
Type: filesandordirs; Name: "{app}\runners"
Type: filesandordirs; Name: "{app}\models"
Type: filesandordirs; Name: "{app}\schemas"
Type: filesandordirs; Name: "{app}\licenses"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Excludes: "portable.marker"; Flags: ignoreversion recursesubdirs createallsubdirs
; Installed mode reads its configuration from the per-user data directory,
; with a matching schemas\ beside it (docs/design/architecture.md#configuration).
; An existing configuration is kept on upgrade and uninstall.
Source: "{#SourceDir}\config.json"; DestDir: "{localappdata}\TTS Host"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#SourceDir}\schemas\*"; DestDir: "{localappdata}\TTS Host\schemas"; Flags: ignoreversion uninsneveruninstall

[Dirs]
Name: "{localappdata}\TTS Host\models"; Flags: uninsneveruninstall

[Icons]
Name: "{group}\TTS Host"; Filename: "{app}\tts-host.exe"
Name: "{group}\TTS Host Settings"; Filename: "{app}\tts-host.exe"; Parameters: "--settings"

[Registry]
; The same Run entry and command the Host's own startup switch writes
; (src/startup_registration.cpp), so Settings shows it as on for this copy.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "TTS Host"; ValueData: """{app}\tts-host.exe"" --config ""{localappdata}\TTS Host\config.json"""; Tasks: startup

[Run]
Filename: "{app}\tts-host.exe"; Description: "Start TTS Host"; Flags: nowait postinstall skipifsilent

[Code]
const
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';
  RunValue = 'TTS Host';

// The installer asks the first-run startup question, so record its answer
// the way the Host does (src/startup_choice.cpp) and the Host never asks again.
procedure CurStepChanged(CurStep: TSetupStep);
var
  Answer: String;
begin
  if CurStep = ssPostInstall then
  begin
    if WizardIsTaskSelected('startup') then Answer := 'launchOnLogin' else Answer := 'declined';
    ForceDirectories(ExpandConstant('{localappdata}\TTS Host'));
    SaveStringToFile(ExpandConstant('{localappdata}\TTS Host\startup-choice.marker'), Answer + #10, False);
  end;
end;

// Uninstall removes the Run entry only when it starts this copy, whether the
// installer or the Host's Settings wrote it; another copy's entry stays.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Command: String;
begin
  if CurUninstallStep = usUninstall then
    if RegQueryStringValue(HKCU, RunKey, RunValue, Command) then
      if Pos(Lowercase('"' + ExpandConstant('{app}\tts-host.exe') + '"'), Lowercase(Command)) = 1 then
        RegDeleteValue(HKCU, RunKey, RunValue);
end;
