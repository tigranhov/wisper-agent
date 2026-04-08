# Build Instructions

Before every build, kill any running speakinto process to avoid linker errors (the exe is locked while running):

```bash
MSYS_NO_PATHCONV=1 /c/WINDOWS/system32/taskkill.exe /f /im speakinto.exe 2>/dev/null
```

Build commands:

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
