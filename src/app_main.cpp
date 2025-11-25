#include <windows.h>
#include <commctrl.h>
#include <string>
#include <thread>
#include <algorithm>

#include "ui_helpers.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")

int RunSocketChat(_In_ HINSTANCE hInstance, _In_ int nCmdShow, _In_opt_ LPWSTR cmdLine);
int RunShmChat(_In_ HINSTANCE hInstance, _In_ int nCmdShow, _In_opt_ LPWSTR cmdLine);

enum : UINT {
    ID_SOCKET_BTN = 5001,
    ID_SHM_BTN = 5002
};

enum class LaunchTarget { None, Socket, Shm };
static LaunchTarget g_target = LaunchTarget::None;

struct LauncherState {
    HWND hwnd{};
    HWND title{}, subtitle{}, tagline{}, socketBtn{}, shmBtn{};
    UiTheme theme{};
    HFONT titleFont{}, subtitleFont{}, bodyFont{}, btnFont{};
};

static void InitFonts(LauncherState* state) {
    state->titleFont = CreateUiFont(34, true, L"Segoe UI Black");
    state->subtitleFont = CreateUiFont(22, true, L"Segoe UI Semibold");
    state->bodyFont = CreateUiFont(16, false);
    state->btnFont = CreateUiFont(18, true);
}

static void SetFonts(const LauncherState* state) {
    SetControlFont(state->title, state->titleFont);
    SetControlFont(state->subtitle, state->subtitleFont);
    SetControlFont(state->tagline, state->bodyFont);
    SetControlFont(state->socketBtn, state->btnFont);
    SetControlFont(state->shmBtn, state->btnFont);
}

static void CreateLauncherUi(LauncherState* state) {
    state->title = CreateWindowExW(0, L"STATIC", L"OS Chat Studio",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, state->hwnd, (HMENU)4101, nullptr, nullptr);

    state->subtitle = CreateWindowExW(0, L"STATIC",
        L"Choose your engine and start chatting instantly.",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, state->hwnd, (HMENU)4102, nullptr, nullptr);

    state->tagline = CreateWindowExW(0, L"STATIC",
        L"Socket Chat: cross-device over TCP.\r\nShared Memory Chat: blazing fast on the same machine with full sync.",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, state->hwnd, (HMENU)4103, nullptr, nullptr);

    state->socketBtn = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_PUSHBUTTON, 0, 0, 0, 0, state->hwnd, (HMENU)ID_SOCKET_BTN, nullptr, nullptr);
    state->shmBtn = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_PUSHBUTTON, 0, 0, 0, 0, state->hwnd, (HMENU)ID_SHM_BTN, nullptr, nullptr);

    InitFonts(state);
    SetFonts(state);
}

static void LayoutLauncher(LauncherState* state, int width, int height) {
    int padding = 26;
    int cardHeight = 230;
    int heroTop = padding + 30;

    MoveWindow(state->title, padding, heroTop, width - padding * 2, 44, TRUE);
    MoveWindow(state->subtitle, padding, heroTop + 48, width - padding * 2, 36, TRUE);
    MoveWindow(state->tagline, padding, heroTop + 84, width - padding * 2, 60, TRUE);

    int cardTop = heroTop + 170;
    int cardWidth = (width - padding * 3) / 2;
    MoveWindow(state->socketBtn, padding, cardTop, cardWidth, cardHeight, TRUE);
    MoveWindow(state->shmBtn, padding * 2 + cardWidth, cardTop, cardWidth, cardHeight, TRUE);
}

static void PaintLauncherBackground(HWND hwnd, HDC hdc, LauncherState* state) {
    RECT rc;
    GetClientRect(hwnd, &rc);

    TRIVERTEX vert[2];
    vert[0].x = rc.left;
    vert[0].y = rc.top;
    vert[0].Red = 0x0500;
    vert[0].Green = 0x0A00;
    vert[0].Blue = 0x1800;
    vert[0].Alpha = 0xFFFF;

    vert[1].x = rc.right;
    vert[1].y = rc.bottom;
    vert[1].Red = GetRValue(state->theme.base) << 8;
    vert[1].Green = (GetGValue(state->theme.base) + 20) << 8;
    vert[1].Blue = (GetBValue(state->theme.base) + 16) << 8;
    vert[1].Alpha = 0xFFFF;

    GRADIENT_RECT gRect = {0, 1};
    GradientFill(hdc, vert, 2, &gRect, 1, GRADIENT_FILL_RECT_V);

    RECT glass{ rc.left + 18, rc.top + 90, rc.right - 18, rc.bottom - 24 };
    HBRUSH glassBrush = CreateSolidBrush(RGB(26, 34, 58));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(50, 82, 140));
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, glassBrush);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, glass.left, glass.top, glass.right, glass.bottom, 18, 18);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(glassBrush);
    DeleteObject(pen);
}

static void DrawCardButton(LPDRAWITEMSTRUCT dis, LauncherState* state, bool primary) {
    HDC hdc = dis->hDC;
    RECT r = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;

    COLORREF start = primary ? state->theme.accent : state->theme.accent2;
    COLORREF end = RGB(40, 54, 92);
    auto clampColor = [](int v) { return std::clamp(v, 0, 255); };
    if (pressed) {
        start = RGB(
            clampColor((int)GetRValue(start) - 20),
            clampColor((int)GetGValue(start) - 20),
            clampColor((int)GetBValue(start) - 20));
    } else if (hover) {
        end = RGB(
            clampColor((int)GetRValue(end) + 14),
            clampColor((int)GetGValue(end) + 14),
            clampColor((int)GetBValue(end) + 14));
    }

    TRIVERTEX v[2];
    v[0].x = r.left;
    v[0].y = r.top;
    v[0].Red = GetRValue(start) << 8;
    v[0].Green = GetGValue(start) << 8;
    v[0].Blue = GetBValue(start) << 8;
    v[0].Alpha = 0xFFFF;
    v[1].x = r.right;
    v[1].y = r.bottom;
    v[1].Red = GetRValue(end) << 8;
    v[1].Green = GetGValue(end) << 8;
    v[1].Blue = GetBValue(end) << 8;
    v[1].Alpha = 0xFFFF;

    GRADIENT_RECT rect = {0, 1};
    GradientFill(hdc, v, 2, &rect, 1, GRADIENT_FILL_RECT_V);

    RECT inner = r;
    InflateRect(&inner, -12, -12);
    HPEN border = CreatePen(PS_SOLID, 2, primary ? state->theme.accent : state->theme.accent2);
    HPEN oldPen = (HPEN)SelectObject(hdc, border);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, inner.left, inner.top, inner.right, inner.bottom, 16, 16);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(border);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, state->theme.text);
    HFONT oldFont = (HFONT)SelectObject(hdc, state->btnFont);
    std::wstring header = primary ? L"Socket Chat" : L"Shared Memory Chat";
    std::wstring sub = primary ? L"Connect devices over TCP." : L"Instant messaging on the same machine.";

    RECT textRc = inner;
    textRc.top += 28;
    DrawTextW(hdc, header.c_str(), -1, &textRc, DT_CENTER | DT_TOP | DT_SINGLELINE);
    textRc.top += 34;
    SelectObject(hdc, state->bodyFont);
    DrawTextW(hdc, sub.c_str(), -1, &textRc, DT_CENTER | DT_TOP | DT_WORDBREAK);
    SelectObject(hdc, oldFont);
}

static void LaunchSocket(LauncherState* state) {
    EnableWindow(state->socketBtn, FALSE);
    EnableWindow(state->shmBtn, FALSE);
    g_target = LaunchTarget::Socket;
    DestroyWindow(state->hwnd);
}

static void LaunchShm(LauncherState* state) {
    EnableWindow(state->socketBtn, FALSE);
    EnableWindow(state->shmBtn, FALSE);
    g_target = LaunchTarget::Shm;
    DestroyWindow(state->hwnd);
}

static LRESULT CALLBACK LauncherWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LauncherState* state = reinterpret_cast<LauncherState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<LauncherState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        state->hwnd = hwnd;
        return TRUE;
    }
    case WM_CREATE: {
        CreateLauncherUi(state);
        return 0;
    }
    case WM_SIZE: {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        LayoutLauncher(state, w, h);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_SOCKET_BTN && HIWORD(wParam) == BN_CLICKED) {
            LaunchSocket(state);
        } else if (id == ID_SHM_BTN && HIWORD(wParam) == BN_CLICKED) {
            LaunchShm(state);
        }
        return 0;
    }
    case WM_DRAWITEM: {
        auto dis = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (dis->CtlID == ID_SOCKET_BTN) {
            DrawCardButton(dis, state, true);
            return TRUE;
        } else if (dis->CtlID == ID_SHM_BTN) {
            DrawCardButton(dis, state, false);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, state->theme.text);
        return (LRESULT)state->theme.baseBrush;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintLauncherBackground(hwnd, hdc, state);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY: {
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    LauncherState state;
    WNDCLASSW wc{};
    wc.lpfnWndProc = LauncherWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ChatLauncherWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"OS Chat Studio",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1120, 720,
        nullptr, nullptr, hInstance, &state);

    if (!hwnd) {
        MessageBoxW(nullptr, L"Failed to create launcher window.", L"Error", MB_ICONERROR | MB_TOPMOST);
        return -1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_target == LaunchTarget::Socket) {
        return RunSocketChat(hInstance, SW_SHOWNORMAL, nullptr);
    } else if (g_target == LaunchTarget::Shm) {
        return RunShmChat(hInstance, SW_SHOWNORMAL, nullptr);
    }
    return 0;
}
