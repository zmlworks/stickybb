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
#define COL_PIN_HOT RGB(0x5A, 0x16, 0x1C)   /* 追加(v1.1): ピンのオン背景 */

/* ---- 追加(v1.2): 自前で描くタイトルバー ----
   Win11 の標準キャプション (DWM が描く) と同じ見た目・動作を、ボタン幅だけ狭くして再現する。
   サイズは論理px (96DPI 基準)。GetDpiForWindow の値で換算するが、このアプリは DPI 非対応
   (Windows が拡大表示) のままなので、実際には常に 96 として扱われる (表示は従来どおり)。 */
#define CAPBTN_W        32    /* キャプションボタンの幅 (標準は 46) */
#define CAP_GLYPH_PX    10    /* 最小化/最大化/閉じる のグリフの大きさ (標準と同じ) */
#define PIN_GLYPH_PX    16    /* ピンのグリフ (16px で線の太さ 1px = 他のボタンと同じ) */
#define CAP_ICON_PX     16
#define CAP_PAD         8     /* アイコン/タイトルの左右余白 */
#define TITLE_MIN_W     40    /* タイトルを省略表示 (…) してでも出す最小幅。これ未満なら隠す */
#define SWATCH_MARGIN   4     /* 色ボタン列の左右の最小余白 */
#define COL_GLYPH       RGB(0xFF, 0xFF, 0xFF)   /* アクティブ時のグリフ */
#define INACTIVE_ALPHA  100   /* 非アクティブ時: 白を 100/255 (≈39%) で重ねた色 = #896466 (実測値) */
#define HOVER_ALPHA     15    /* ホバー背景: 白 6% (Win11 SubtleFillColorSecondary) */
#define PRESS_ALPHA     10    /* 押下背景:   白 4% (Win11 SubtleFillColorTertiary) */
#define COL_CLOSE_HOT   RGB(0xC4, 0x2B, 0x1C)   /* 閉じる ホバー (Win11 と同じ) */
#define CLOSE_PRESS_ALPHA 230 /* 閉じる 押下: #C42B1C を 90% */
#define PRESS_GLYPH_ALPHA 200 /* 押下中のグリフは少し暗く */
/* 追加(v1.2): スクロールの手がかり (枠の上下を少し明るく) */
#define HINT_PX         3
#define COL_HINT        RGB(0x6A, 0x1A, 0x20)
#define WM_APP_HINT     (WM_APP + 3)

enum { CB_NONE = 0, CB_PIN, CB_MIN, CB_MAX, CB_CLOSE };

/* 追加(v1.1): 表示倍率 (EM_SETZOOM, 単位 %) */
#define ZOOM_MIN   50
#define ZOOM_MAX   300
#define ZOOM_STEP  10
#define ID_ZOOM_IN    301
#define ID_ZOOM_OUT   302
#define ID_ZOOM_RESET 303

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
/* 追加(v1.2): 自前のキャプション */
static int     gCapH = 31;           /* キャプションの高さ (クライアント座標, WM_NCCALCSIZE で決まる) */
static int     gActive = 0;          /* WM_NCACTIVATE で更新 */
static int     gHot = CB_NONE;       /* マウスが乗っているボタン */
static int     gPressed = CB_NONE;   /* 押しているボタン (キャプチャ中) */
static int     gTrackNC = 0, gTrackC = 0;
static HFONT   gCapFont, gGlyphFont, gPinFont;
static int     gGlyphFontOK = 0;     /* 1: Segoe Fluent Icons / Segoe MDL2 Assets が使える */
static HICON   gIconSm;
static HWND    gTip = NULL;          /* キャプションボタンのツールチップ */
static int     gHintUp = 0, gHintDown = 0, gHintPosted = 0;
static int     gLineH = 27;          /* 1行の高さの目安 (100%) */



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
   タイトル文字列の先頭に「● 」を付ける (v1.2: キャプションは自前で描くが、文字列は同じものを使う) */
static void UpdateTitle(void)
{
    wchar_t buf[TITLE_MAX + 8];
    if (!gMain) return;
    lstrcpyW(buf, gDirty ? L"\x25CF " : L"");
    lstrcatW(buf, gUserTitle[0] ? gUserTitle : gTitle);
    SetWindowTextW(gMain, buf);   /* WM_SETTEXT でキャプションを描き直す */
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
static void PostHint(void);

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
    PostHint();
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
    if (w < 200) w = 200;   /* 変更(v1.2): 300 → 200 (狭くしたとき色ボタンを詰める) */
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
        PostHint();
        return;
    }
    n = -(delta / WHEEL_DELTA) * (int)lines;
    if (n) SendMessageW(gEdit, EM_LINESCROLL, 0, n);
    PostHint();
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

/* 変更(v1.2): 幅が足りないときは色ボタンの間隔を詰める (最大 SWATCH_GAP, 最小 0) */
static int SwatchGap(int cw)
{
    int spare = cw - BORDER * 2 - SWATCH_MARGIN * 2 - INK_COUNT * SWATCH_BOX;
    int gap = spare / (INK_COUNT - 1);
    if (gap > SWATCH_GAP) gap = SWATCH_GAP;
    if (gap < 0) gap = 0;
    return gap;
}

static void UpdateTipRects(HWND hwnd);

static void Layout(HWND hwnd)
{
    RECT rc; GetClientRect(hwnd, &rc);
    /* 変更(v1.2): キャプションをクライアント領域に描くので、その分 (gCapH) 下げる */
    int top = gCapH + BORDER + PAD;
    int editBottom = rc.bottom - BORDER - BAR_H;
    MoveWindow(gEdit,
               BORDER + PAD,
               top,
               rc.right - BORDER * 2 - PAD * 2,
               editBottom - top,
               TRUE);
    {
        int gap = SwatchGap(rc.right), box = SWATCH_BOX;
        int total = INK_COUNT * box + (INK_COUNT - 1) * gap;
        int x0 = (rc.right - total) / 2;
        int y0 = rc.bottom - BORDER - (BAR_H + box) / 2;
        int i;
        for (i = 0; i < INK_COUNT; i++)
            MoveWindow(gSwatch[i], x0 + i * (box + gap), y0, box, box, TRUE);
    }
    UpdateTipRects(hwnd);
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
        PostHint();
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

/* ---- 追加(v1.2): 自前のタイトルバー (キャプション) ----
   WM_NCCALCSIZE でクライアント領域をタイトルバーの位置まで広げ (左右下のリサイズ枠は残す)、
   タイトルバーをクライアント領域に自分で描く。Win11 の枠線・影・角丸は DWM がそのまま付ける
   (左右下の枠を残しているので DwmExtendFrameIntoClientArea は不要。広げると DWM が
   標準のボタンを描いてしまうため使わない)。
   右から 閉じる・最大化・最小化・ピン の順に並べ、残りがアイコンとタイトル (ドラッグ領域)。
   幅が足りないときはタイトルから省略・非表示にする (優先度: キャプションボタン > ピン > タイトル)。 */
static void SetTopmost(int on, int save);

static COLORREF Blend(COLORREF fg, COLORREF bg, int a)   /* a = fg の割合 0..255 */
{
    return RGB((GetRValue(fg) * a + GetRValue(bg) * (255 - a) + 127) / 255,
               (GetGValue(fg) * a + GetGValue(bg) * (255 - a) + 127) / 255,
               (GetBValue(fg) * a + GetBValue(bg) * (255 - a) + 127) / 255);
}

static UINT WinDpi(HWND h)
{
    typedef UINT (WINAPI *FN)(HWND);
    static FN fn = NULL;
    static int init = 0;
    UINT d = 0;
    if (!init) {
        init = 1;
        fn = (FN)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    }
    if (fn && h) d = fn(h);
    return d ? d : 96;   /* DPI 非対応のプロセスでは常に 96 (Windows が拡大表示する) */
}

static int SysMetric(int idx, UINT dpi)
{
    typedef int (WINAPI *FN)(int, UINT);
    static FN fn = NULL;
    static int init = 0;
    if (!init) {
        init = 1;
        fn = (FN)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi");
    }
    return fn ? fn(idx, dpi) : GetSystemMetrics(idx);
}

static int Sc(int v) { return MulDiv(v, (int)WinDpi(gMain), 96); }

static int CALLBACK FontFoundProc(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM lp)
{
    (void)lf; (void)tm; (void)type;
    *(int *)lp = 1;
    return 0;
}

static int FontExists(const wchar_t *face)
{
    LOGFONTW lf;
    int found = 0;
    HDC dc = GetDC(NULL);
    ZeroMemory(&lf, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, face, LF_FACESIZE);
    EnumFontFamiliesExW(dc, &lf, (FONTENUMPROCW)FontFoundProc, (LPARAM)&found, 0);
    ReleaseDC(NULL, dc);
    return found;
}

static int GlyphsPresent(HFONT f, const wchar_t *s)
{
    WORD idx[8];
    int n = lstrlenW(s), i, ok = 1;
    HDC dc = GetDC(NULL);
    HGDIOBJ of = SelectObject(dc, f);
    if (n > 8 || GetGlyphIndicesW(dc, s, n, idx, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR) ok = 0;
    for (i = 0; ok && i < n; i++)
        if (idx[i] == 0xFFFF) ok = 0;
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
    return ok;
}

/* タイトル文字のフォント (システムのキャプションフォント) と、ボタンのグリフ用フォント。
   グリフは Segoe Fluent Icons (Win11) → Segoe MDL2 Assets (Win10) の順に探し、
   どちらも無ければ自前のベクター描画 (線の太さは同じ 1 論理px) にする。フォントは同梱しない。 */
static void CreateCaptionFonts(HWND hwnd)
{
    static const wchar_t *faces[2] = { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" };
    UINT dpi = WinDpi(hwnd);
    NONCLIENTMETRICSW nm;
    int i;
    if (gCapFont) { DeleteObject(gCapFont); gCapFont = NULL; }
    if (gGlyphFont) { DeleteObject(gGlyphFont); gGlyphFont = NULL; }
    if (gPinFont) { DeleteObject(gPinFont); gPinFont = NULL; }
    ZeroMemory(&nm, sizeof(nm));
    nm.cbSize = sizeof(nm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0)) {
        if (dpi != 96) nm.lfCaptionFont.lfHeight = MulDiv(nm.lfCaptionFont.lfHeight, (int)dpi, 96);
        gCapFont = CreateFontIndirectW(&nm.lfCaptionFont);
    }
    gGlyphFontOK = 0;
    for (i = 0; i < 2 && !gGlyphFontOK; i++) {
        if (!FontExists(faces[i])) continue;
        gGlyphFont = CreateFontW(-MulDiv(CAP_GLYPH_PX, (int)dpi, 96), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, 0, ANTIALIASED_QUALITY,
                                 DEFAULT_PITCH, faces[i]);
        gPinFont = CreateFontW(-MulDiv(PIN_GLYPH_PX, (int)dpi, 96), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, 0, ANTIALIASED_QUALITY,
                               DEFAULT_PITCH, faces[i]);
        if (gGlyphFont && gPinFont &&
            GlyphsPresent(gGlyphFont, L"\xE921\xE922\xE923\xE8BB") &&
            GlyphsPresent(gPinFont, L"\xE718\xE842"))
            gGlyphFontOK = 1;
        else {
            if (gGlyphFont) { DeleteObject(gGlyphFont); gGlyphFont = NULL; }
            if (gPinFont) { DeleteObject(gPinFont); gPinFont = NULL; }
        }
    }
}

typedef struct {
    RECT btn[5];        /* CB_PIN..CB_CLOSE */
    RECT icon, text;
    int  showIcon;
} CapLayout;

static void CalcCaption(int cw, CapLayout *L)
{
    int bw = Sc(CAPBTN_W), h = gCapH, x = cw, b;
    int pad = Sc(CAP_PAD), ic = Sc(CAP_ICON_PX);
    ZeroMemory(L, sizeof(*L));
    for (b = CB_CLOSE; b >= CB_PIN; b--) {      /* 右から 閉じる, 最大化, 最小化, ピン */
        SetRect(&L->btn[b], x - bw, 0, x, h);
        x -= bw;
    }
    L->showIcon = (x >= pad + ic + pad);
    if (L->showIcon)
        SetRect(&L->icon, pad, (h - ic) / 2, pad + ic, (h - ic) / 2 + ic);
    SetRect(&L->text, L->showIcon ? pad + ic + pad : pad, 0, x - pad, h);
}

/* クライアント座標 pt にあるボタン */
static int CapButtonAt(HWND hwnd, POINT pt)
{
    RECT rc;
    CapLayout L;
    int b;
    if (pt.y < 0 || pt.y >= gCapH) return CB_NONE;
    GetClientRect(hwnd, &rc);
    CalcCaption(rc.right, &L);
    for (b = CB_PIN; b <= CB_CLOSE; b++)
        if (PtInRect(&L.btn[b], pt)) return b;
    return CB_NONE;
}

static LRESULT CaptionHitTest(HWND hwnd, POINT pt)
{
    RECT rc;
    CapLayout L;
    if (pt.y < 0 || pt.y >= gCapH) return HTCLIENT;
    GetClientRect(hwnd, &rc);
    if (!IsZoomed(hwnd)) {   /* 上端はリサイズ */
        UINT dpi = WinDpi(hwnd);
        int edge = SysMetric(SM_CYFRAME, dpi), corner = Sc(16);
        if (pt.y < edge) {
            if (pt.x < corner) return HTTOPLEFT;
            if (pt.x >= rc.right - corner) return HTTOPRIGHT;
            return HTTOP;
        }
    }
    switch (CapButtonAt(hwnd, pt)) {
    case CB_MIN:   return HTMINBUTTON;
    case CB_MAX:   return HTMAXBUTTON;    /* Win11 のスナップレイアウトが出る */
    case CB_CLOSE: return HTCLOSE;
    case CB_PIN:   return HTCLIENT;       /* ピンは普通のクライアントのボタン */
    }
    CalcCaption(rc.right, &L);
    if (L.showIcon && pt.x >= 0 && pt.x < L.icon.right + Sc(4)) return HTSYSMENU;
    return HTCAPTION;
}

static void InvalidateCaption(HWND hwnd)
{
    RECT rc;
    if (!hwnd) return;
    GetClientRect(hwnd, &rc);
    rc.bottom = gCapH;
    InvalidateRect(hwnd, &rc, FALSE);
}

static void SetHot(HWND hwnd, int b)
{
    if (gHot == b) return;
    gHot = b;
    InvalidateCaption(hwnd);
}

/* Win11 の標準キャプションボタンと同じ配色 */
static void CapButtonColors(int b, COLORREF *bg, COLORREF *fg)
{
    int hot = (gHot == b), pressed = (gPressed == b && hot);
    *bg = COL_TITLE;
    *fg = gActive ? COL_GLYPH : Blend(COL_GLYPH, COL_TITLE, INACTIVE_ALPHA);
    if (b == CB_CLOSE) {
        if (pressed) {
            *bg = Blend(COL_CLOSE_HOT, COL_TITLE, CLOSE_PRESS_ALPHA);
            *fg = Blend(COL_GLYPH, *bg, PRESS_GLYPH_ALPHA);
        } else if (hot) {
            *bg = COL_CLOSE_HOT;
            *fg = COL_GLYPH;
        }
    } else {
        if (pressed) {
            *bg = Blend(COL_GLYPH, COL_TITLE, PRESS_ALPHA);
            *fg = Blend(COL_GLYPH, *bg, PRESS_GLYPH_ALPHA);
        } else if (hot) {
            *bg = Blend(COL_GLYPH, COL_TITLE, HOVER_ALPHA);
            *fg = COL_GLYPH;
        }
    }
}

static void FillPx(HDC dc, int l, int t, int r, int b, int S, HBRUSH br)
{
    RECT q;
    SetRect(&q, l * S, t * S, r * S, b * S);
    FillRect(dc, &q, br);
}

/* フォントが無い環境用のベクター描画 (4倍で描いてから縮小)。
   線の太さはすべて 1 論理px (= Segoe のキャプショングリフと同じ)。
   S = 倍率 (4), u = 1 論理px あたりの実ピクセル (DPI), cx/cy = 中心 (縮小後の px) */
static void DrawVectorGlyph(HDC m, int b, int w, int h, int S, COLORREF fg)
{
    float u = (float)WinDpi(gMain) / 96.0f;
    int cx = w / 2, cy = h / 2;
    int g = (int)(5 * u + 0.5f), t = (int)(u + 0.5f);   /* 半径 5px, 線 1px */
    HBRUSH br = CreateSolidBrush(fg);
    if (t < 1) t = 1;
    switch (b) {
    case CB_MIN:
        FillPx(m, cx - g, cy, cx + g, cy + t, S, br);
        break;
    case CB_MAX:
        if (IsZoomed(gMain)) {   /* 元に戻す: 前の四角 8x8 + 後ろの四角の見える部分 */
            int o = (int)(2 * u + 0.5f);
            int l = cx - g, tp = cy - g + o, r = cx + g - o, bt = cy + g;
            FillPx(m, l, tp, r, tp + t, S, br);
            FillPx(m, l, bt - t, r, bt, S, br);
            FillPx(m, l, tp, l + t, bt, S, br);
            FillPx(m, r - t, tp, r, bt, S, br);
            FillPx(m, l + o, cy - g, cx + g, cy - g + t, S, br);      /* 後ろの上辺 */
            FillPx(m, cx + g - t, cy - g, cx + g, bt - o, S, br);      /* 後ろの右辺 */
            FillPx(m, l + o, cy - g, l + o + t, tp, S, br);
            FillPx(m, r, bt - o - t, cx + g, bt - o, S, br);
        } else {
            FillPx(m, cx - g, cy - g, cx + g, cy - g + t, S, br);
            FillPx(m, cx - g, cy + g - t, cx + g, cy + g, S, br);
            FillPx(m, cx - g, cy - g, cx - g + t, cy + g, S, br);
            FillPx(m, cx + g - t, cy - g, cx + g, cy + g, S, br);
        }
        break;
    case CB_CLOSE: {
        LOGBRUSH lb;
        HPEN pen;
        HGDIOBJ op;
        lb.lbStyle = BS_SOLID; lb.lbColor = fg; lb.lbHatch = 0;
        pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT, (DWORD)(t * S), &lb, 0, NULL);
        op = SelectObject(m, pen);
        MoveToEx(m, (cx - g) * S, (cy - g) * S, NULL); LineTo(m, (cx + g) * S, (cy + g) * S);
        MoveToEx(m, (cx + g) * S, (cy - g) * S, NULL); LineTo(m, (cx - g) * S, (cy + g) * S);
        SelectObject(m, op);
        DeleteObject(pen);
        break;
    }
    case CB_PIN: {
        /* 画鋲 (高さ約 14px)。オフ: 45度傾いた輪郭 / オン: まっすぐ・塗りつぶし */
        static const float shape[][2] = {
            { -3.5f, -7.0f }, { 3.5f, -7.0f }, { 2.5f, -5.5f }, { 2.5f, -1.0f },
            { 5.0f, 1.5f }, { -5.0f, 1.5f }, { -2.5f, -1.0f }, { -2.5f, -5.5f }
        };
        POINT pt[8], nd[2];
        float k = u * S, cs = gTopmost ? 1.0f : 0.7071f, sn = gTopmost ? 0.0f : 0.7071f;
        float fx = cx * (float)S, fy = (cy + 0.5f) * (float)S;
        LOGBRUSH lb;
        HPEN pen;
        HBRUSH fill;
        HGDIOBJ op, ob;
        int i;
        for (i = 0; i < 8; i++) {
            float x = shape[i][0] * k, y = shape[i][1] * k;
            pt[i].x = (LONG)(fx + x * cs - y * sn);
            pt[i].y = (LONG)(fy + x * sn + y * cs);
        }
        nd[0].x = (LONG)(fx - 1.5f * k * sn); nd[0].y = (LONG)(fy + 1.5f * k * cs);
        nd[1].x = (LONG)(fx - 7.0f * k * sn); nd[1].y = (LONG)(fy + 7.0f * k * cs);
        lb.lbStyle = BS_SOLID; lb.lbColor = fg; lb.lbHatch = 0;
        pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT | PS_JOIN_MITER,
                           (DWORD)(t * S), &lb, 0, NULL);
        fill = gTopmost ? CreateSolidBrush(fg) : NULL;
        op = SelectObject(m, pen);
        ob = SelectObject(m, fill ? (HGDIOBJ)fill : GetStockObject(NULL_BRUSH));
        Polygon(m, pt, 8);
        MoveToEx(m, nd[0].x, nd[0].y, NULL);
        LineTo(m, nd[1].x, nd[1].y);
        SelectObject(m, op); SelectObject(m, ob);
        DeleteObject(pen);
        if (fill) DeleteObject(fill);
        break;
    }
    }
    DeleteObject(br);
}

/* Segoe Fluent Icons / MDL2 のグリフを、黒い部分 (black box) の中心がボタンの中心に来るよう描く */
static void DrawFontGlyph(HDC dc, const RECT *r, int b, COLORREF fg)
{
    wchar_t ch = 0;
    HFONT f = (b == CB_PIN) ? gPinFont : gGlyphFont;
    GLYPHMETRICS gm;
    MAT2 mat;
    HGDIOBJ of;
    int cx = (r->left + r->right) / 2, cy = (r->top + r->bottom) / 2;
    switch (b) {
    case CB_PIN:   ch = gTopmost ? 0xE842 /* PinnedFill */ : 0xE718 /* Pin */; break;
    case CB_MIN:   ch = 0xE921; break;
    case CB_MAX:   ch = IsZoomed(gMain) ? 0xE923 : 0xE922; break;
    case CB_CLOSE: ch = 0xE8BB; break;
    }
    if (!ch || !f) return;
    of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    ZeroMemory(&mat, sizeof(mat));
    mat.eM11.value = 1; mat.eM22.value = 1;
    if (GetGlyphOutlineW(dc, ch, GGO_METRICS, &gm, 0, NULL, &mat) != GDI_ERROR && gm.gmBlackBoxX > 0) {
        UINT oa = SetTextAlign(dc, TA_BASELINE | TA_LEFT);
        int x = cx - (int)gm.gmBlackBoxX / 2 - gm.gmptGlyphOrigin.x;
        int y = cy - (int)gm.gmBlackBoxY / 2 + gm.gmptGlyphOrigin.y;
        ExtTextOutW(dc, x, y, 0, NULL, &ch, 1, NULL);
        SetTextAlign(dc, oa);
    } else {
        RECT t = *r;
        DrawTextW(dc, &ch, 1, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, of);
}

static void DrawCapButton(HDC dc, const RECT *r, int b)
{
    COLORREF bg, fg;
    int w = r->right - r->left, h = r->bottom - r->top;
    int onBg = (b == CB_PIN && gTopmost);
    if (w <= 0 || h <= 0) return;
    CapButtonColors(b, &bg, &fg);
    if (!gGlyphFontOK || onBg) {
        const int S = 4;
        HDC m = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, w * S, h * S);
        HGDIOBJ ob = SelectObject(m, bmp);
        RECT q = { 0, 0, w * S, h * S };
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(m, &q, br);
        DeleteObject(br);
        if (onBg) {   /* オン: 角丸の背景 #5A161C (v1.1 と同じ色) */
            int in = Sc(3), rr = Sc(8);
            HBRUSH hb = CreateSolidBrush(gHot == b ? Blend(COL_GLYPH, COL_PIN_HOT, HOVER_ALPHA) : COL_PIN_HOT);
            HGDIOBJ o1 = SelectObject(m, hb), o2 = SelectObject(m, GetStockObject(NULL_PEN));
            RoundRect(m, in * S, in * S, (w - in) * S + 1, (h - in) * S + 1, rr * S, rr * S);
            SelectObject(m, o1); SelectObject(m, o2);
            DeleteObject(hb);
        }
        if (!gGlyphFontOK) DrawVectorGlyph(m, b, w, h, S, fg);
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        StretchBlt(dc, r->left, r->top, w, h, m, 0, 0, w * S, h * S, SRCCOPY);
        SelectObject(m, ob);
        DeleteObject(bmp);
        DeleteDC(m);
    } else {
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(dc, r, br);
        DeleteObject(br);
    }
    if (gGlyphFontOK) DrawFontGlyph(dc, r, b, fg);
}

/* キャプション全体を裏で描いてから一度に転送する (ちらつき防止) */
static void PaintCaption(HWND hwnd, HDC dc, int cw)
{
    int h = gCapH, b;
    CapLayout L;
    HDC m;
    HBITMAP bmp;
    HGDIOBJ ob;
    RECT r;
    wchar_t t[TITLE_MAX + 16];
    int n;
    if (cw <= 0 || h <= 0) return;
    CalcCaption(cw, &L);
    m = CreateCompatibleDC(dc);
    bmp = CreateCompatibleBitmap(dc, cw, h);
    ob = SelectObject(m, bmp);
    SetRect(&r, 0, 0, cw, h);
    FillRect(m, &r, gBorder);
    for (b = CB_PIN; b <= CB_CLOSE; b++)
        DrawCapButton(m, &L.btn[b], b);
    if (L.showIcon && gIconSm)
        DrawIconEx(m, L.icon.left, L.icon.top, gIconSm, L.icon.right - L.icon.left,
                   L.icon.bottom - L.icon.top, 0, NULL, DI_NORMAL);
    n = GetWindowTextW(hwnd, t, TITLE_MAX + 16);
    if (n > 0 && L.text.right > L.text.left) {
        /* タイトルは入りきれば全部、足りなければ「…」で省略、TITLE_MIN_W 未満なら隠す */
        SIZE sz;
        int avail = L.text.right - L.text.left;
        HGDIOBJ of = SelectObject(m, gCapFont ? (HGDIOBJ)gCapFont : GetStockObject(DEFAULT_GUI_FONT));
        GetTextExtentPoint32W(m, t, n, &sz);
        if (sz.cx <= avail || avail >= Sc(TITLE_MIN_W)) {
            SetBkMode(m, TRANSPARENT);
            /* 非アクティブ時は Windows と同じように暗くする (ボタンのグリフと同じ割合) */
            SetTextColor(m, gActive ? COL_FG : Blend(COL_FG, COL_TITLE, INACTIVE_ALPHA));
            DrawTextW(m, t, n, &L.text,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        SelectObject(m, of);
    }
    BitBlt(dc, 0, 0, cw, h, m, 0, 0, SRCCOPY);
    SelectObject(m, ob);
    DeleteObject(bmp);
    DeleteDC(m);
}

/* ---- ツールチップ (ピン・最小化・最大化・閉じる) ---- */
static void TipSetText(int id, const wchar_t *s)
{
    TOOLINFOW ti;
    if (!gTip || !gMain) return;
    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize = TTTOOLINFOW_V2_SIZE;   /* comctl32 v5 (マニフェスト無し) でも通るサイズ */
    ti.hwnd = gMain;
    ti.uId = (UINT_PTR)id;
    ti.lpszText = (LPWSTR)s;
    SendMessageW(gTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}

static void UpdateTips(void)
{
    TipSetText(CB_PIN, gTopmost ? L"最前面に表示: オン（クリックで解除）"
                                : L"最前面に表示: オフ（クリックで常に手前に表示）");
    TipSetText(CB_MAX, (gMain && IsZoomed(gMain)) ? L"元に戻す (縮小)" : L"最大化");
}

static void UpdateTipRects(HWND hwnd)
{
    RECT rc;
    CapLayout L;
    int b;
    if (!gTip) return;
    GetClientRect(hwnd, &rc);
    CalcCaption(rc.right, &L);
    for (b = CB_PIN; b <= CB_CLOSE; b++) {
        TOOLINFOW ti;
        ZeroMemory(&ti, sizeof(ti));
        ti.cbSize = TTTOOLINFOW_V2_SIZE;
        ti.hwnd = hwnd;
        ti.uId = (UINT_PTR)b;
        ti.rect = L.btn[b];
        SendMessageW(gTip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti);
    }
}

static void CreateTips(HWND hwnd)
{
    static const wchar_t *text[5] = { L"", L"", L"最小化", L"最大化", L"閉じる" };
    INITCOMMONCONTROLSEX ic;
    RECT rc;
    CapLayout L;
    int b;
    ic.dwSize = sizeof(ic);
    ic.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&ic);
    gTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                           WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                           hwnd, NULL, GetModuleHandleW(NULL), NULL);
    if (!gTip) return;
    GetClientRect(hwnd, &rc);
    CalcCaption(rc.right, &L);
    for (b = CB_PIN; b <= CB_CLOSE; b++) {
        TOOLINFOW ti;
        ZeroMemory(&ti, sizeof(ti));
        ti.cbSize = TTTOOLINFOW_V2_SIZE;
        ti.uFlags = TTF_SUBCLASS;     /* ピン (クライアント領域) のマウス移動は自動で中継 */
        ti.hwnd = hwnd;
        ti.uId = (UINT_PTR)b;
        ti.rect = L.btn[b];
        ti.lpszText = (LPWSTR)text[b];
        SendMessageW(gTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
    UpdateTips();
}

/* 最小化/最大化/閉じる は非クライアント扱い (WM_NCMOUSEMOVE) なので、自分で中継する */
static void RelayTipNC(HWND hwnd, LPARAM lp)
{
    MSG mm;
    POINT pt;
    if (!gTip) return;
    pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp);
    mm.hwnd = hwnd;
    mm.message = WM_MOUSEMOVE;
    mm.wParam = 0;
    mm.pt = pt;
    mm.time = (DWORD)GetMessageTime();
    ScreenToClient(hwnd, &pt);
    mm.lParam = MAKELPARAM(pt.x, pt.y);
    SendMessageW(gTip, TTM_RELAYEVENT, 0, (LPARAM)&mm);
}

static int HitToButton(WPARAM ht)
{
    switch (ht) {
    case HTMINBUTTON: return CB_MIN;
    case HTMAXBUTTON: return CB_MAX;
    case HTCLOSE:     return CB_CLOSE;
    }
    return CB_NONE;
}

static void TrackLeave(HWND hwnd, int nc)
{
    TRACKMOUSEEVENT t;
    t.cbSize = sizeof(t);
    t.dwFlags = TME_LEAVE | (nc ? TME_NONCLIENT : 0);
    t.hwndTrack = hwnd;
    t.dwHoverTime = 0;
    if (TrackMouseEvent(&t)) {
        if (nc) gTrackNC = 1; else gTrackC = 1;
    }
}

static void DoCapButton(HWND hwnd, int b)
{
    switch (b) {
    case CB_PIN:   SetTopmost(!gTopmost, 1); break;
    case CB_MIN:   SendMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0); break;
    case CB_MAX:   SendMessageW(hwnd, WM_SYSCOMMAND, IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0); break;
    case CB_CLOSE: SendMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0); break;
    }
}

/* タイトルの右クリック用のシステムメニュー。DefWindowProc の WM_CONTEXTMENU は内部の
   当たり判定 (自前キャプションは「クライアント」扱い) を使うので、自分で出す */
static void ShowSystemMenu(HWND hwnd, int x, int y)
{
    HMENU sm = GetSystemMenu(hwnd, FALSE);
    int zoomed = IsZoomed(hwnd), cmd;
    if (!sm) return;
    EnableMenuItem(sm, SC_RESTORE,  MF_BYCOMMAND | (zoomed ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(sm, SC_MOVE,     MF_BYCOMMAND | (zoomed ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(sm, SC_SIZE,     MF_BYCOMMAND | (zoomed ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(sm, SC_MINIMIZE, MF_BYCOMMAND | MF_ENABLED);
    EnableMenuItem(sm, SC_MAXIMIZE, MF_BYCOMMAND | (zoomed ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(sm, SC_CLOSE,    MF_BYCOMMAND | MF_ENABLED);
    SetMenuDefaultItem(sm, SC_CLOSE, FALSE);
    cmd = (int)TrackPopupMenu(sm, TPM_RETURNCMD | TPM_RIGHTBUTTON |
                              (GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN),
                              x, y, 0, hwnd, NULL);
    if (cmd) PostMessageW(hwnd, WM_SYSCOMMAND, (WPARAM)cmd, MAKELPARAM(x, y));
}

static void SetTopmost(int on, int save)
{
    gTopmost = on ? 1 : 0;
    /* 変更(v1.2): ピンは自前のキャプション内のボタンになったので、別ウィンドウの作り直しは不要 */
    if (gMain)
        SetWindowPos(gMain, gTopmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (save) {
        wchar_t sec[16];
        wsprintfW(sec, L"%d", gSlot);
        IniWriteInt(sec, L"topmost", gTopmost);
    }
    UpdateTips();
    InvalidateCaption(gMain);
}

/* ---- 追加(v1.2): スクロールの手がかり ----
   上にまだ文字がある (先頭が見えていない) → 枠の上辺、下にまだある → 枠の下辺を少し明るくする。
   表示座標 (EM_POSFROMCHAR) だけで判定するので、表示倍率 (EM_SETZOOM) にも左右されない。 */
static void InvalidateHintBands(void)
{
    RECT rc, r;
    if (!gMain) return;
    GetClientRect(gMain, &rc);
    SetRect(&r, 0, gCapH, rc.right, gCapH + BORDER);
    InvalidateRect(gMain, &r, FALSE);
    SetRect(&r, 0, rc.bottom - BORDER, rc.right, rc.bottom);
    InvalidateRect(gMain, &r, FALSE);
}

static void UpdateScrollHint(void)
{
    RECT fr, cr;
    POINTL p0, pl, pp;
    LONG lines, li;
    int lh = 0, up, down, bottom;
    if (!gEdit || !gMain) return;
    SendMessageW(gEdit, EM_GETRECT, 0, (LPARAM)&fr);
    GetClientRect(gEdit, &cr);
    bottom = (fr.bottom < cr.bottom) ? fr.bottom : cr.bottom;
    p0.x = p0.y = 0;
    SendMessageW(gEdit, EM_POSFROMCHAR, (WPARAM)&p0, 0);
    up = (p0.y < fr.top);
    lines = (LONG)SendMessageW(gEdit, EM_GETLINECOUNT, 0, 0);
    if (lines < 1) lines = 1;
    li = (LONG)SendMessageW(gEdit, EM_LINEINDEX, (WPARAM)(lines - 1), 0);
    pl.x = pl.y = 0;
    SendMessageW(gEdit, EM_POSFROMCHAR, (WPARAM)&pl, li);
    if (lines >= 2) {
        li = (LONG)SendMessageW(gEdit, EM_LINEINDEX, (WPARAM)(lines - 2), 0);
        pp.x = pp.y = 0;
        SendMessageW(gEdit, EM_POSFROMCHAR, (WPARAM)&pp, li);
        lh = pl.y - pp.y;
    }
    if (lh <= 0) lh = MulDiv(gLineH, gZoom, 100);
    down = (pl.y + lh > bottom + 1);
    if (up != gHintUp || down != gHintDown) {
        gHintUp = up;
        gHintDown = down;
        InvalidateHintBands();
    }
}

static void PostHint(void)
{
    if (!gMain || gHintPosted) return;
    gHintPosted = 1;
    PostMessageW(gMain, WM_APP_HINT, 0, 0);
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
        /* 変更(v1.2): ENM_UPDATE / ENM_SCROLL はスクロールの手がかりの更新用 */
        SendMessageW(gEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_UPDATE | ENM_SCROLL);
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
        CreateCaptionFonts(hwnd);   /* 追加(v1.2) */
        gIconSm = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP),
                                    IMAGE_ICON, Sc(CAP_ICON_PX), Sc(CAP_ICON_PX), 0);
#ifdef TEST_HARNESS
        SetTimer(hwnd, 99, 1500, NULL);
#endif
        SetTimer(hwnd, ID_TIMER_AUTO, AUTO_MS, NULL);
        gReady = 1;
        ReapplyInkIfCaret();
        return 0;
    }
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            gHot = CB_NONE;
            return 0;
        }
        Layout(hwnd);
        HideEditBar();
        UpdateTips();
        PostHint();
        return 0;

    /* ---- 追加(v1.2): 自前のキャプション ---- */
    case WM_NCCALCSIZE: {
        /* 標準の計算 (左右下のリサイズ枠) を使い、上だけキャプション分を取り消す */
        RECT *r = wp ? &((NCCALCSIZE_PARAMS *)lp)->rgrc[0] : (RECT *)lp;
        RECT orig = *r;
        LRESULT res = DefWindowProcW(hwnd, msg, wp, lp);
        int top = r->top - orig.top, side = r->left - orig.left;
        if (IsZoomed(hwnd)) {
            /* 最大化時はウィンドウが枠の分だけ画面外にはみ出すので、その分は下げる */
            if (top - side >= 8 && top - side < 200) gCapH = top - side;
            r->top = orig.top + side;
        } else {
            if (!IsIconic(hwnd) && top >= 8 && top < 200) gCapH = top;
            r->top = orig.top;
        }
        return res;
    }
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);   /* 左右下の枠はそのまま */
        POINT pt;
        if (hit != HTCLIENT) return hit;
        pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp);
        ScreenToClient(hwnd, &pt);
        return CaptionHitTest(hwnd, pt);
    }
    case WM_NCACTIVATE:
        gActive = wp ? 1 : 0;
        InvalidateCaption(hwnd);
        /* lParam = -1: 標準のキャプションを描かせない */
        return DefWindowProcW(hwnd, msg, wp, (LPARAM)-1);
    case WM_SETTEXT: {
        LRESULT r = DefWindowProcW(hwnd, msg, wp, lp);
        InvalidateCaption(hwnd);
        return r;
    }
    case WM_NCMOUSEMOVE: {
        int b = HitToButton(wp);
        if (!gTrackNC) TrackLeave(hwnd, 1);
        RelayTipNC(hwnd, lp);
        if (!gPressed) SetHot(hwnd, b);
        if (b) return 0;   /* ボタンの上は DefWindowProc に渡さない (標準ボタンを描かせない) */
        break;
    }
    case WM_NCMOUSELEAVE:
        gTrackNC = 0;
        if (!gPressed && gHot >= CB_MIN) SetHot(hwnd, CB_NONE);
        return 0;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK: {
        int b = HitToButton(wp);
        if (!b) break;     /* タイトル (移動/ダブルクリックで最大化)・枠・アイコンは標準の処理 */
        if (gTip) SendMessageW(gTip, TTM_POP, 0, 0);
        gPressed = b;
        gHot = b;
        SetCapture(hwnd);
        InvalidateCaption(hwnd);
        return 0;
    }
    case WM_NCLBUTTONUP:
        if (HitToButton(wp)) return 0;
        break;
    case WM_MOUSEMOVE: {
        POINT pt;
        int b;
        pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp);
        b = CapButtonAt(hwnd, pt);
        if (gPressed) {    /* 押したまま動かしている: 押したボタンの上にいる間だけ押下表示 */
            SetHot(hwnd, b == gPressed ? b : CB_NONE);
            return 0;
        }
        if (b != CB_PIN) b = CB_NONE;
        if (b && !gTrackC) TrackLeave(hwnd, 0);
        SetHot(hwnd, b);
        break;
    }
    case WM_MOUSELEAVE:
        gTrackC = 0;
        if (!gPressed && gHot == CB_PIN) SetHot(hwnd, CB_NONE);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt;
        pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp);
        if (CapButtonAt(hwnd, pt) == CB_PIN) {
            gPressed = CB_PIN;
            gHot = CB_PIN;
            SetCapture(hwnd);
            InvalidateCaption(hwnd);
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP:
        if (gPressed) {
            POINT pt;
            int b = gPressed, at;
            pt.x = (short)LOWORD(lp); pt.y = (short)HIWORD(lp);
            at = CapButtonAt(hwnd, pt);
            gPressed = CB_NONE;
            ReleaseCapture();
            gTrackNC = gTrackC = 0;
            gHot = (at == b) ? b : CB_NONE;
            if (gHot) TrackLeave(hwnd, gHot != CB_PIN);
            InvalidateCaption(hwnd);
            if (at == b) DoCapButton(hwnd, b);
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (gPressed) {
            gPressed = CB_NONE;
            SetHot(hwnd, CB_NONE);
            InvalidateCaption(hwnd);
        }
        break;
    case WM_SETCURSOR:
        if ((HWND)wp == hwnd && LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (CapButtonAt(hwnd, pt) == CB_PIN) {   /* ピンは v1.1 と同じく手の形 */
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
    case WM_NCRBUTTONDOWN:
        if (wp == HTCAPTION || wp == HTSYSMENU || HitToButton(wp)) return 0;
        break;
    case WM_NCRBUTTONUP:
        if (wp == HTCAPTION || wp == HTSYSMENU || HitToButton(wp)) {
            ShowSystemMenu(hwnd, (short)LOWORD(lp), (short)HIWORD(lp));
            return 0;
        }
        break;
    case WM_CONTEXTMENU:
        /* タイトル・アイコン・キャプションボタンの右クリック → システムメニュー */
        if ((HWND)wp == hwnd && lp != (LPARAM)-1) {
            LRESULT ht = SendMessageW(hwnd, WM_NCHITTEST, 0, lp);
            if (ht == HTCAPTION || ht == HTSYSMENU || HitToButton((WPARAM)ht)) {
                ShowSystemMenu(hwnd, (short)LOWORD(lp), (short)HIWORD(lp));
                return 0;
            }
        }
        break;
    case WM_SETFOCUS:
        /* 追加(v1.2): ウィンドウがアクティブになったら入力先をメモ本文へ */
        if (gEdit) SetFocus(gEdit);
        return 0;
    case WM_ERASEBKGND:
        return 1;          /* WM_PAINT で全部描く (ちらつき防止) */
    case WM_APP_HINT:
        gHintPosted = 0;
        UpdateScrollHint();
        return 0;
    case WM_MOUSEWHEEL:
        WheelScroll(wp);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc, band;
        int i;
        GetClientRect(hwnd, &rc);
        /* 変更(v1.2): 上端 gCapH はキャプション。その下は従来どおり 赤い枠 BORDER + 緑の面 */
        if (ps.rcPaint.top < gCapH) PaintCaption(hwnd, dc, rc.right);
        band = rc; band.top = gCapH; band.right = BORDER;
        FillRect(dc, &band, gBorder);
        band = rc; band.top = gCapH; band.left = rc.right - BORDER;
        FillRect(dc, &band, gBorder);
        band = rc; band.top = gCapH; band.bottom = gCapH + BORDER;
        FillRect(dc, &band, gBorder);
        band = rc; band.top = rc.bottom - BORDER;
        FillRect(dc, &band, gBorder);
        SetRect(&band, BORDER, gCapH + BORDER, rc.right - BORDER, rc.bottom - BORDER);
        FillRect(dc, &band, gBg);
        /* 追加(v1.2): スクロールの手がかり。緑の面に接する側ほど明るいグラデーション (HINT_PX 行) */
        for (i = 0; i < HINT_PX && (gHintUp || gHintDown); i++) {
            HBRUSH hb = CreateSolidBrush(Blend(COL_HINT, COL_TITLE, 255 * (HINT_PX - i) / HINT_PX));
            if (gHintUp) {
                SetRect(&band, BORDER, gCapH + BORDER - 1 - i, rc.right - BORDER, gCapH + BORDER - i);
                FillRect(dc, &band, hb);
            }
            if (gHintDown) {
                SetRect(&band, BORDER, rc.bottom - BORDER + i, rc.right - BORDER, rc.bottom - BORDER + i + 1);
                FillRect(dc, &band, hb);
            }
            DeleteObject(hb);
        }
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
            PostHint();
        } else if (LOWORD(wp) == ID_EDIT &&
                   (HIWORD(wp) == EN_VSCROLL || HIWORD(wp) == EN_UPDATE)) {
            PostHint();
        }
        return 0;
    case WM_NOTIFY:
        if (((NMHDR *)lp)->idFrom == ID_EDIT && ((NMHDR *)lp)->code == EN_SELCHANGE)
            PostHint();   /* 追加(v1.2): カーソル移動で自動スクロールしたとき */
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
        /* 変更(v1.2): 300 固定 → 色ボタンを間隔 0 まで詰めた幅 (と キャプションボタン+ピン) */
        int need = INK_COUNT * SWATCH_BOX + BORDER * 2 + SWATCH_MARGIN * 2;
        int cap = Sc(CAPBTN_W) * 4 + Sc(CAP_PAD);
        int nc = 16;
        RECT wr, cr;
        if (!IsIconic(hwnd) && GetWindowRect(hwnd, &wr) && GetClientRect(hwnd, &cr) && cr.right > 0) {
            nc = (wr.right - wr.left) - cr.right;
            if (nc < 0 || nc > 64) nc = 16;
        }
        m->ptMinTrackSize.x = (need > cap ? need : cap) + nc;
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
        break;
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
        if (gCapFont) DeleteObject(gCapFont);
        if (gGlyphFont) DeleteObject(gGlyphFont);
        if (gPinFont) DeleteObject(gPinFont);
        if (gIconSm) DestroyIcon(gIconSm);
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
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h, NULL, NULL, inst, NULL);
    /* 追加(v1.2): キャプションボタンのツールチップ (ピンは自前キャプション内のボタン) */
    CreateTips(gMain);
    SetWindowPos(gMain, NULL, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    RestorePlacement(gMain);
    ShowWindow(gMain, SW_SHOW);
    if (gTopmost) SetTopmost(1, 0);   /* 追加(v1.1) */
    PostHint();

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
