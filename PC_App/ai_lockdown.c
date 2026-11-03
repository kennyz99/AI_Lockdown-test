
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>

#define POLL_TIMER_ID 2
#define POLL_INTERVAL 30
#define QCOUNT 3
#define RX_BUFFER_SIZE 256

typedef struct {
    const wchar_t *q;
    const wchar_t *a[3];
    int correct;
} Question;

static const Question bank[7] = {
    {L"GPIO 的主要用途是什麼？",{L"A  儲存檔案",L"B  控制輸入與輸出",L"C  計算數學公式"},1},
    {L"RCC 在 STM32 中的主要功能是什麼？",{L"A  管理時鐘",L"B  顯示圖片",L"C  儲存程式"},0},
    {L"PLL 的主要功能是什麼？",{L"A  調整時鐘頻率",L"B  控制按鈕顏色",L"C  儲存資料"},0},
    {L"RTOS 是什麼？",{L"A  一種電阻",L"B  即時作業系統",L"C  一種 LED"},1},
    {L"SP 和 PC 的用途是什麼？",{L"A  堆疊指標與程式計數器",L"B  控制燈光",L"C  傳送聲音"},0},
    {L"要讓 LED 閃爍，通常需要什麼？",{L"A  永遠保持高電位",L"B  定期切換輸出狀態",L"C  移除電源"},1},
    {L"中斷優先權主要用來做什麼？",{L"A  改變螢幕大小",L"B  增加記憶體",L"C  決定中斷處理順序"},2}
};

static HWND win;
static HANDLE hSerial = INVALID_HANDLE_VALUE;

static int selected[QCOUNT];
static int qi = 0;
static int seconds = 30;
static int over = 0;
static int success = 0;
static int hover = -1;
static int connected = 0;
static int waitingAnswer = 0;

static int rxSeen = 0;
static DWORD lastRxTick = 0;

static wchar_t statusText[80] = L"WAITING FOR STM32";
static char rxBuffer[RX_BUFFER_SIZE];
static int rxLength = 0;

static void fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r = {x, y, x+w, y+h};
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void frame(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ op = SelectObject(dc, p);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));

    Rectangle(dc, x, y, x+w, y+h);

    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(p);
}

static void txt(HDC dc, int x, int y, int w, int h,
                const wchar_t *s, int size, COLORREF c,
                int bold, UINT flags)
{
    HFONT f = CreateFontW(
        -size, 0, 0, 0,
        bold ? FW_BOLD : FW_NORMAL,
        0, 0, 0, DEFAULT_CHARSET, 0, 0,
        CLEARTYPE_QUALITY, DEFAULT_PITCH,
        L"Microsoft JhengHei UI"
    );

    HGDIOBJ old = SelectObject(dc, f);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);

    RECT r = {x, y, x+w, y+h};

    DrawTextW(dc, s, -1, &r, flags);

    SelectObject(dc, old);
    DeleteObject(f);
}

static void chooseQuestions(void)
{
    int used[7] = {0};

    for (int i = 0; i < QCOUNT; i++) {
        int n;

        do {
            n = rand() % 7;
        } while (used[n]);

        used[n] = 1;
        selected[i] = n;
    }
}

static int SendLine(const char *line)
{
    DWORD written = 0;
    DWORD len;

    if (hSerial == INVALID_HANDLE_VALUE)
        return 0;

    len = (DWORD)lstrlenA(line);

    if (!WriteFile(hSerial, line, len, &written, NULL)
        || written != len) {

        connected = 0;
        rxSeen = 0;

        lstrcpyW(statusText, L"SERIAL WRITE ERROR");

        InvalidateRect(win, NULL, TRUE);
        return 0;
    }

    return 1;
}

static int OpenSerial(void)
{
    DCB dcb = {0};
    COMMTIMEOUTS timeouts = {0};

    hSerial = CreateFileW(
        L"\\\\.\\COM10",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hSerial == INVALID_HANDLE_VALUE) {
        connected = 0;
        rxSeen = 0;

        lstrcpyW(statusText, L"CONNECTION FAILED - CHECK COM10");
        return 0;
    }

    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(hSerial, &dcb)) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;

        connected = 0;
        rxSeen = 0;

        lstrcpyW(statusText, L"UART CONFIGURATION FAILED");
        return 0;
    }

    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

    if (!SetCommState(hSerial, &dcb)) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;

        connected = 0;
        rxSeen = 0;

        lstrcpyW(statusText, L"UART SETTINGS FAILED");
        return 0;
    }

    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    SetCommTimeouts(hSerial, &timeouts);
    SetupComm(hSerial, 256, 256);

    PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    connected = 1;
    rxSeen = 0;
    lastRxTick = 0;
    rxLength = 0;

    lstrcpyW(statusText, L"COM10 OPEN - WAITING FOR STM32");

    return 1;
}

static void CloseSerial(void)
{
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }

    connected = 0;
    rxSeen = 0;
}

static void SendCurrentQuestion(void)
{
    char line[32];
    int correct;

    if (!connected || over || qi >= QCOUNT)
        return;

    correct = bank[selected[qi]].correct + 1;

    wsprintfA(line, "Q,%d\n", correct);

    if (SendLine(line)) {
        waitingAnswer = 0;
        seconds = 30;

        lstrcpyW(statusText, L"QUESTION SENT - WAITING FOR STM32");
    }

    InvalidateRect(win, NULL, TRUE);
}

static void restartGame(void)
{
    qi = 0;
    seconds = 30;
    over = 0;
    success = 0;
    hover = -1;
    waitingAnswer = 0;

    chooseQuestions();

    if (connected) {
        lstrcpyW(statusText, L"NEW GAME - SENDING QUESTION");
        SendCurrentQuestion();
    }
    else {
        lstrcpyW(statusText, L"NOT CONNECTED - CHECK COM10");
    }

    InvalidateRect(win, NULL, TRUE);
}

static void sendKeyboardAnswer(int answerIndex)
{
    char line[16];

    if (!connected || over || waitingAnswer)
        return;

    if (qi >= QCOUNT)
        return;

    wsprintfA(line, "A,%d\n", answerIndex + 1);

    if (SendLine(line)) {
        waitingAnswer = 1;

        lstrcpyW(statusText, L"ANSWER SENT - WAITING FOR STM32");

        InvalidateRect(win, NULL, TRUE);
    }
}

static void HandleSerialLine(char *line)
{
    int value = -1;

    rxSeen = 1;
    lastRxTick = GetTickCount();

    if (sscanf(line, "T,%d", &value) == 1) {
        if (value >= 0 && value <= 30) {
            seconds = value;
            InvalidateRect(win, NULL, FALSE);
        }

        return;
    }

    if (strcmp(line, "R,CORRECT") == 0) {
        waitingAnswer = 0;
        qi++;

        if (qi >= QCOUNT) {
            over = 1;
            success = 1;

            lstrcpyW(statusText, L"ALL ANSWERS CORRECT");
        }
        else {
            lstrcpyW(statusText, L"CORRECT - NEXT QUESTION");
            SendCurrentQuestion();
        }

        InvalidateRect(win, NULL, TRUE);
        return;
    }

    if (strcmp(line, "R,WRONG") == 0
        || strcmp(line, "R,TIMEOUT") == 0) {

        waitingAnswer = 0;
        over = 1;
        success = 0;

        if (strcmp(line, "R,TIMEOUT") == 0)
            lstrcpyW(statusText, L"TIME EXPIRED - LOCKDOWN");
        else
            lstrcpyW(statusText, L"WRONG ANSWER - LOCKDOWN");

        InvalidateRect(win, NULL, TRUE);
        return;
    }

    if (strcmp(line, "S,WIN") == 0) {
        over = 1;
        success = 1;
        waitingAnswer = 0;

        lstrcpyW(statusText, L"MISSION COMPLETE");

        InvalidateRect(win, NULL, TRUE);
        return;
    }
}

static void PollSerial(void)
{
    char temp[64];
    DWORD bytesRead = 0;

    if (hSerial == INVALID_HANDLE_VALUE)
        return;

    if (!ReadFile(hSerial, temp, sizeof(temp), &bytesRead, NULL))
        return;

    if (bytesRead == 0)
        return;

    for (DWORD i = 0; i < bytesRead; i++) {
        char c = temp[i];

        if (c == '\r')
            continue;

        if (c == '\n') {
            rxBuffer[rxLength] = '\0';

            if (rxLength > 0)
                HandleSerialLine(rxBuffer);

            rxLength = 0;
        }
        else if (rxLength < RX_BUFFER_SIZE - 1) {
            rxBuffer[rxLength++] = c;
        }
        else {
            rxLength = 0;
        }
    }
}

static void paint(HDC dc, RECT r)
{
    int w = r.right;
    int h = r.bottom;

    COLORREF bg = (over && !success)
        ? RGB(35,4,12) : RGB(5,14,25);

    COLORREF cyan = RGB(0,225,255);
    COLORREF red = RGB(255,55,68);
    COLORREF green = RGB(50,255,160);
    COLORREF yellow = RGB(255,195,50);
    COLORREF muted = RGB(110,158,180);
    COLORREF white = RGB(225,242,249);

    COLORREF accent = (over && !success) ? red : cyan;

    fill(dc,0,0,w,h,bg);

    for (int x=0;x<w;x+=36)
        fill(dc,x,0,1,h,RGB(11,28,40));

    for (int y=0;y<h;y+=36)
        fill(dc,0,y,w,1,RGB(11,28,40));

    fill(dc,22,18,w-44,68,RGB(9,25,39));
    frame(dc,22,18,w-44,68,accent);

    txt(dc,38,24,w-76,28,
        L"RESEARCH LAB  /  CONTROL CENTER",
        20,cyan,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    txt(dc,40,53,w-80,20,
        L"FACILITY 07  //  AI CONTAINMENT PROTOCOL",
        11,muted,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    txt(dc,w-250,30,210,25,
        over?(success?L"STATUS: SECURE":L"STATUS: LOCKDOWN"):
        (connected?L"STATUS: ONLINE":L"STATUS: OFFLINE"),
        13,(over&&!success)?red:(connected?green:red),
        1,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

    fill(dc,22,98,w-44,36,RGB(12,29,44));
    frame(dc,22,98,w-44,36,RGB(30,73,91));

    txt(dc,35,103,165,25,L"POWER  98%",
        12,green,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    txt(dc,220,103,230,25,
        connected?L"AI LINK  CONNECTED":L"AI LINK  DISCONNECTED",
        12,connected?cyan:red,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    txt(dc,w-220,103,185,25,L"SECURITY  LEVEL 3",
        12,white,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    txt(dc,35,146,260,22,L"EMERGENCY COUNTDOWN",
        12,muted,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    wchar_t t[16];
    wsprintfW(t,L"%02d",seconds);

    txt(dc,w-145,139,100,42,t,31,
        (seconds<=10&&!over)?red:cyan,
        1,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

    fill(dc,35,183,w-70,7,RGB(26,45,58));

    int used=(w-70)*(30-seconds)/30;

    if (used<0) used=0;
    if (used>w-70) used=w-70;

    if (used>0)
        fill(dc,35,183,used,7,seconds<=10?red:cyan);

    fill(dc,22,207,w-44,215,RGB(9,24,38));
    frame(dc,22,207,w-44,215,accent);

    txt(dc,40,219,w-80,24,
        over?(success?L"MISSION COMPLETE":
        L"CRITICAL FAILURE  //  LOCKDOWN"):
        L"SECURITY CHALLENGE",
        15,accent,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    if (!over) {
        wchar_t count[40];

        wsprintfW(count,L"QUESTION  %d / %d",qi+1,QCOUNT);

        txt(dc,40,250,w-80,22,count,
            12,muted,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        txt(dc,40,278,w-80,50,
            bank[selected[qi]].q,
            19,white,1,DT_LEFT|DT_VCENTER|DT_WORDBREAK);

        for (int i=0;i<3;i++) {
            int y=337+i*43;

            fill(dc,40,y,w-80,35,
                hover==i?RGB(28,43,57):RGB(10,34,49));

            frame(dc,40,y,w-80,35,hover==i?red:cyan);

            txt(dc,58,y+2,w-116,31,
                bank[selected[qi]].a[i],
                16,white,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        }

        txt(dc,40,470,w-80,24,
            waitingAnswer?
            L"ANSWER SENT：等待 STM32 判斷中……":
            L"請選擇 A、B、C，答案由 STM32 判斷。",
            11,muted,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    }
    else {
        txt(dc,42,270,w-84,60,
            success?
            L"你成功解除 AI 封鎖，控制中心已恢復正常。":
            L"AI 已接管控制中心。系統進入緊急鎖定模式。",
            17,white,1,DT_LEFT|DT_VCENTER|DT_WORDBREAK);

        txt(dc,42,335,w-84,35,
            success?L"所有安全驗證均已通過。":
            L"請重新啟動任務，再次嘗試解除封鎖。",
            13,success?green:red,0,
            DT_LEFT|DT_VCENTER|DT_SINGLELINE);

        fill(dc,40,385,170,30,RGB(10,37,52));
        frame(dc,40,385,170,30,accent);

        txt(dc,45,387,160,26,L"[ R ] RESTART",
            12,white,1,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    /* ===== 右下角 STM32 連線狀態格 ===== */

    COLORREF linkColor;
    const wchar_t *linkText;

    if (!connected || hSerial == INVALID_HANDLE_VALUE) {
        linkColor = red;
        linkText = L"DISCONNECTED";
    }
    else if (!rxSeen) {
        linkColor = yellow;
        linkText = L"WAITING";
    }
    else if (GetTickCount() - lastRxTick > 5000) {
        linkColor = yellow;
        linkText = L"NO RECENT DATA";
    }
    else {
        linkColor = green;
        linkText = L"CONNECTED";
    }

    int boxW = 166;
    int boxH = 34;
    int boxX = w - boxW - 20;
    int boxY = h - boxH - 43;

    fill(dc,boxX,boxY,boxW,boxH,RGB(8,25,39));
    frame(dc,boxX,boxY,boxW,boxH,linkColor);

    fill(dc,boxX+9,boxY+12,9,9,linkColor);

    txt(dc,boxX+25,boxY+3,boxW-32,15,
        L"STM32  UART",
        10,muted,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    txt(dc,boxX+25,boxY+17,boxW-32,14,
        linkText,
        10,linkColor,1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    txt(dc,24,h-27,w-220,20,statusText,
        10,muted,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
}

static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp)
{
    switch(msg) {

    case WM_CREATE:
        win=hwnd;

        srand((unsigned)time(NULL));
        chooseQuestions();

        if (OpenSerial()) {
            SetTimer(hwnd,POLL_TIMER_ID,POLL_INTERVAL,NULL);
            restartGame();
        }
        else {
            MessageBoxW(hwnd,
                L"無法開啟 COM10。請確認 STM32 已接上 USB，且沒有其他程式占用 COM10。",
                L"STM32 連線失敗",
                MB_OK|MB_ICONERROR);
        }

        return 0;

    case WM_TIMER:
        if (wp==POLL_TIMER_ID) {
            PollSerial();
            InvalidateRect(hwnd,NULL,FALSE);
        }

        return 0;

    case WM_LBUTTONDOWN: {
        int y=HIWORD(lp);

        if (over) {
            if (LOWORD(lp)>=35 && LOWORD(lp)<=220
                && y>=375 && y<=425)
                restartGame();
        }
        else if (y>=337 && y<372)
            sendKeyboardAnswer(0);
        else if (y>=380 && y<415)
            sendKeyboardAnswer(1);
        else if (y>=423 && y<458)
            sendKeyboardAnswer(2);

        return 0;
    }

    case WM_KEYDOWN:
        if (wp=='R' && over)
            restartGame();
        else if (wp=='1' || wp=='A')
            sendKeyboardAnswer(0);
        else if (wp=='2' || wp=='B')
            sendKeyboardAnswer(1);
        else if (wp=='3' || wp=='C')
            sendKeyboardAnswer(2);

        return 0;

    case WM_MOUSEMOVE: {
        int y=HIWORD(lp);
        int n=-1;

        if (!over) {
            if (y>=337 && y<372) n=0;
            else if (y>=380 && y<415) n=1;
            else if (y>=423 && y<458) n=2;
        }

        if (n!=hover) {
            hover=n;
            InvalidateRect(hwnd,NULL,FALSE);
        }

        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc=BeginPaint(hwnd,&ps);
        RECT r;

        GetClientRect(hwnd,&r);
        paint(dc,r);

        EndPaint(hwnd,&ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd,POLL_TIMER_ID);
        CloseSerial();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI WinMain(HINSTANCE inst,HINSTANCE prev,LPSTR cmd,int show)
{
    (void)prev;
    (void)cmd;

    WNDCLASSW wc={0};

    wc.lpfnWndProc=proc;
    wc.hInstance=inst;
    wc.lpszClassName=L"AILockdownWindow";
    wc.hCursor=LoadCursorW(NULL,IDC_ARROW);
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);

    if (!RegisterClassW(&wc))
        return 1;

    HWND hwnd=CreateWindowExW(
        0,wc.lpszClassName,L"AI 叛變 - 控制中心",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT,CW_USEDEFAULT,720,570,
        NULL,NULL,inst,NULL
    );

    if (!hwnd)
        return 1;

    win=hwnd;

    ShowWindow(hwnd,show);
    UpdateWindow(hwnd);

    MSG msg;

    while (GetMessageW(&msg,NULL,0,0)>0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
