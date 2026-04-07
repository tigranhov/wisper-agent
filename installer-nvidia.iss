[Setup]
AppName=Wisper Agent
AppVersion=0.3.5
AppPublisher=tigranhov
AppPublisherURL=https://github.com/tigranhov/wisper-agent
DefaultDirName={autopf}\Wisper Agent
DefaultGroupName=Wisper Agent
UninstallDisplayIcon={app}\wisper-agent.exe
OutputDir=release
OutputBaseFilename=wisper-agent-nvidia-setup-win64
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
SetupIconFile=assets\icons\icon.ico

[Files]
; Main application
Source: "build\Release\wisper-agent.exe"; DestDir: "{app}"; Flags: ignoreversion
; CPU whisper (fallback) — shared DLLs from CPU build (no cuBLAS dependency)
Source: "bin\Release\whisper-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\Release\whisper.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\Release\ggml.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\Release\ggml-cpu.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\Release\ggml-base.dll"; DestDir: "{app}"; Flags: ignoreversion
; CUDA whisper (GPU accelerated — ggml-cuda.dll plugin provides GPU backend)
Source: "bin\cuda\whisper-cli.exe"; DestDir: "{app}"; DestName: "whisper-cli-cuda.exe"; Flags: ignoreversion
Source: "bin\cuda\ggml-cuda.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\cuda\cudart64_12.dll"; DestDir: "{app}"; Flags: ignoreversion
; Icons
Source: "assets\icons\*.ico"; DestDir: "{app}\assets\icons"; Flags: ignoreversion

[Icons]
Name: "{group}\Wisper Agent"; Filename: "{app}\wisper-agent.exe"
Name: "{group}\Uninstall Wisper Agent"; Filename: "{uninstallexe}"

[Tasks]
Name: "startup"; Description: "Start Wisper Agent on Windows login"; GroupDescription: "Additional options:"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "WisperAgent"; ValueData: """{app}\wisper-agent.exe"""; Flags: uninsdeletevalue; Tasks: startup

[Run]
Filename: "{app}\wisper-agent.exe"; Description: "Launch Wisper Agent"; Flags: nowait postinstall skipifsilent
