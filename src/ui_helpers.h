#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <sstream>
#include <strsafe.h>
#include <cstdarg>

struct UiTheme {
    COLORREF base = RGB(12, 18, 38);        
    COLORREF card = RGB(22, 30, 52);         
    COLORREF accent = RGB(0, 194, 255);      
    COLORREF accent2 = RGB(16, 185, 129);    
    COLORREF text = RGB(228, 238, 255);      
    COLORREF muted = RGB(140, 159, 193);     
    HBRUSH baseBrush = CreateSolidBrush(base);
    HBRUSH cardBrush = CreateSolidBrush(card);
};

inline HFONT CreateUiFont(int size, bool bold = false, const wchar_t* face = L"Segoe UI") {
    return CreateFontW(
        size, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, face);
}

inline std::wstring GetWindowTextWstr(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(len, L'\0');
    if (len > 0) {
        GetWindowTextW(hwnd, text.data(), len + 1);
    }
    return text;
}

inline void AppendText(HWND edit, const std::wstring& text) {
    int end = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, (WPARAM)end, (LPARAM)end);
    SendMessageW(edit, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
}

inline void PaintGradientHeader(HDC hdc, const RECT& area, const UiTheme& theme) {
    TRIVERTEX vert[2];
    vert[0].x = area.left;
    vert[0].y = area.top;
    vert[0].Red   = GetRValue(theme.accent) << 8;
    vert[0].Green = GetGValue(theme.accent) << 8;
    vert[0].Blue  = GetBValue(theme.accent) << 8;
    vert[0].Alpha = 0xFFFF;

    vert[1].x = area.right;
    vert[1].y = area.bottom;
    vert[1].Red   = GetRValue(theme.card) << 8;
    vert[1].Green = GetGValue(theme.card) << 8;
    vert[1].Blue  = GetBValue(theme.card) << 8;
    vert[1].Alpha = 0xFFFF;

    GRADIENT_RECT gRect = {0, 1};
    GradientFill(hdc, vert, 2, &gRect, 1, GRADIENT_FILL_RECT_V);

    HPEN pen = CreatePen(PS_SOLID, 2, theme.accent2);
    HPEN old = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, area.left, area.bottom - 2, nullptr);
    LineTo(hdc, area.right, area.bottom - 2);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

inline void DrawCard(HDC hdc, const RECT& rc, const UiTheme& theme) {
    HBRUSH brush = theme.cardBrush;
    FillRect(hdc, &rc, brush);
    HPEN pen = CreatePen(PS_SOLID, 1, theme.accent);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

inline HBRUSH HandleCtlColor(const UiTheme& theme, HWND hwndChild, HDC hdc) {
    SetBkColor(hdc, theme.card);
    SetTextColor(hdc, theme.text);
    return theme.cardBrush;
}

inline void SetControlFont(HWND hwnd, HFONT font) {
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
}

inline std::wstring FormatWide(const wchar_t* fmt, ...) {
    wchar_t buffer[512];
    va_list args;
    va_start(args, fmt);
    StringCchVPrintfW(buffer, 512, fmt, args);
    va_end(args);
    return buffer;
}
