[Setup]
AppName=Wisper Agent
AppVersion=0.3.1
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
; CPU whisper (fallback)
Source: "bin\Release\whisper-cli.exe"; DestDir: "{app}"; Flags: ignoreversion
; CUDA whisper (GPU accelerated — cuBLAS DLLs loaded from user's NVIDIA driver)
Source: "bin\cuda\whisper-cli.exe"; DestDir: "{app}"; DestName: "whisper-cli-cuda.exe"; Flags: ignoreversion
Source: "bin\cuda\ggml-cuda.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\cuda\cudart64_12.dll"; DestDir: "{app}"; Flags: ignoreversion
; Shared DLLs from CUDA build (includes GPU code paths, falls back to CPU)
Source: "bin\cuda\whisper.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\cuda\ggml.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\cuda\ggml-cpu.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "bin\cuda\ggml-base.dll"; DestDir: "{app}"; Flags: ignoreversion
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
