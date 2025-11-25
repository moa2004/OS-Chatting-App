#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <codecvt>
#include <locale>
#include <mutex>
#include <shellapi.h>

#include "ui_helpers.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")

static constexpr UINT WM_APP_LOG = WM_APP + 1;
static constexpr UINT WM_APP_CONNECTED = WM_APP + 2;

enum class Role { Server, Client };

struct AppState {
    HWND hwnd{};
    HWND logBox{}, inputBox{}, sendBtn{}, hostBox{}, portBox{}, startBtn{}, stopBtn{};
    HWND statusLabel{}, serverRadio{}, clientRadio{};
    UiTheme theme{};
    HFONT fontSmall{}, fontMedium{}, fontLarge{}, fontMono{};
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    SOCKET listenSock{INVALID_SOCKET};
    SOCKET connSock{INVALID_SOCKET};
    std::thread workerThread;
    std::thread recvThread;
    Role role{Role::Server};
    std::mutex sendMutex;
};

static LPWSTR g_socketCmdLine = nullptr;

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
    return w;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}

static void PostLog(HWND hwnd, const std::wstring& text) {
    auto payload = new std::wstring(text);
    PostMessageW(hwnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(payload));
}

static void PostConnected(HWND hwnd, bool connected) {
    PostMessageW(hwnd, WM_APP_CONNECTED, connected ? 1 : 0, 0);
}

static void CloseSocket(SOCKET& s) {
    if (s != INVALID_SOCKET) {
        closesocket(s);
        s = INVALID_SOCKET;
    }
}

static void StopNetworking(AppState* app) {
    app->running = false;
    CloseSocket(app->listenSock);
    CloseSocket(app->connSock);
    if (app->workerThread.joinable()) app->workerThread.join();
    if (app->recvThread.joinable()) app->recvThread.join();
    if (app->connected.exchange(false)) {
        PostConnected(app->hwnd, false);
    }
}

static void ReceiveLoop(AppState* app) {
    char buffer[1024];
    while (app->running && app->connSock != INVALID_SOCKET) {
        int res = recv(app->connSock, buffer, sizeof(buffer) - 1, 0);
        if (res <= 0) {
            PostLog(app->hwnd, L"[!] Disconnected.\r\n");
            app->connected = false;
            PostConnected(app->hwnd, false);
            break;
        }
        buffer[res] = '\0';
        std::string msg(buffer, res);
        std::wstring fromLabel = (app->role == Role::Server) ? L"[RX][Client] " : L"[RX][Server] ";
        PostLog(app->hwnd, fromLabel + Utf8ToWide(msg) + L"\r\n");
    }
}

static void RunServer(AppState* app, int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        PostLog(app->hwnd, L"WSAStartup failed.");
        return;
    }

    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        PostLog(app->hwnd, L"Failed to create socket.");
        WSACleanup();
        return;
    }
    app->listenSock = listenSock;

    sockaddr_in hint{};
    hint.sin_family = AF_INET;
    hint.sin_port = htons(static_cast<u_short>(port));
    hint.sin_addr.S_un.S_addr = INADDR_ANY;

    if (bind(listenSock, reinterpret_cast<sockaddr*>(&hint), sizeof(hint)) == SOCKET_ERROR) {
        PostLog(app->hwnd, L"Bind failed. Is the port in use?");
        CloseSocket(app->listenSock);
        WSACleanup();
        return;
    }

    listen(listenSock, SOMAXCONN);
    PostLog(app->hwnd, L"Listening on port " + std::to_wstring(port) + L"...\r\n");
    PostLog(app->hwnd, L"Waiting for a client to connect...\r\n");

    sockaddr_in client;
    int clientSize = sizeof(client);
    SOCKET clientSocket = accept(listenSock, reinterpret_cast<sockaddr*>(&client), &clientSize);
    if (clientSocket == INVALID_SOCKET) {
        if (app->running) PostLog(app->hwnd, L"Accept failed.");
        CloseSocket(app->listenSock);
        WSACleanup();
        return;
    }

    wchar_t host[NI_MAXHOST], svc[NI_MAXSERV];
    ZeroMemory(host, sizeof(host));
    ZeroMemory(svc, sizeof(svc));
    GetNameInfoW(reinterpret_cast<sockaddr*>(&client), sizeof(client),
                 host, NI_MAXHOST,
                 svc, NI_MAXSERV,
                 NI_NUMERICHOST | NI_NUMERICSERV);

    PostLog(app->hwnd, FormatWide(L"Connected: %s:%s\r\n", host, svc));
    app->connSock = clientSocket;
    app->connected = true;
    PostConnected(app->hwnd, true);

    app->recvThread = std::thread(ReceiveLoop, app);
    if (app->recvThread.joinable()) app->recvThread.join();
    CloseSocket(app->connSock);
    CloseSocket(app->listenSock);
    app->running = false;
    PostConnected(app->hwnd, false);
    WSACleanup();
}

static void RunClient(AppState* app, const std::wstring& host, int port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        PostLog(app->hwnd, L"WSAStartup failed.");
        return;
    }
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        PostLog(app->hwnd, L"Failed to create socket.");
        WSACleanup();
        return;
    }
    app->connSock = sock;

    sockaddr_in hint{};
    hint.sin_family = AF_INET;
    hint.sin_port = htons(static_cast<u_short>(port));

    std::string hostUtf8 = WideToUtf8(host);
    inet_pton(AF_INET, hostUtf8.c_str(), &hint.sin_addr);

    PostLog(app->hwnd, L"Connecting to " + host + L":" + std::to_wstring(port) + L"...\r\n");
    if (connect(sock, reinterpret_cast<sockaddr*>(&hint), sizeof(hint)) == SOCKET_ERROR) {
        PostLog(app->hwnd, L"Connect failed. Check IP/port.");
        CloseSocket(app->connSock);
        app->running = false;
        WSACleanup();
        return;
    }

    PostLog(app->hwnd, L"Connected!\r\n");
    app->connected = true;
    PostConnected(app->hwnd, true);
    app->recvThread = std::thread(ReceiveLoop, app);
    if (app->recvThread.joinable()) app->recvThread.join();
    CloseSocket(app->connSock);
    app->running = false;
    PostConnected(app->hwnd, false);
    WSACleanup();
}

static void SendMessageOut(AppState* app) {
    if (!app->connected || app->connSock == INVALID_SOCKET) {
        PostLog(app->hwnd, L"Not connected.\r\n");
        return;
    }
    std::wstring text = GetWindowTextWstr(app->inputBox);
    if (text.empty()) return;
    std::string payload = WideToUtf8(text);
    std::lock_guard<std::mutex> lock(app->sendMutex);
    send(app->connSock, payload.c_str(), static_cast<int>(payload.size()), 0);
    std::wstring label = (app->role == Role::Server) ? L"[TX][Server] " : L"[TX][Client] ";
    PostLog(app->hwnd, label + text + L"\r\n");
    SetWindowTextW(app->inputBox, L"");
}

static Role CurrentRole(const AppState* app) {
    return (SendMessageW(app->serverRadio, BM_GETCHECK, 0, 0) == BST_CHECKED) ? Role::Server : Role::Client;
}

static void Layout(AppState* app, int width, int height) {
    int padding = 16;
    int headerH = 90;
    int inputH = 36;

    MoveWindow(app->statusLabel, padding, padding + 8, width - padding * 2, 28, TRUE);

    int rowY = headerH + padding;
    MoveWindow(app->serverRadio, padding, rowY, 90, 24, TRUE);
    MoveWindow(app->clientRadio, padding + 100, rowY, 90, 24, TRUE);
    MoveWindow(app->hostBox, padding + 220, rowY, 200, 26, TRUE);
    MoveWindow(app->portBox, padding + 430, rowY, 80, 26, TRUE);
    MoveWindow(app->startBtn, padding + 520, rowY, 120, 30, TRUE);
    MoveWindow(app->stopBtn, padding + 650, rowY, 100, 30, TRUE);

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
    app->statusLabel = CreateWindowExW(0, L"STATIC", L"Socket Chat - Offline",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, app->hwnd, (HMENU)2001, nullptr, nullptr);

    app->serverRadio = CreateWindowExW(0, L"BUTTON", L"Server",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 0, 0, 0, 0,
        app->hwnd, (HMENU)2002, nullptr, nullptr);
    app->clientRadio = CreateWindowExW(0, L"BUTTON", L"Client",
        WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 0, 0, 0, 0,
        app->hwnd, (HMENU)2003, nullptr, nullptr);
    SendMessageW(app->serverRadio, BM_SETCHECK, BST_CHECKED, 0);

    app->hostBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"127.0.0.1",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0,
        app->hwnd, (HMENU)2004, nullptr, nullptr);
    app->portBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"54000",
        WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL, 0, 0, 0, 0,
        app->hwnd, (HMENU)2005, nullptr, nullptr);

    app->startBtn = CreateWindowExW(0, L"BUTTON", L"Start / Join",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
        app->hwnd, (HMENU)2006, nullptr, nullptr);
    app->stopBtn = CreateWindowExW(0, L"BUTTON", L"Stop",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
        app->hwnd, (HMENU)2007, nullptr, nullptr);

    app->logBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        0, 0, 0, 0, app->hwnd, (HMENU)2008, nullptr, nullptr);

    app->inputBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0,
        app->hwnd, (HMENU)2009, nullptr, nullptr);
    app->sendBtn = CreateWindowExW(0, L"BUTTON", L"Send",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 0, 0,
        app->hwnd, (HMENU)2010, nullptr, nullptr);

    SetControlFont(app->statusLabel, app->fontLarge);
    SetControlFont(app->serverRadio, app->fontMedium);
    SetControlFont(app->clientRadio, app->fontMedium);
    SetControlFont(app->hostBox, app->fontSmall);
    SetControlFont(app->portBox, app->fontSmall);
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
    HPEN pen = CreatePen(PS_SOLID, 1, app->theme.accent);
    HPEN old = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldB = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, card.left, card.top, card.right, card.bottom, 16, 16);
    SelectObject(hdc, oldB);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

static int GetPortFromUi(AppState* app) {
    std::wstring portStr = GetWindowTextWstr(app->portBox);
    int port = _wtoi(portStr.c_str());
    if (port <= 0) port = 54000;
    return port;
}

static void StartConnection(AppState* app) {
    if (app->running) {
        PostLog(app->hwnd, L"Already running.\r\n");
        return;
    }
    app->running = true;
    app->role = CurrentRole(app);
    int port = GetPortFromUi(app);
    std::wstring host = GetWindowTextWstr(app->hostBox);
    if (app->role == Role::Server) {
        app->workerThread = std::thread(RunServer, app, port);
    } else {
        app->workerThread = std::thread(RunClient, app, host, port);
    }
}

static void StopConnection(AppState* app) {
    StopNetworking(app);
}

static void ParseCommandLineDefaults(AppState* app, LPWSTR cmdLine) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    if (!argv) return;
    for (int i = 0; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--mode" && i + 1 < argc) {
            std::wstring v = argv[++i];
            if (v == L"server") {
                SendMessageW(app->serverRadio, BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(app->clientRadio, BM_SETCHECK, BST_UNCHECKED, 0);
            } else if (v == L"client") {
                SendMessageW(app->clientRadio, BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(app->serverRadio, BM_SETCHECK, BST_UNCHECKED, 0);
            }
        } else if (arg == L"--host" && i + 1 < argc) {
            SetWindowTextW(app->hostBox, argv[++i]);
        } else if (arg == L"--port" && i + 1 < argc) {
            SetWindowTextW(app->portBox, argv[++i]);
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
        ParseCommandLineDefaults(app, g_socketCmdLine ? g_socketCmdLine : GetCommandLineW());
        EnableWindow(app->sendBtn, FALSE);
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
        if (id == 2006 && HIWORD(wParam) == BN_CLICKED) {
            StartConnection(app);
        } else if (id == 2007 && HIWORD(wParam) == BN_CLICKED) {
            StopConnection(app);
        } else if (id == 2010 && HIWORD(wParam) == BN_CLICKED) {
            SendMessageOut(app);
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
    case WM_APP_CONNECTED: {
        bool connected = wParam != 0;
        SetWindowTextW(app->statusLabel, connected ? L"Socket Chat - Live" : L"Socket Chat - Offline");
        EnableWindow(app->sendBtn, connected ? TRUE : FALSE);
        InvalidateRect(hwnd, nullptr, FALSE);
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
        StopNetworking(app);
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

// Launches the socket chat UI. Called from the unified launcher.
int RunSocketChat(_In_ HINSTANCE hInstance, _In_ int nCmdShow, _In_opt_ LPWSTR cmdLine) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    g_socketCmdLine = cmdLine;

    AppState app;
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"NeonSocketChatWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, // no layered flag to ensure visibility on all systems
        wc.lpszClassName,
        L"Socket Chat (Winsock)",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 940, 680,
        nullptr, nullptr, hInstance, &app);

    if (!hwnd) {
        MessageBoxW(nullptr, L"Failed to create Socket Chat window.", L"Error", MB_ICONERROR | MB_TOPMOST);
        return -1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    StopNetworking(&app);
    return 0;
}
