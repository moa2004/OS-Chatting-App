#include <windows.h>
#include <commctrl.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <shellapi.h>

#include "ui_helpers.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")

static constexpr UINT WM_APP_LOG = WM_APP + 1;
static constexpr UINT WM_APP_STATUS = WM_APP + 2;

enum class Peer { A, B };

constexpr size_t kMaxMessages = 64;
constexpr size_t kMaxText = 240;

struct ChatMessage {
    DWORD tick;
    wchar_t text[kMaxText];
};

struct SharedRegion {
    LONG headAtoB;
    LONG headBtoA;
    ChatMessage aToB[kMaxMessages];
    ChatMessage bToA[kMaxMessages];
};

struct AppState {
    HWND hwnd{};
    HWND logBox{}, inputBox{}, sendBtn{}, channelBox{}, statusLabel{}, peerARadio{}, peerBRadio{}, startBtn{}, stopBtn{};
    UiTheme theme{};
    HFONT fontSmall{}, fontMedium{}, fontLarge{}, fontMono{};
    std::atomic<bool> running{false};
    HANDLE mapHandle{nullptr};
    SharedRegion* region{nullptr};
    HANDLE semIn{nullptr};
    HANDLE semOut{nullptr};
    std::thread recvThread;
    Peer peer{Peer::A};
    long localTail{0};
    std::mutex sendMutex;
};

static LPWSTR g_shmCmdLine = nullptr;

static void PostLog(HWND hwnd, const std::wstring& text) {
    auto payload = new std::wstring(text);
    PostMessageW(hwnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(payload));
}

static void PostStatus(HWND hwnd, const std::wstring& text) {
    auto payload = new std::wstring(text);
    PostMessageW(hwnd, WM_APP_STATUS, 0, reinterpret_cast<LPARAM>(payload));
}

static std::wstring GetChannel(AppState* app) {
    std::wstring c = GetWindowTextWstr(app->channelBox);
    if (c.empty()) c = L"demo";
    return c;
}

static Peer CurrentPeer(AppState* app) {
    return (SendMessageW(app->peerARadio, BM_GETCHECK, 0, 0) == BST_CHECKED) ? Peer::A : Peer::B;
}

static void CloseHandles(AppState* app) {
    if (app->semIn) { CloseHandle(app->semIn); app->semIn = nullptr; }
    if (app->semOut) { CloseHandle(app->semOut); app->semOut = nullptr; }
    if (app->region) { UnmapViewOfFile(app->region); app->region = nullptr; }
    if (app->mapHandle) { CloseHandle(app->mapHandle); app->mapHandle = nullptr; }
}

static void StopChat(AppState* app) {
    app->running = false;
    if (app->semIn) ReleaseSemaphore(app->semIn, 1, nullptr);
    if (app->recvThread.joinable()) app->recvThread.join();
    CloseHandles(app);
    PostStatus(app->hwnd, L"Shared Memory Chat - Offline");
    EnableWindow(app->peerARadio, TRUE);
    EnableWindow(app->peerBRadio, TRUE);
    EnableWindow(app->startBtn, TRUE);
    EnableWindow(app->stopBtn, FALSE);
    EnableWindow(app->sendBtn, FALSE);
}

static void ReceiveLoop(AppState* app) {
    while (app->running) {
        DWORD wait = WaitForSingleObject(app->semIn, 200);
        if (!app->running) break;
        if (wait == WAIT_OBJECT_0) {
            long idx = app->localTail++;
            size_t slot = static_cast<size_t>(idx % kMaxMessages);
            const ChatMessage* msg = (app->peer == Peer::A)
                ? &app->region->bToA[slot]
                : &app->region->aToB[slot];
            std::wstring sender = (app->peer == Peer::A) ? L"[RX][Peer B] " : L"[RX][Peer A] ";
            PostLog(app->hwnd, sender + std::wstring(msg->text) + L"\r\n");
        }
    }
}

static void StartChat(AppState* app) {
    if (app->running) {
        PostLog(app->hwnd, L"Already running.\r\n");
        return;
    }
    app->peer = CurrentPeer(app);
    std::wstring channel = GetChannel(app);
    std::wstring base = L"Local\\ShmChat_" + channel;
    std::wstring mapName = base + L"_map";
    std::wstring semAtoB = base + L"_AtoB";
    std::wstring semBtoA = base + L"_BtoA";

    app->mapHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedRegion), mapName.c_str());
    bool existed = (GetLastError() == ERROR_ALREADY_EXISTS);
    if (!app->mapHandle) {
        PostLog(app->hwnd, L"Failed to create shared memory.");
        return;
    }
    app->region = (SharedRegion*)MapViewOfFile(app->mapHandle, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedRegion));
    if (!app->region) {
        PostLog(app->hwnd, L"MapViewOfFile failed.");
        CloseHandles(app);
        return;
    }
    if (!existed) {
        ZeroMemory(app->region, sizeof(SharedRegion));
    }

    std::wstring inName = (app->peer == Peer::A) ? semBtoA : semAtoB;
    std::wstring outName = (app->peer == Peer::A) ? semAtoB : semBtoA;
    app->semIn = CreateSemaphoreW(nullptr, 0, 1024, inName.c_str());
    app->semOut = CreateSemaphoreW(nullptr, 0, 1024, outName.c_str());
    if (!app->semIn || !app->semOut) {
        PostLog(app->hwnd, L"Failed to create semaphores.");
        CloseHandles(app);
        return;
    }

    app->localTail = 0;
    app->running = true;
    EnableWindow(app->peerARadio, FALSE);
    EnableWindow(app->peerBRadio, FALSE);
    EnableWindow(app->startBtn, FALSE);
    EnableWindow(app->stopBtn, TRUE);
    PostStatus(app->hwnd, L"Connected to channel \"" + channel + L"\" as Peer " + (app->peer == Peer::A ? L"A" : L"B"));
    PostLog(app->hwnd, L"Shared memory ready.\r\n");
    EnableWindow(app->sendBtn, TRUE);
    app->recvThread = std::thread(ReceiveLoop, app);
}

static void SendChat(AppState* app) {
    if (!app->running || !app->region) {
        PostLog(app->hwnd, L"Not connected.\r\n");
        return;
    }
    std::wstring text = GetWindowTextWstr(app->inputBox);
    if (text.empty()) return;
    if (text.size() >= kMaxText) text.resize(kMaxText - 1);

    LONG newHead;
    ChatMessage* slot;
    if (app->peer == Peer::A) {
        newHead = InterlockedIncrement(&app->region->headAtoB);
        size_t idx = static_cast<size_t>((newHead - 1) % kMaxMessages);
        slot = &app->region->aToB[idx];
    } else {
        newHead = InterlockedIncrement(&app->region->headBtoA);
        size_t idx = static_cast<size_t>((newHead - 1) % kMaxMessages);
        slot = &app->region->bToA[idx];
    }

    slot->tick = GetTickCount();
    wcsncpy_s(slot->text, text.c_str(), kMaxText - 1);
    ReleaseSemaphore(app->semOut, 1, nullptr);
    std::wstring me = (app->peer == Peer::A) ? L"[TX][Peer A] " : L"[TX][Peer B] ";
    PostLog(app->hwnd, me + text + L"\r\n");
    SetWindowTextW(app->inputBox, L"");
}

static void Layout(AppState* app, int width, int height) {
    int padding = 16;
    int headerH = 90;
    int inputH = 36;

    MoveWindow(app->statusLabel, padding, padding + 8, width - padding * 2, 30, TRUE);
    int rowY = headerH + padding;
    MoveWindow(app->peerARadio, padding, rowY, 90, 24, TRUE);
    MoveWindow(app->peerBRadio, padding + 100, rowY, 90, 24, TRUE);
    MoveWindow(app->channelBox, padding + 210, rowY, 220, 26, TRUE);
    MoveWindow(app->startBtn, padding + 440, rowY, 120, 30, TRUE);
    MoveWindow(app->stopBtn, padding + 570, rowY, 100, 30, TRUE);

    int logTop = rowY + 50;
    int logHeight = height - logTop - inputH - padding * 2;
    MoveWindow(app->logBox, padding, logTop, width - padding * 2, logHeight, TRUE);
    int inputY = logTop + logHeight + padding;
    MoveWindow(app->inputBox, padding, inputY, width - padding * 3 - 120, inputH + 8, TRUE);
    MoveWindow(app->sendBtn, width - padding - 120, inputY, 120, inputH + 8, TRUE);
}

static void InitFonts(AppState* app) {
    app->fontSmall = CreateUiFont(16, false);
    app->fontMedium = CreateUiFont(18, true);
    app->fontLarge = CreateUiFont(26, true, L"Segoe UI Semibold");
    app->fontMono = CreateFontW(16, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                FIXED_PITCH, L"Cascadia Code");
}

static void CreateUi(AppState* app) {
    app->statusLabel = CreateWindowExW(0, L"STATIC", L"Shared Memory Chat - Offline",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, app->hwnd, (HMENU)3001, nullptr, nullptr);

    app->peerARadio = CreateWindowExW(0, L"BUTTON", L"Peer A",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 0, 0, 0, 0,
        app->hwnd, (HMENU)3002, nullptr, nullptr);
    app->peerBRadio = CreateWindowExW(0, L"BUTTON", L"Peer B",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 0, 0, 0, 0,
        app->hwnd, (HMENU)3003, nullptr, nullptr);
    SendMessageW(app->peerARadio, BM_SETCHECK, BST_CHECKED, 0);

    app->channelBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"demo",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0,
        app->hwnd, (HMENU)3004, nullptr, nullptr);

    app->startBtn = CreateWindowExW(0, L"BUTTON", L"Start",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
        app->hwnd, (HMENU)3005, nullptr, nullptr);
    app->stopBtn = CreateWindowExW(0, L"BUTTON", L"Stop",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
        app->hwnd, (HMENU)3006, nullptr, nullptr);

    app->logBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        0, 0, 0, 0, app->hwnd, (HMENU)3007, nullptr, nullptr);

    app->inputBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0,
        app->hwnd, (HMENU)3008, nullptr, nullptr);
    app->sendBtn = CreateWindowExW(0, L"BUTTON", L"Send",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 0, 0,
        app->hwnd, (HMENU)3009, nullptr, nullptr);

    SetControlFont(app->statusLabel, app->fontLarge);
    SetControlFont(app->peerARadio, app->fontMedium);
    SetControlFont(app->peerBRadio, app->fontMedium);
    SetControlFont(app->channelBox, app->fontSmall);
    SetControlFont(app->startBtn, app->fontMedium);
    SetControlFont(app->stopBtn, app->fontMedium);
    SetControlFont(app->logBox, app->fontMono);
    SetControlFont(app->inputBox, app->fontSmall);
    SetControlFont(app->sendBtn, app->fontMedium);
}

static void PaintBackground(HWND hwnd, HDC hdc, AppState* app) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, app->theme.baseBrush);
    RECT header{rc.left, rc.top, rc.right, rc.top + 80};
    PaintGradientHeader(hdc, header, app->theme);
    RECT statusBg{rc.left + 12, rc.top + 12, rc.right - 12, rc.top + 52};
    DrawCard(hdc, statusBg, app->theme);
    RECT card{rc.left + 12, rc.top + 80, rc.right - 12, rc.bottom - 90};
    HPEN pen = CreatePen(PS_SOLID, 1, app->theme.accent2);
    HPEN old = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldB = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, card.left, card.top, card.right, card.bottom, 16, 16);
    SelectObject(hdc, oldB);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

static void ParseCommandLineDefaults(AppState* app, LPWSTR cmdLine) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    if (!argv) return;
    for (int i = 0; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--channel" && i + 1 < argc) {
            SetWindowTextW(app->channelBox, argv[++i]);
        } else if (arg == L"--peer" && i + 1 < argc) {
            std::wstring v = argv[++i];
            if (v == L"A" || v == L"a") {
                SendMessageW(app->peerARadio, BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(app->peerBRadio, BM_SETCHECK, BST_UNCHECKED, 0);
            } else if (v == L"B" || v == L"b") {
                SendMessageW(app->peerBRadio, BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(app->peerARadio, BM_SETCHECK, BST_UNCHECKED, 0);
            }
        }
    }
    LocalFree(argv);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = reinterpret_cast<AppState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);
        app->hwnd = hwnd;
        return TRUE;
    }
    case WM_CREATE: {
        InitFonts(app);
        CreateUi(app);
        EnableWindow(app->sendBtn, FALSE);
        ParseCommandLineDefaults(app, g_shmCmdLine ? g_shmCmdLine : GetCommandLineW());
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        Layout(app, w, h);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == 3005 && HIWORD(wParam) == BN_CLICKED) {
            StartChat(app);
        } else if (id == 3006 && HIWORD(wParam) == BN_CLICKED) {
            StopChat(app);
        } else if (id == 3009 && HIWORD(wParam) == BN_CLICKED) {
            SendChat(app);
        }
        return 0;
    }
    case WM_APP_LOG: {
        auto text = reinterpret_cast<std::wstring*>(lParam);
        if (text) {
            AppendText(app->logBox, *text);
            delete text;
        }
        return 0;
    }
    case WM_APP_STATUS: {
        auto text = reinterpret_cast<std::wstring*>(lParam);
        if (text) {
            SetWindowTextW(app->statusLabel, text->c_str());
            delete text;
        }
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, app->theme.card);
        SetTextColor(hdc, app->theme.text);
        return (LRESULT)app->theme.cardBrush;
    }
    case WM_DESTROY: {
        StopChat(app);
        PostQuitMessage(0);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintBackground(hwnd, hdc, app);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
int RunShmChat(_In_ HINSTANCE hInstance, _In_ int nCmdShow, _In_opt_ LPWSTR cmdLine) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    g_shmCmdLine = cmdLine;

    AppState app;
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"NeonShmChatWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"Shared Memory Chat",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 900, 640,
        nullptr, nullptr, hInstance, &app);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Failed to create Shared Memory Chat window.", L"Error", MB_ICONERROR | MB_TOPMOST);
        return -1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    StopChat(&app);
    return 0;
}
