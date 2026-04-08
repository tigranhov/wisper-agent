[Setup]
AppName=SpeakInto
AppVersion=0.3.7
AppPublisher=tigranhov
AppPublisherURL=https://github.com/tigranhov/speakinto
DefaultDirName={autopf}\SpeakInto
DefaultGroupName=SpeakInto
UninstallDisplayIcon={app}\speakinto.exe
OutputDir=release
OutputBaseFilename=speakinto-setup-universal-win64
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
SetupIconFile=assets\icons\icon.ico

[Files]
Source: "build\Release\speakinto.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\Release\whisper-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\Release\whisper.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\Release\ggml.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\Release\ggml-cpu.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\Release\ggml-base.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "assets\icons\*.ico"; DestDir: "{app}\assets\icons"; Flags: ignoreversion

[Icons]
Name: "{group}\SpeakInto"; Filename: "{app}\speakinto.exe"
Name: "{group}\Uninstall SpeakInto"; Filename: "{uninstallexe}"

[Tasks]
Name: "startup"; Description: "Start SpeakInto on Windows login"; GroupDescription: "Additional options:"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "SpeakInto"; ValueData: """{app}\speakinto.exe"""; Flags: uninsdeletevalue; Tasks: startup

[Run]
Filename: "{app}\speakinto.exe"; Description: "Launch SpeakInto"; Flags: nowait postinstall skipifsilent
