# OS Chatting App (Win32/C++)

Interactive Windows GUI that lets you chat in two modes from a single launcher:
- **Socket Chat (Winsock)**: server/client over TCP to other machines or the same host.
- **Shared Memory Chat**: ultra-fast local chat using a memory-mapped file + named semaphores (no network).

No external UI toolkit; everything is plain Win32 with a custom neon look.

## Build
Requirements: Windows, CMake, and either MSVC (recommended) or MinGW.

MSVC example (x64 Release):
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

MinGW example (static runtime to avoid missing DLLs):
```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release
```
> CMakeLists already adds `-static -static-libgcc -static-libstdc++` when using MinGW.

Artifact:
- MSVC: `build/Release/chat_app.exe`
- MinGW: `build/chat_app.exe`

## Run (Launcher)
Just run the exe with no args and pick the mode from the UI:
```powershell
build/Release/chat_app.exe
```

## Run (direct CLI)
Socket server:
```powershell
chat_app.exe --engine socket --mode server --port 54000
```
Socket client:
```powershell
chat_app.exe --engine socket --mode client --host 127.0.0.1 --port 54000
```
Shared memory (same machine):
```powershell
chat_app.exe --engine shm --channel demo --peer A
chat_app.exe --engine shm --channel demo --peer B
```

## How to Test Chatting
### Socket mode (needs 2 instances)
1) Start Instance #1 → choose **Server** → `Start / Join` (waits on the port).
2) Start Instance #2 → choose **Client**, set `host` (use `127.0.0.1` if same PC) and same `port` → `Start / Join`.
3) When status shows **Live**, the Send button lights up. Exchange messages; logs show `[TX]/[RX]` with Server/Client labels.

If you see "Connect failed": ensure a server instance is running, ports match, and firewall allows loopback; try a different port if 54000 is busy.

### Shared memory mode (needs 2 instances on same PC)
1) Use the same `channel` name in both instances.
2) One picks **Peer A**, the other **Peer B** → `Start` in both.
3) Send messages; logs show `[TX]/[RX]` with Peer A/B labels. To switch peers, press `Stop` first, change the radio, then `Start`.

## Notes
- GUI is all Win32 (no Qt/.NET). Fonts/colors live in `ui_helpers.h`.
- Socket chat threads: one worker (server/client) + one receiver; UI updated via `WM_APP` messages.
- Shared memory uses a mapped file + two semaphores (A→B, B→A) with per-direction ring buffers, avoiding busy-wait.
- Sends are disabled until a connection/session is active to prevent "Not connected" spam.

## Troubleshooting
- **Send button disabled**: the app isn't connected yet. Establish Server/Client (socket) or press Start (shared memory).
- **Connect failed**: start the Server first, verify host/port, and allow the app through firewall; try a different port.
- **Switching Peer A/B**: stop the session first, then change the radio and start again.
