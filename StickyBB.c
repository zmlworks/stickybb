/* SPDX-License-Identifier: MIT — Copyright (c) 2026 zmlworks */
/* StickyNotesRTver2 — RT fork with chalk-era UI, solid background */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#define _RICHEDIT_VER 0x0800
#include <windows.h>
#include <richedit.h>
#include <commctrl.h>
#include <wchar.h>
#ifdef TEST_HARNESS
#include <stdio.h>
#endif

#ifndef FR_DOWN
#define FR_DOWN 0x00000001  /* commdlg.h (LEAN_AND_MEAN で除外) */
#endif
#define ID_EDIT        101
#define ID_SAVE        102
#define ID_INK0        200
#define IDI_APP          1
#define ID_TIMER_AUTO    2
#define ID_TIMER_IDLE    3
#define IDM_UNDO       401
#define IDM_CUT        402
#define IDM_COPY       403
#define IDM_PASTE      404
#define IDM_DELETE     405
#define IDM_SELALL     406

#define AUTO_MS      (30 * 60 * 1000)
#define IDLE_MS      (10 * 60 * 1000)
#define BORDER       5
#define PAD          8
#define BAR_H        28
#define SWATCH       16
#define SWATCH_FRAME 4
#define SWATCH_BOX   (SWATCH + SWATCH_FRAME * 2)
#define SWATCH_GAP   10   /* 変更(v1.1): 14 → 10 色ボタンの間隔を少し狭く */
#define MAX_SLOT     32
#define INK_COUNT    7
#define COL_INK_SEL  RGB(0xCE, 0x91, 0x78)

#define COL_BG     RGB(0x00, 0x22, 0x18)
#define COL_FG     RGB(0xD3, 0xD3, 0xD3)
#define COL_TITLE  RGB(0x3D, 0x01, 0x05)
#define COL_PIN_OFF RGB(0x8B, 0x91, 0x8B)   /* 追加(v1.1): 最前面ピン オフ時 */
#define COL_PIN_HOT RGB(0x5A, 0x16, 0x1C)   /* 追加(v1.1): ピンのホバー/オン背景 */

/* 追加(v1.1): 表示倍率 (EM_SETZOOM, 単位 %) */
#define ZOOM_MIN   50
#define ZOOM_MAX   300
#define ZOOM_STEP  10
#define ID_ZOOM_IN    301
#define ID_ZOOM_OUT   302
#define ID_ZOOM_RESET 303
#define WM_APP_PIN    (WM_APP + 1)
#define WM_APP_TOPMOST (WM_APP + 2)

static const COLORREF kInk[INK_COUNT] = {
    /* 変更: 並び順 c1, c3, #B56B85(新), c4, c5, c6, c2 */
    RGB(0xC6, 0xCF, 0xC6),
    RGB(0xC0, 0x5E, 0x40),
    RGB(0xB5, 0x6B, 0x85),
    RGB(0xAE, 0xB8, 0x72),
    RGB(0x41, 0xA1, 0x8B),
    RGB(0x6E, 0x9E, 0xB2),
    RGB(0x8B, 0x91, 0x8B)
};

static HWND    gEdit, gMain;
static HWND    gSwatch[INK_COUNT];
static HFONT   gFont;
static HBRUSH  gBg, gBorder;
static wchar_t gPath[MAX_PATH];
static wchar_t gTitle[32];          /* 既定タイトル (StickyBB / StickyBB N) */
#define TITLE_MAX 64
static wchar_t gUserTitle[TITLE_MAX + 1]; /* 追加: INI の title (空なら既定) */
static wchar_t gIni[MAX_PATH];
static int     gSlot = 1;
static int     gDirty = 0;
static int     gInk = 0;
static int     gReady = 0;
static int     gReentering = 0;
static HANDLE  gMutex = NULL;
static WNDPROC gEditPrev = NULL;
static int     gZoom = 100;          /* 追加(v1.1): このウィンドウの表示倍率 % (INI: zoom=) */
static int     gTopmost = 0;         /* 追加(v1.1): 最前面に表示 (INI: topmost=) */
static HWND    gPin = NULL;          /* 追加(v1.1): タイトルバー上のピン (所有ポップアップ) */
static HWND    gPinTip = NULL;
static int     gPinHot = 0;



/* 起動時に1回だけ (読み込み前) 呼ぶ。オリジナルと同じ -20px 固定 */
static void RecreateFont(void)
{
    HFONT nf = CreateFontW(-20, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                           DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                           VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
    if (!nf) return;
    if (gEdit) SendMessageW(gEdit, WM_SETFONT, (WPARAM)nf, TRUE);
    if (gFont) DeleteObject(gFont);
    gFont = nf;
}

static void IniPath(void)
{
    wchar_t *slash;
    GetModuleFileNameW(NULL, gIni, MAX_PATH);
    slash = wcsrchr(gIni, L'\\');
    if (slash) slash[1] = 0;
    else gIni[0] = 0;
    lstrcatW(gIni, L"StickyBB.ini");
    /* 追加: INI が無ければ UTF-16LE(BOM付き)で作る。こうすると *W 系 API が
       日本語タイトルを文字化けせずに保存できる (ANSI ファイルだとコードページ依存) */
    {
        HANDLE h = CreateFileW(gIni, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            static const BYTE bom[2] = { 0xFF, 0xFE };
            DWORD wr;
            WriteFile(h, bom, 2, &wr, NULL);
            CloseHandle(h);
        }
    }
}

/* 追加: 未保存マーク付きでウィンドウタイトル(=タスクバー名)を更新。
   キャプションは DWM が描く標準のものなので、タイトル文字列の先頭に「● 」を付ける */
static void UpdateTitle(void)
{
    wchar_t buf[TITLE_MAX + 8];
    if (!gMain) return;
    lstrcpyW(buf, gDirty ? L"\x25CF " : L"");
    lstrcatW(buf, gUserTitle[0] ? gUserTitle : gTitle);
    SetWindowTextW(gMain, buf);
}

static void SetDirty(int d)
{
    if (gDirty == d) return;
    gDirty = d;
    UpdateTitle();
}

static void LoadUserTitle(void)
{
    wchar_t sec[16];
    wsprintfW(sec, L"%d", gSlot);
    GetPrivateProfileStringW(sec, L"title", L"", gUserTitle, TITLE_MAX + 1, gIni);
}

static void IniWriteInt(const wchar_t *sec, const wchar_t *key, int v)
{
    wchar_t buf[32];
    wsprintfW(buf, L"%d", v);
    WritePrivateProfileStringW(sec, key, buf, gIni);
}

static void HideEditBar(void);

/* ---- 追加(v1.1): 表示倍率 ----
   RichEdit の EM_SETZOOM で表示だけを拡大/縮小する。文字ごとの色・書式や
   RTF ファイルの中身には一切手を付けない (保存形式は変わらない)。 */
static void ApplyZoom(void)
{
    if (!gEdit) return;
    /* 100% は 100/100 を指定 (0/0 だと再描画されない RichEdit 実装があった) */
    SendMessageW(gEdit, EM_SETZOOM, (WPARAM)gZoom, (LPARAM)100);
    InvalidateRect(gEdit, NULL, TRUE);
    HideEditBar();
}

static void SetZoom(int z)
{
    wchar_t sec[16];
    if (z < ZOOM_MIN) z = ZOOM_MIN;
    if (z > ZOOM_MAX) z = ZOOM_MAX;
    gZoom = z;
    ApplyZoom();
    wsprintfW(sec, L"%d", gSlot);
    IniWriteInt(sec, L"zoom", gZoom);
}

/* Ctrl+; / Ctrl+= / Ctrl++ (拡大), Ctrl+- (縮小), Ctrl+0 (100%)。
   キーの割り当てはレイアウトで違うので、仮想キーではなく「そのキーが出す文字」で判定する:
   JIS の ';' キー = VK_OEM_PLUS(0xBB), US の ';' = VK_OEM_1(0xBA)、US の '=' = VK_OEM_PLUS。
   テンキーの + - 0 も受け付ける。戻り値: コマンド ID (該当なしは 0) */
static int ZoomKeyCommand(const MSG *m)
{
    UINT ch;
    if (m->message != WM_KEYDOWN) return 0;
    if (!(GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) return 0;
    switch (m->wParam) {
    case VK_ADD:      return ID_ZOOM_IN;
    case VK_SUBTRACT: return ID_ZOOM_OUT;
    case VK_NUMPAD0:  return ID_ZOOM_RESET;
    }
    ch = MapVirtualKeyW((UINT)m->wParam, 2 /* MAPVK_VK_TO_CHAR */) & 0xFFFF;
    if (ch == L';' || ch == L'=' || ch == L'+') return ID_ZOOM_IN;
    if (ch == L'-') return ID_ZOOM_OUT;
    if (ch == L'0') return ID_ZOOM_RESET;
    return 0;
}

static void SavePlacement(HWND hwnd)
{
    WINDOWPLACEMENT pl;
    wchar_t sec[16];
    ZeroMemory(&pl, sizeof(pl));
    pl.length = sizeof(pl);
    if (!GetWindowPlacement(hwnd, &pl)) return;
    wsprintfW(sec, L"%d", gSlot);
    IniWriteInt(sec, L"show", pl.showCmd);
    IniWriteInt(sec, L"flags", (int)pl.flags);
    IniWriteInt(sec, L"left", pl.rcNormalPosition.left);
    IniWriteInt(sec, L"top", pl.rcNormalPosition.top);
    IniWriteInt(sec, L"right", pl.rcNormalPosition.right);
    IniWriteInt(sec, L"bottom", pl.rcNormalPosition.bottom);
}

static void ClampRect(RECT *r)
{
    HMONITOR mon = MonitorFromRect(r, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    int w, h;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return;
    w = r->right - r->left;
    h = r->bottom - r->top;
    if (w < 300) w = 300;
    if (h < 160) h = 160;
    if (r->left < mi.rcWork.left) { r->left = mi.rcWork.left; r->right = r->left + w; }
    if (r->top < mi.rcWork.top) { r->top = mi.rcWork.top; r->bottom = r->top + h; }
    if (r->right > mi.rcWork.right) { r->right = mi.rcWork.right; r->left = r->right - w; }
    if (r->bottom > mi.rcWork.bottom) { r->bottom = mi.rcWork.bottom; r->top = r->bottom - h; }
}

static int RestorePlacement(HWND hwnd)
{
    WINDOWPLACEMENT pl;
    wchar_t sec[16], got[16];
    int left;
    wsprintfW(sec, L"%d", gSlot);
    GetPrivateProfileStringW(sec, L"left", L"", got, 16, gIni);
    if (!got[0]) return 0;
    ZeroMemory(&pl, sizeof(pl));
    pl.length = sizeof(pl);
    GetWindowPlacement(hwnd, &pl);
    pl.flags = (UINT)GetPrivateProfileIntW(sec, L"flags", 0, gIni);
    pl.showCmd = (UINT)GetPrivateProfileIntW(sec, L"show", SW_SHOWNORMAL, gIni);
    if (pl.showCmd == SW_SHOWMINIMIZED || pl.showCmd == SW_MINIMIZE)
        pl.showCmd = SW_SHOWNORMAL;
    left = (int)GetPrivateProfileIntW(sec, L"left", 0, gIni);
    pl.rcNormalPosition.left = left;
    pl.rcNormalPosition.top = (LONG)GetPrivateProfileIntW(sec, L"top", 0, gIni);
    pl.rcNormalPosition.right = (LONG)GetPrivateProfileIntW(sec, L"right", left + 300, gIni);
    pl.rcNormalPosition.bottom = (LONG)GetPrivateProfileIntW(sec, L"bottom", 260, gIni);
    ClampRect(&pl.rcNormalPosition);
    return SetWindowPlacement(hwnd, &pl) ? 1 : 0;
}

static void HideEditBar(void)
{
    if (!gEdit) return;
    ShowScrollBar(gEdit, SB_VERT, FALSE);
    SendMessageW(gEdit, EM_SHOWSCROLLBAR, SB_VERT, FALSE);
}

static void WheelScroll(WPARAM wp)
{
    int delta = (short)HIWORD(wp);
    UINT lines = 3;
    int n;
    if (!gEdit) return;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == 0) return;
    if (lines == WHEEL_PAGESCROLL) {
        SendMessageW(gEdit, EM_SCROLL, (delta > 0) ? SB_PAGEUP : SB_PAGEDOWN, 0);
        return;
    }
    n = -(delta / WHEEL_DELTA) * (int)lines;
    if (n) SendMessageW(gEdit, EM_LINESCROLL, 0, n);
}

static int ClaimSlot(void)
{
    int i;
    for (i = 1; i <= MAX_SLOT; i++) {
        wchar_t name[64];
        HANDLE h;
        wsprintfW(name, L"Local\\StickyBB-%d", i);
        h = CreateMutexW(NULL, FALSE, name);
        if (h && GetLastError() != ERROR_ALREADY_EXISTS) {
            gMutex = h;
            return i;
        }
        if (h) CloseHandle(h);
    }
    return MAX_SLOT;
}

static void NotePath(void)
{
    wchar_t *slash;
    GetModuleFileNameW(NULL, gPath, MAX_PATH);
    slash = wcsrchr(gPath, L'\\');
    if (slash) slash[1] = 0;
    else gPath[0] = 0;
    if (gSlot <= 1)
        lstrcatW(gPath, L"StickyBB.rtf");
    else {
        wchar_t name[32];
        wsprintfW(name, L"StickyBB-%d.rtf", gSlot);
        lstrcatW(gPath, name);
    }
}

static DWORD CALLBACK StreamOutProc(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG *pcb)
{
    DWORD wr = 0;
    if (!WriteFile((HANDLE)cookie, buf, (DWORD)cb, &wr, NULL)) return 1;
    *pcb = (LONG)wr;
    return 0;
}

static DWORD CALLBACK StreamInProc(DWORD_PTR cookie, LPBYTE buf, LONG cb, LONG *pcb)
{
    DWORD rd = 0;
    if (!ReadFile((HANDLE)cookie, buf, (DWORD)cb, &rd, NULL)) {
        *pcb = 0;
        return 1;
    }
    *pcb = (LONG)rd;
    return 0;
}

static void ApplyLayoutFormat(int withDefaultColor)
{
    CHARRANGE save, all;
    CHARFORMAT2W cf;
    PARAFORMAT2 pf;

    SendMessageW(gEdit, EM_EXGETSEL, 0, (LPARAM)&save);
    all.cpMin = 0;
    all.cpMax = -1;
    SendMessageW(gEdit, EM_EXSETSEL, 0, (LPARAM)&all);

    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC;
    cf.dwEffects = 0;
    cf.yHeight = 300;
    lstrcpyW(cf.szFaceName, L"Segoe UI");
    if (withDefaultColor) {
        cf.dwMask |= CFM_COLOR;
        cf.crTextColor = kInk[0];
    }
    SendMessageW(gEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

    ZeroMemory(&pf, sizeof(pf));
    pf.cbSize = sizeof(pf);
    pf.dwMask = PFM_SPACEBEFORE | PFM_SPACEAFTER | PFM_LINESPACING |
                PFM_OFFSET | PFM_STARTINDENT | PFM_RIGHTINDENT;
    pf.bLineSpacingRule = 0;
    pf.dyLineSpacing = 0;
    pf.dySpaceBefore = 0;
    pf.dySpaceAfter = 0;
    SendMessageW(gEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf);

    SendMessageW(gEdit, EM_EXSETSEL, 0, (LPARAM)&save);
}

static void ApplyDefaultFormat(void)
{
    ApplyLayoutFormat(1);
}

static void SetTypingColor(COLORREF c)
{
    CHARFORMAT2W cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.dwEffects = 0;
    cf.crTextColor = c;
    SendMessageW(gEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static void TouchIdle(void)
{
    HWND hwnd = gMain ? gMain : (gEdit ? GetParent(gEdit) : NULL);
    if (!hwnd) return;
    KillTimer(hwnd, ID_TIMER_IDLE);
    SetTimer(hwnd, ID_TIMER_IDLE, IDLE_MS, NULL);
}

static void ResetInkToDefault(void)
{
    int i;
    HWND hwnd = gMain ? gMain : (gEdit ? GetParent(gEdit) : NULL);
    if (hwnd) KillTimer(hwnd, ID_TIMER_IDLE);
    if (gInk == 0) return;
    gInk = 0;
    if (gEdit) SetTypingColor(kInk[0]);
    for (i = 0; i < INK_COUNT; i++)
        if (gSwatch[i]) InvalidateRect(gSwatch[i], NULL, TRUE);
}

static void ReapplyInkIfCaret(void)
{
    CHARRANGE cr;
    if (!gReady || gReentering || !gEdit)
        return;
    SendMessageW(gEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMin != cr.cpMax)
        return;
    gReentering = 1;
    SetTypingColor(kInk[gInk]);
    gReentering = 0;
}

static void SaveNote(void)
{
    /* 修正: 旧版は本体ファイルを直接 CREATE_ALWAYS で切り詰めてから書いていたため、
       書き込み中の電源断/失敗でメモが消える恐れがあった。一時ファイルに書いてから置き換える。
       また失敗時に gDirty を下ろさない (次回の自動保存/終了時に再試行される)。 */
    wchar_t tmp[MAX_PATH + 8];
    HANDLE h;
    EDITSTREAM es;
    BOOL ok;
    if (lstrlenW(gPath) + 5 >= MAX_PATH + 8) return;
    lstrcpyW(tmp, gPath);
    lstrcatW(tmp, L".tmp");
    h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    ZeroMemory(&es, sizeof(es));
    es.dwCookie = (DWORD_PTR)h;
    es.pfnCallback = StreamOutProc;
    SendMessageW(gEdit, EM_STREAMOUT, SF_RTF, (LPARAM)&es);
    ok = (es.dwError == 0) && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || !MoveFileExW(tmp, gPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp);
        return;
    }
    SetDirty(0);
}

static void LoadNote(void)
{
    HANDLE h = CreateFileW(gPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        ApplyDefaultFormat();
        return;
    }
    {
        EDITSTREAM es;
        ZeroMemory(&es, sizeof(es));
        es.dwCookie = (DWORD_PTR)h;
        es.pfnCallback = StreamInProc;
        SendMessageW(gEdit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
    }
    CloseHandle(h);
    ApplyLayoutFormat(0);
    SetDirty(0);
}

/* 追加: 文書の先頭文字が '#' なら、その1段落目をタイトルとして取り出して削除する。
   - 判定は「文書の一番最初の文字」のみ (前の空白は無視しない: 予測しやすさ優先)
   - 削除は EM_REPLACESEL で該当範囲だけ消すので、残りの文字の色/書式はそのまま
   - '#' だけなら既定タイトルに戻す
   - 戻り値: 処理したら 1 (呼び出し側で保存する) */
static int TakeTitleLine(void)
{
    wchar_t line[512], clean[TITLE_MAX + 1];
    TEXTRANGEW tr;
    FINDTEXTEXW ft;
    LONG end, cut, n, i, j;
    CHARRANGE save, del;
    wchar_t sec[16];

    if (!gEdit) return 0;
    tr.chrg.cpMin = 0; tr.chrg.cpMax = 1; line[0] = 0;
    tr.lpstrText = line;
    SendMessageW(gEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);
    if (line[0] != L'#') return 0;

    /* 1段落目の終わり (RichEdit の段落区切りは \r) */
    ft.chrg.cpMin = 0; ft.chrg.cpMax = -1; ft.lpstrText = L"\r";
    end = (LONG)SendMessageW(gEdit, EM_FINDTEXTEXW, FR_DOWN, (LPARAM)&ft);
    if (end < 0) { end = GetWindowTextLengthW(gEdit); cut = end; }
    else cut = end + 1;                         /* 改行ごと消す */
    n = end; if (n > 511) n = 511;
    tr.chrg.cpMin = 0; tr.chrg.cpMax = n;
    SendMessageW(gEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr);

    /* '#' の後ろを整形: 制御文字・ファイル名に使えない文字を除去、前後の空白(全角含む)を除去、64文字まで */
    for (i = 1, j = 0; line[i] && j < TITLE_MAX; i++) {
        wchar_t c = line[i];
        if (c < 0x20 || c == 0x7F || wcschr(L"\\/:*?\"<>|", c)) continue;
        if (j == 0 && (c == L' ' || c == L'\t' || c == 0x3000)) continue;
        clean[j++] = c;
    }
    while (j > 0 && (clean[j-1] == L' ' || clean[j-1] == 0x3000)) j--;
    clean[j] = 0;

    /* 先頭段落を削除 (選択位置はずらして復元) */
    SendMessageW(gEdit, EM_EXGETSEL, 0, (LPARAM)&save);
    del.cpMin = 0; del.cpMax = cut;
    SendMessageW(gEdit, EM_EXSETSEL, 0, (LPARAM)&del);
    SendMessageW(gEdit, EM_REPLACESEL, FALSE, (LPARAM)L"");
    save.cpMin = (save.cpMin > cut) ? save.cpMin - cut : 0;
    save.cpMax = (save.cpMax > cut) ? save.cpMax - cut : 0;
    SendMessageW(gEdit, EM_EXSETSEL, 0, (LPARAM)&save);

    wsprintfW(sec, L"%d", gSlot);
    WritePrivateProfileStringW(sec, L"title", clean[0] ? clean : NULL, gIni);
    lstrcpyW(gUserTitle, clean);
    SetDirty(1);
    UpdateTitle();
    return 1;
}

static void ColorCaption(HWND hwnd)
{
    typedef HRESULT (WINAPI *FN)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE m = LoadLibraryW(L"dwmapi.dll");
    if (!m) return;
    FN fn = (FN)GetProcAddress(m, "DwmSetWindowAttribute");
    if (fn) {
        COLORREF cap = COL_TITLE, txt = COL_FG, brd = COL_TITLE;
        BOOL dark = TRUE;
        int corner = 1;
        fn(hwnd, 20, &dark, sizeof(dark));
        fn(hwnd, 33, &corner, sizeof(corner));
        fn(hwnd, 34, &brd, sizeof(brd));
        fn(hwnd, 35, &cap, sizeof(cap));
        fn(hwnd, 36, &txt, sizeof(txt));
    }
    FreeLibrary(m);
}

static void Layout(HWND hwnd)
{
    RECT rc; GetClientRect(hwnd, &rc);
    int editBottom = rc.bottom - BORDER - BAR_H;
    MoveWindow(gEdit,
               BORDER + PAD,
               BORDER + PAD,
               rc.right - BORDER * 2 - PAD * 2,
               editBottom - BORDER - PAD,
               TRUE);
    {
        int gap = SWATCH_GAP, box = SWATCH_BOX;
        int total = INK_COUNT * box + (INK_COUNT - 1) * gap;
        int x0 = (rc.right - total) / 2;
        int y0 = rc.bottom - BORDER - (BAR_H + box) / 2;
        int i;
        for (i = 0; i < INK_COUNT; i++)
            MoveWindow(gSwatch[i], x0 + i * (box + gap), y0, box, box, TRUE);
    }
}

static void ShowEditMenu(HWND edit, int sx, int sy)
{
    CHARRANGE cr;
    HMENU m = CreatePopupMenu();
    int canUndo = (int)SendMessageW(edit, EM_CANUNDO, 0, 0);
    int hasSel;
    int canPaste = IsClipboardFormatAvailable(CF_UNICODETEXT) ||
                   IsClipboardFormatAvailable(CF_TEXT);
    SendMessageW(edit, EM_EXGETSEL, 0, (LPARAM)&cr);
    hasSel = (cr.cpMax != cr.cpMin);

    AppendMenuW(m, MF_STRING | (canUndo ? 0 : MF_GRAYED), IDM_UNDO, L"元に戻す");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (hasSel ? 0 : MF_GRAYED), IDM_CUT, L"切り取り");
    AppendMenuW(m, MF_STRING | (hasSel ? 0 : MF_GRAYED), IDM_COPY, L"コピー");
    AppendMenuW(m, MF_STRING | (canPaste ? 0 : MF_GRAYED), IDM_PASTE, L"貼り付け");
    AppendMenuW(m, MF_STRING | (hasSel ? 0 : MF_GRAYED), IDM_DELETE, L"削除");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_SELALL, L"すべて選択");

    if (sx == -1 && sy == -1) {
        POINT pt;
        GetCaretPos(&pt);
        ClientToScreen(edit, &pt);
        sx = pt.x;
        sy = pt.y;
    }
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_RIGHTBUTTON, sx, sy, 0, GetParent(edit), NULL);
    DestroyMenu(m);
}

static LRESULT CALLBACK EditProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    /* 修正(細字の原因の一つ): 貼り付けを書式なしテキストに限定する。
       旧版は Web/Word 等からの RTF/HTML 貼り付けで、元のフォント(游ゴシック Light 等)・
       太さ・サイズがそのまま入り、その部分だけ細く/違って見えていた。 */
    if (msg == WM_PASTE) {
        SendMessageW(hw, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
        return 0;
    }
    if (msg == WM_KEYDOWN &&
        ((wp == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) ||
         (wp == VK_INSERT && (GetKeyState(VK_SHIFT) & 0x8000)))) {
        SendMessageW(hw, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
        return 0;
    }
    /* 修正3: 入力の直前にも、現在のインク色を入力位置へ再適用する (選択範囲が空のときだけ)。
       EN_SELCHANGE だけに頼ると、Windows 実機ではクリック/矢印キーで移動した後、
       IME の変換開始時や文字入力時に RichEdit が隣の文字の書式を拾い直すことがあり、
       隣の色で入力されていた。選択範囲がある場合は ReapplyInkIfCaret が何もしないので、
       選択しただけで色が変わることはない。 */
    if (msg == WM_CHAR || msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_CHAR ||
        msg == WM_IME_COMPOSITION)
        ReapplyInkIfCaret();
    if (msg == WM_LBUTTONUP || msg == WM_KEYUP) {
        LRESULT r = CallWindowProcW(gEditPrev, hw, msg, wp, lp);
        ReapplyInkIfCaret();
        return r;
    }
    if (msg == WM_MOUSEWHEEL) {
        WheelScroll(wp);
        return 0;
    }
    if (msg == WM_CONTEXTMENU) {
        ShowEditMenu(hw, (short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    }
    if (msg == WM_RBUTTONUP) {
        POINT pt;
        pt.x = (short)LOWORD(lp);
        pt.y = (short)HIWORD(lp);
        ClientToScreen(hw, &pt);
        ShowEditMenu(hw, pt.x, pt.y);
        return 0;
    }
    return CallWindowProcW(gEditPrev, hw, msg, wp, lp);
}

static LRESULT CALLBACK SwatchProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    int id = GetDlgCtrlID(hw);
    int idx = id - ID_INK0;
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        RECT rc, inner;
        HBRUSH b;
        GetClientRect(hw, &rc);
        FillRect(dc, &rc, gBg);
        inner = rc;
        InflateRect(&inner, -SWATCH_FRAME, -SWATCH_FRAME);
        b = CreateSolidBrush(kInk[idx]);
        FillRect(dc, &inner, b);
        DeleteObject(b);
        if (idx == gInk) {
            HPEN p = CreatePen(PS_SOLID, 3, COL_INK_SEL);
            HGDIOBJ oldp = SelectObject(dc, p);
            HGDIOBJ oldb = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, 1, 1, rc.right - 1, rc.bottom - 1);
            SelectObject(dc, oldp);
            SelectObject(dc, oldb);
            DeleteObject(p);
        }
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_LBUTTONUP:
        SendMessageW(GetParent(hw), WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)hw);
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return TRUE;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

/* ---- 追加(v1.1): 最前面に表示 (タイトルバーのピン) ----
   キャプションは DWM が描く標準のもの (色だけ DwmSetWindowAttribute で変更) なので、
   非クライアント領域に直接描いても表示されない。そこで小さな所有ポップアップ
   (オーナー = メインウィンドウ) を最小化ボタンの左隣に重ねて置く。
   所有ウィンドウなので、常にオーナーの直上に重なり、最小化で一緒に隠れ、最前面も一緒になる。 */
static void SetTopmost(int on, int save);

static void PinTipText(void)
{
    TOOLINFOW ti;
    if (!gPinTip) return;
    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd = gPin;
    ti.uId = (UINT_PTR)gPin;
    ti.lpszText = gTopmost ? L"最前面に表示: オン（クリックで解除）"
                           : L"最前面に表示: オフ（クリックで常に手前に表示）";
    SendMessageW(gPinTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}

static void PositionPin(void)
{
    TITLEBARINFOEX tb;
    RECT btn;
    int w, h;
    if (!gPin || !gMain) return;
    if (IsIconic(gMain) || !IsWindowVisible(gMain)) return;
    ZeroMemory(&tb, sizeof(tb));
    tb.cbSize = sizeof(tb);
    SendMessageW(gMain, WM_GETTITLEBARINFOEX, 0, (LPARAM)&tb);
    btn = tb.rgrect[2];                         /* 最小化ボタン */
    if (btn.right <= btn.left || btn.bottom <= btn.top) {
        /* 予備: WM_GETTITLEBARINFOEX が使えない環境 (Wine など) */
        TITLEBARINFO t;
        int bw = GetSystemMetrics(SM_CXSIZE);
        ZeroMemory(&t, sizeof(t));
        t.cbSize = sizeof(t);
        if (!GetTitleBarInfo(gMain, &t)) return;
        btn.right  = t.rcTitleBar.right - bw * 2;
        btn.left   = btn.right - bw;
        btn.top    = t.rcTitleBar.top;
        btn.bottom = t.rcTitleBar.bottom;
    }
    h = btn.bottom - btn.top;
    if (h < 16) h = 16;
    w = h + 6; if (w < 26) w = 26; if (w > 40) w = 40;
    {   /* オーナーのすぐ上に重ねる (他のウィンドウより手前には出さない)。
           ピンの最前面属性はオーナーと必ず揃える: ずれた位置に挿入すると
           Windows がオーナーごと最前面を外してしまうため */
        HWND after = GetWindow(gMain, GW_HWNDPREV);
        UINT f = SWP_NOACTIVATE | SWP_SHOWWINDOW;
        if (after == gPin)
            f |= SWP_NOZORDER;
        else {
            int ownerTop = (GetWindowLongW(gMain, GWL_EXSTYLE) & WS_EX_TOPMOST) ? 1 : 0;
            if (!after || ((GetWindowLongW(after, GWL_EXSTYLE) & WS_EX_TOPMOST) ? 1 : 0) != ownerTop)
                after = ownerTop ? HWND_TOPMOST : HWND_TOP;
        }
        SetWindowPos(gPin, after, btn.left - w - 2, btn.top, w, h, f);
    }
}


/* 画鋲アイコン。4倍の大きさで描いてから縮小 (HALFTONE) し、輪郭をなめらかにする。
   オン: インク選択色で塗りつぶし・まっすぐ (刺さっている) / オフ: グレーの輪郭・45度傾き */
static void DrawPin(HDC dc, int cw, int ch)
{
    const int S = 4;
    int W = cw * S, H = ch * S, i;
    HDC m = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
    HGDIOBJ ob = SelectObject(m, bmp);
    RECT r = { 0, 0, W, H };
    HBRUSH bg = CreateSolidBrush(COL_TITLE);
    COLORREF col = gTopmost ? COL_INK_SEL : COL_PIN_OFF;
    /* 単位 = アイコン高さ 16 を基準とした座標 (中心 0,0) */
    static const float shape[][2] = {
        { -3.5f, -7.0f }, { 3.5f, -7.0f }, { 2.5f, -5.5f }, { 2.5f, -1.0f },
        { 5.0f, 1.5f }, { -5.0f, 1.5f }, { -2.5f, -1.0f }, { -2.5f, -5.5f }
    };
    POINT pt[8], nd[2];
    float k = (float)(ch < cw ? ch : cw) * S / 22.0f;
    float cs = gTopmost ? 1.0f : 0.7071f, sn = gTopmost ? 0.0f : 0.7071f;
    float cx = W / 2.0f, cy = H / 2.0f + k * 0.5f;
    HPEN pen;
    HBRUSH fill;
    HGDIOBJ op, obr;

    FillRect(m, &r, bg);
    if (gTopmost || gPinHot) {
        HBRUSH hb = CreateSolidBrush(COL_PIN_HOT);
        HGDIOBJ o1 = SelectObject(m, hb), o2 = SelectObject(m, GetStockObject(NULL_PEN));
        RoundRect(m, S * 2, S * 2, W - S * 2, H - S * 2, S * 8, S * 8);
        SelectObject(m, o1); SelectObject(m, o2);
        DeleteObject(hb);
    }
    for (i = 0; i < 8; i++) {
        float x = shape[i][0] * k, y = shape[i][1] * k;
        pt[i].x = (LONG)(cx + x * cs - y * sn);
        pt[i].y = (LONG)(cy + x * sn + y * cs);
    }
    {
        float y0 = 1.5f * k, y1 = 7.0f * k;
        nd[0].x = (LONG)(cx - y0 * sn); nd[0].y = (LONG)(cy + y0 * cs);
        nd[1].x = (LONG)(cx - y1 * sn); nd[1].y = (LONG)(cy + y1 * cs);
    }
    pen = CreatePen(PS_SOLID, (int)(k * 1.4f) + 1, col);
    fill = gTopmost ? CreateSolidBrush(col) : NULL;
    op = SelectObject(m, pen);
    obr = SelectObject(m, fill ? (HGDIOBJ)fill : GetStockObject(NULL_BRUSH));
    Polygon(m, pt, 8);
    MoveToEx(m, nd[0].x, nd[0].y, NULL);
    LineTo(m, nd[1].x, nd[1].y);
    SelectObject(m, op); SelectObject(m, obr);
    DeleteObject(pen);
    if (fill) DeleteObject(fill);

    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, NULL);
    StretchBlt(dc, 0, 0, cw, ch, m, 0, 0, W, H, SRCCOPY);
    SelectObject(m, ob);
    DeleteObject(bmp);
    DeleteObject(bg);
    DeleteDC(m);
}

static LRESULT CALLBACK PinProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint(hw, &ps);
        GetClientRect(hw, &rc);
        DrawPin(dc, rc.right, rc.bottom);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
        if (!gPinHot) {
            TRACKMOUSEEVENT t;
            t.cbSize = sizeof(t); t.dwFlags = TME_LEAVE; t.hwndTrack = hw; t.dwHoverTime = 0;
            TrackMouseEvent(&t);
            gPinHot = 1;
            InvalidateRect(hw, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        gPinHot = 0;
        InvalidateRect(hw, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
        /* 自分自身を作り直すので、処理はメインウィンドウ側で行う */
        PostMessageW(gMain, WM_APP_TOPMOST, 0, 0);
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return TRUE;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

static void CreatePin(HWND owner)
{
    WNDCLASSW pc = {0};
    static int registered = 0;
    pc.lpfnWndProc   = PinProc;
    pc.hInstance     = GetModuleHandleW(NULL);
    pc.hCursor       = LoadCursor(NULL, IDC_HAND);
    pc.lpszClassName = L"StickyPinV1";
    if (!registered) {
        INITCOMMONCONTROLSEX ic;
        RegisterClassW(&pc);
        ic.dwSize = sizeof(ic);
        ic.dwICC = ICC_WIN95_CLASSES;
        InitCommonControlsEx(&ic);
        registered = 1;
    }
    gPinHot = 0;
    gPin = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"StickyPinV1", L"",
                           WS_POPUP, 0, 0, 26, 26, owner, NULL, pc.hInstance, NULL);
    if (!gPin) return;
    /* ツールチップはピンの所有ウィンドウ (ピンと一緒に破棄される) */
    gPinTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                              WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                              gPin, NULL, pc.hInstance, NULL);
    if (gPinTip) {
        TOOLINFOW ti;
        ZeroMemory(&ti, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = gPin;
        ti.uId = (UINT_PTR)gPin;
        ti.lpszText = L"";
        SendMessageW(gPinTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        PinTipText();
    }
}

static void SetTopmost(int on, int save)
{
    gTopmost = on ? 1 : 0;
    if (gMain) {
        /* ピン (所有ウィンドウ) がある状態で切り替えると、環境によって (Wine で確認)
           オーナーの最前面属性が変わらないことがあるため、ピンを作り直す。
           新しいピンはオーナーの最前面状態を引き継いで、その直上に作られる。 */
        int hadPin = (gPin != NULL);
        int wasActive = (GetForegroundWindow() == gMain);
        HWND focus = GetFocus();
        if (hadPin) { DestroyWindow(gPin); gPin = NULL; gPinTip = NULL; }
        SetWindowPos(gMain, gTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (hadPin) CreatePin(gMain);
        /* 作り直しで入力先が外れないようにする (アクティブだったときだけ戻す) */
        if (wasActive && GetForegroundWindow() != gMain) SetForegroundWindow(gMain);
        if (wasActive && focus && IsWindow(focus) && GetFocus() != focus) SetFocus(focus);
    }
    if (save) {
        wchar_t sec[16];
        wsprintfW(sec, L"%d", gSlot);
        IniWriteInt(sec, L"topmost", gTopmost);
    }
    PinTipText();
    PositionPin();
    if (gPin) InvalidateRect(gPin, NULL, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        int i;
        gBg     = CreateSolidBrush(COL_BG);
        gBorder = CreateSolidBrush(COL_TITLE);

        gEdit = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_WANTRETURN | ES_NOHIDESEL | WS_CLIPSIBLINGS,
            0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)ID_EDIT, NULL, NULL);
        SendMessageW(gEdit, EM_SETBKGNDCOLOR, 0, COL_BG);
        SendMessageW(gEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
        RecreateFont();
        /* 修正: リッチエディットの既定上限は約32K文字。長いメモが入力不能になるのを防ぐ */
        SendMessageW(gEdit, EM_EXLIMITTEXT, 0, 0x7FFFFFFF);
        /* 修正(細字の主因): IME入力時の自動フォント切替(IMF_AUTOFONT)を止める。
           旧版では日本語を入力すると RichEdit が Segoe UI に無い文字のために
           別フォント(環境により 游ゴシック UI 等の細い書体)を勝手に割り当て、RTFに保存していた。
           一方、起動時の ApplyLayoutFormat が全体を Segoe UI に戻すため、再起動後は
           Windows のフォントリンク(Meiryo UI 等)で描画される。結果「入力直後/貼り付け部分は細い、
           再起動すると戻る」という“時々細くなる”現象になっていた。
           自動切替を止めて常にフォントリンク経由の同じ書体で描画させ、見た目を一定にする。 */
        {
            LRESULT lo = SendMessageW(gEdit, EM_GETLANGOPTIONS, 0, 0);
            lo &= ~(LRESULT)(IMF_AUTOFONT | IMF_AUTOFONTSIZEADJUST);
            SendMessageW(gEdit, EM_SETLANGOPTIONS, 0, lo);
        }
        gEditPrev = (WNDPROC)SetWindowLongPtrW(gEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);
        HideEditBar();

        {
            WNDCLASSW sc = {0};
            sc.lpfnWndProc   = SwatchProc;
            sc.hInstance     = GetModuleHandleW(NULL);
            sc.hCursor       = LoadCursor(NULL, IDC_HAND);
            sc.lpszClassName = L"StickySwatchV2";
            RegisterClassW(&sc);
        }
        for (i = 0; i < INK_COUNT; i++) {
            gSwatch[i] = CreateWindowExW(0, L"StickySwatchV2", L"",
                WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                hwnd, (HMENU)(INT_PTR)(ID_INK0 + i), NULL, NULL);
        }

        NotePath();
        LoadNote();
        if (gZoom != 100) ApplyZoom();   /* 追加(v1.1): INI の zoom= を反映 (表示のみ) */
        ColorCaption(hwnd);
#ifdef TEST_HARNESS
        SetTimer(hwnd, 99, 1500, NULL);
#endif
        SetTimer(hwnd, ID_TIMER_AUTO, AUTO_MS, NULL);
        gReady = 1;
        ReapplyInkIfCaret();
        return 0;
    }
    case WM_SIZE:
        Layout(hwnd);
        HideEditBar();
        PositionPin();
        return 0;
    case WM_MOVE:
        PositionPin();
        break;
    case WM_WINDOWPOSCHANGED:
        /* 追加(v1.1): 重なり順が変わったときもピンをオーナーの直上に保つ */
        if (!(((WINDOWPOS *)lp)->flags & SWP_NOZORDER)) PositionPin();
        break;
    case WM_MOUSEWHEEL:
        WheelScroll(wp);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc, band;
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, gBg);
        band = rc; band.right = BORDER;
        FillRect(dc, &band, gBorder);
        band = rc; band.left = rc.right - BORDER;
        FillRect(dc, &band, gBorder);
        band = rc; band.bottom = BORDER;
        FillRect(dc, &band, gBorder);
        band = rc; band.top = rc.bottom - BORDER;
        FillRect(dc, &band, gBorder);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_SAVE) {
            /* 追加: 手動保存時にも '#タイトル' 行を処理 (自動保存では入力中の文字を
               消してしまうので行わない) */
            TakeTitleLine();
            SaveNote();
        }
        else if (LOWORD(wp) == ID_ZOOM_IN)
            SetZoom(gZoom + ZOOM_STEP);
        else if (LOWORD(wp) == ID_ZOOM_OUT)
            SetZoom(gZoom - ZOOM_STEP);
        else if (LOWORD(wp) == ID_ZOOM_RESET)
            SetZoom(100);
        else if (LOWORD(wp) == IDM_UNDO)
            SendMessageW(gEdit, WM_UNDO, 0, 0);
        else if (LOWORD(wp) == IDM_CUT)
            SendMessageW(gEdit, WM_CUT, 0, 0);
        else if (LOWORD(wp) == IDM_COPY)
            SendMessageW(gEdit, WM_COPY, 0, 0);
        else if (LOWORD(wp) == IDM_PASTE)
            SendMessageW(gEdit, WM_PASTE, 0, 0);
        else if (LOWORD(wp) == IDM_DELETE)
            SendMessageW(gEdit, WM_CLEAR, 0, 0);
        else if (LOWORD(wp) == IDM_SELALL)
            SendMessageW(gEdit, EM_SETSEL, 0, -1);
        else if (LOWORD(wp) >= ID_INK0 && LOWORD(wp) < ID_INK0 + INK_COUNT) {
            int i, idx = LOWORD(wp) - ID_INK0;
            /* 追加: 選択範囲がある状態で色を押すと既存文字の色が変わる = 変更扱い */
            {
                CHARRANGE cr;
                SendMessageW(gEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
                if (cr.cpMin != cr.cpMax) SetDirty(1);
            }
            gInk = idx;
            SetTypingColor(kInk[idx]);
            for (i = 0; i < INK_COUNT; i++)
                InvalidateRect(gSwatch[i], NULL, TRUE);
            SetFocus(gEdit);
            ReapplyInkIfCaret();
            TouchIdle();
        } else if (LOWORD(wp) == ID_EDIT && HIWORD(wp) == EN_CHANGE) {
            SetDirty(1);
            HideEditBar();
            TouchIdle();
        }
        return 0;
    case WM_NOTIFY:
        if (gReady &&
            ((NMHDR *)lp)->idFrom == ID_EDIT &&
            ((NMHDR *)lp)->code == EN_SELCHANGE)
        {
            ReapplyInkIfCaret();
        }
        break;
    case WM_TIMER:
        if (wp == ID_TIMER_AUTO && gDirty)
            SaveNote();
        else if (wp == ID_TIMER_IDLE)
            ResetInkToDefault();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *m = (MINMAXINFO *)lp;
        m->ptMinTrackSize.x = 300;  /* 変更: 7色分の幅を確保 */
        m->ptMinTrackSize.y = 160;
        return 0;
    }
    case WM_QUERYENDSESSION:
        /* 修正: シャットダウン/サインアウト時は WM_CLOSE が来ないため未保存分が消えていた */
        SavePlacement(hwnd);
        TakeTitleLine();
        if (gDirty) SaveNote();
        return TRUE;
    case WM_ACTIVATE:
        ColorCaption(hwnd);
        PositionPin();
        PostMessageW(hwnd, WM_APP_PIN, 0, 0);  /* 追加(v1.1): 重なり順の確定後にもう一度 */
        break;
    case WM_APP_PIN:
        PositionPin();
        return 0;
    case WM_APP_TOPMOST:
        SetTopmost(!gTopmost, 1);
        return 0;
    case WM_CLOSE:
        SavePlacement(hwnd);
        TakeTitleLine();   /* 追加: '#タイトル' 行 → INI に保存し本文から削除 */
        if (gDirty) SaveNote();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (gFont) DeleteObject(gFont);
        if (gBg) DeleteObject(gBg);
        if (gBorder) DeleteObject(gBorder);
        if (gMutex) { CloseHandle(gMutex); gMutex = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev; (void)cmd; (void)show;
    if (!LoadLibraryW(L"Msftedit.dll"))
        return 0;

    gSlot = ClaimSlot();
    IniPath();
    LoadUserTitle();
    {   /* 追加(v1.1): ウィンドウごとの表示倍率と最前面 */
        wchar_t sec[16];
        wsprintfW(sec, L"%d", gSlot);
        gZoom = (int)GetPrivateProfileIntW(sec, L"zoom", 100, gIni);
        if (gZoom < ZOOM_MIN || gZoom > ZOOM_MAX) gZoom = 100;
        gTopmost = GetPrivateProfileIntW(sec, L"topmost", 0, gIni) ? 1 : 0;
    }
    if (gSlot <= 1)
        lstrcpyW(gTitle, L"StickyBB");
    else
        wsprintfW(gTitle, L"StickyBB %d", gSlot);

    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = inst;
    wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"StickyBB";
    wc.hIconSm       = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP),
                                         IMAGE_ICON, 16, 16, 0);
    RegisterClassExW(&wc);

    int w = 300, h = 260;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2 + (gSlot - 1) * 28;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2 + (gSlot - 1) * 28;

    gMain = CreateWindowExW(
        0,
        L"StickyBB", gUserTitle[0] ? gUserTitle : gTitle,
        WS_OVERLAPPEDWINDOW,
        x, y, w, h, NULL, NULL, inst, NULL);
    /* 追加(v1.1): 最前面ピン (メインウィンドウの作成が終わってから作る) */
    CreatePin(gMain);
    RestorePlacement(gMain);
    ShowWindow(gMain, SW_SHOW);
    if (gTopmost) SetTopmost(1, 0);   /* 追加(v1.1) */
    PositionPin();

    ACCEL acc = { FVIRTKEY | FCONTROL, 'S', ID_SAVE };
    HACCEL hAcc = CreateAcceleratorTableW(&acc, 1);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        /* 修正: 通常ウィンドウに IsDialogMessage を使うと Tab 等のキーが横取りされるため削除 */
        /* 追加(v1.1): 拡大/縮小キー。RichEdit に届く前に処理する
           (RichEdit 自身の Ctrl+= 下付き文字などに取られないように) */
        int zc = ZoomKeyCommand(&msg);
        if (zc) {
            SendMessageW(gMain, WM_COMMAND, MAKEWPARAM(zc, 0), 0);
            continue;
        }
        if (!TranslateAcceleratorW(gMain, hAcc, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DestroyAcceleratorTable(hAcc);
    return 0;
}
