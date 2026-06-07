#include <windows.h>
#include <stdlib.h>
#include <string>
#include <tchar.h>
#include <wrl.h>
#include <dwmapi.h> 
#include <commctrl.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <uxtheme.h>
#include "WebView2.h"

#pragma comment(lib, "dwmapi.lib") 
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

using namespace Microsoft::WRL;
namespace fs = std::filesystem;

// --- UI Element IDs ---
#define IDC_ADDRESS_BAR 1001
#define IDC_GO_BUTTON 1002
#define IDC_BACK_BUTTON 1003
#define IDC_FORWARD_BUTTON 1004
#define IDC_STATUS_BAR 1005
#define IDC_TAB_CONTROL 1006
#define IDC_NEW_TAB_BUTTON 1007
#define IDC_CLOSE_TAB_BUTTON 1008
#define IDC_SETTINGS_BUTTON 1009 

// Settings Window Control IDs
#define IDC_CHK_DARKMODE 2001
#define IDC_CHK_EXTENSIONS 2002
#define IDC_BTN_SAVESETTINGS 2003
#define IDC_LIST_EXTENSIONS 2004
#define IDC_EDIT_HOMEPAGE 2005
#define IDC_LBL_HOMEPAGE 2006

#define EASTER_EGG_HOTKEY_ID 1

struct BrowserTab {
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    std::wstring currentUrl;
};

// --- Global Variables ---
HWND hWndMain, hWndEdit, hWndButton, hWndBack, hWndForward, hWndStatus, hWndTabs, hWndNewTab, hWndCloseTab, hWndSettings;
HWND hWndSettingsWindow = NULL;
WNDPROC OldEditProc;

ComPtr<ICoreWebView2Environment> g_env;
std::vector<BrowserTab> g_tabs;

// --- Global Persistent Settings ---
BOOL g_isDarkMode = TRUE;
BOOL g_extensionsEnabled = TRUE;
std::wstring g_homePageUrl = L"https://www.streetisreal.com/ubrowse-landing-page/";

// --- FLAPPY BIRD C++ INJECTION PAYLOAD ---
const wchar_t* FLAPPY_BIRD_HTML = LR"html(
<!DOCTYPE html>
<html>
<head>
<style>
    body { margin: 0; overflow: hidden; background: #70c5ce; font-family: Arial, sans-serif; user-select: none; }
    canvas { display: block; }
    #ui { position: absolute; top: 20px; left: 20px; color: white; font-size: 32px; font-weight: bold; text-shadow: 3px 3px 0 #000; z-index: 10; }
</style>
</head>
<body>
<div id="ui">Score: <span id="score">0</span></div>
<canvas id="gameCanvas"></canvas>
<script>
    const canvas = document.getElementById("gameCanvas");
    const ctx = canvas.getContext("2d");
    
    function resize() { canvas.width = window.innerWidth; canvas.height = window.innerHeight; }
    window.addEventListener("resize", resize);
    resize();

    let frames = 0, score = 0;
    let gameOver = false;

    const bird = { x: window.innerWidth * 0.2, y: 150, width: 34, height: 24, gravity: 0.35, jump: 6.5, velocity: 0 };
    let pipes = [];
    const pipeWidth = 60;
    const gap = 160;

    window.addEventListener("keydown", (e) => { if (e.code === "Space") flap(); });
    window.addEventListener("mousedown", flap);

    function flap() {
        if (gameOver) {
            bird.y = 150; bird.velocity = 0;
            pipes = []; score = 0; frames = 0;
            document.getElementById("score").innerText = score;
            gameOver = false; loop();
        } else {
            bird.velocity = -bird.jump;
        }
    }

    function draw() {
        ctx.fillStyle = "#70c5ce"; ctx.fillRect(0, 0, canvas.width, canvas.height);
        
        ctx.fillStyle = "#f2b736"; // Bird
        ctx.fillRect(bird.x, bird.y, bird.width, bird.height);

        ctx.fillStyle = "#73bf2e"; // Pipes
        for (let i = 0; i < pipes.length; i++) {
            let p = pipes[i];
            ctx.fillRect(p.x, 0, pipeWidth, p.top);
            ctx.fillRect(p.x, canvas.height - p.bottom, pipeWidth, p.bottom);
        }
        
        ctx.fillStyle = "#ded895"; // Ground
        ctx.fillRect(0, canvas.height - 30, canvas.width, 30);
    }

    function update() {
        bird.velocity += bird.gravity;
        bird.y += bird.velocity;

        if (bird.y + bird.height >= canvas.height - 30 || bird.y <= 0) gameOver = true;

        if (frames % 120 === 0) {
            let topHeight = Math.max(40, Math.random() * (canvas.height - gap - 80));
            let bottomHeight = canvas.height - gap - topHeight;
            pipes.push({ x: canvas.width, top: topHeight, bottom: bottomHeight, passed: false });
        }

        for (let i = 0; i < pipes.length; i++) {
            let p = pipes[i];
            p.x -= 3.5; // Speed

            // Collision
            if (bird.x < p.x + pipeWidth && bird.x + bird.width > p.x &&
                (bird.y < p.top || bird.y + bird.height > canvas.height - p.bottom)) {
                gameOver = true;
            }

            // Scoring
            if (p.x + pipeWidth < bird.x && !p.passed) {
                score++; p.passed = true;
                document.getElementById("score").innerText = score;
            }
        }
        pipes = pipes.filter(p => p.x + pipeWidth > 0);
        frames++;
    }

    function loop() {
        if (!gameOver) { update(); draw(); requestAnimationFrame(loop); } 
        else {
            ctx.fillStyle = "black"; ctx.font = "bold 40px Arial";
            ctx.fillText("Game Over - Press Space to Restart", canvas.width / 2 - 320, canvas.height / 2);
        }
    }
    loop();
</script>
</body>
</html>
)html";

// Forward Declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK SettingsWndProc(HWND, UINT, WPARAM, LPARAM);
void ResizeLayout(int width, int height);
void ApplyTheme(HWND hwnd, BOOL darkMode);

// --- Settings File Operations ---
void LoadSettings() {
    std::ifstream file("config.conf");
    if (file.is_open()) {
        file >> g_isDarkMode >> g_extensionsEnabled;
        std::string url;
        if (file >> url) {
            int size_needed = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), (int)url.size(), NULL, 0);
            g_homePageUrl = std::wstring(size_needed, 0);
            MultiByteToWideChar(CP_UTF8, 0, url.c_str(), (int)url.size(), &g_homePageUrl[0], size_needed);
        }
        file.close();
    }
}

void SaveSettings() {
    std::ofstream file("config.conf");
    if (file.is_open()) {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, g_homePageUrl.c_str(), (int)g_homePageUrl.size(), NULL, 0, NULL, NULL);
        std::string url(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, g_homePageUrl.c_str(), (int)g_homePageUrl.size(), &url[0], size_needed, NULL, NULL);

        file << g_isDarkMode << " " << g_extensionsEnabled << " " << url;
        file.close();
    }
}

std::wstring LoadExtensionScript(const fs::path& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) return L"";
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, contents.c_str(), (int)contents.size(), NULL, 0);
    std::wstring wcontents(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, contents.c_str(), (int)contents.size(), &wcontents[0], size_needed);
    return wcontents;
}

void CreateNewTab(std::wstring startUrl) {
    if (!g_env) return;

    int newTabIndex = (int)g_tabs.size();
    g_tabs.push_back(BrowserTab());
    g_tabs[newTabIndex].currentUrl = startUrl;

    TCITEM tie = { 0 };
    tie.mask = TCIF_TEXT;
    tie.pszText = (LPWSTR)L"New Tab";
    TabCtrl_InsertItem(hWndTabs, newTabIndex, &tie);
    TabCtrl_SetCurSel(hWndTabs, newTabIndex);

    g_env->CreateCoreWebView2Controller(hWndMain, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [newTabIndex, startUrl](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
            if (controller != nullptr) {
                int activeIndex = -1;
                for (int i = 0; i < g_tabs.size(); i++) {
                    if (!g_tabs[i].controller) {
                        activeIndex = i; break;
                    }
                }

                if (activeIndex != -1) {
                    g_tabs[activeIndex].controller = controller;
                    controller->get_CoreWebView2(&g_tabs[activeIndex].webview);
                    auto webview = g_tabs[activeIndex].webview;

                    // 1. Auto-Titling
                    EventRegistrationToken token;
                    webview->add_DocumentTitleChanged(
                        Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                            [](ICoreWebView2* sender, IUnknown* args) -> HRESULT {
                                int foundIndex = -1;
                                for (int i = 0; i < g_tabs.size(); i++) {
                                    if (g_tabs[i].webview.Get() == sender) {
                                        foundIndex = i; break;
                                    }
                                }
                                if (foundIndex != -1) {
                                    LPWSTR title;
                                    sender->get_DocumentTitle(&title);
                                    TCITEM tie = { 0 };
                                    tie.mask = TCIF_TEXT;
                                    tie.pszText = title;
                                    TabCtrl_SetItem(hWndTabs, foundIndex, &tie);
                                    CoTaskMemFree(title);
                                }
                                return S_OK;
                            }).Get(), &token);

                    // 2. NATIVE DOMAIN ADBLOCKER (With Extension Whitelist)
                    webview->AddWebResourceRequestedFilter(L"*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                    webview->add_WebResourceRequested(
                        Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                            [](ICoreWebView2* sender, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                                ComPtr<ICoreWebView2WebResourceRequest> request;
                                args->get_Request(&request);
                                LPWSTR uri;
                                request->get_Uri(&uri);
                                if (uri) {
                                    std::wstring url(uri);
                                    CoTaskMemFree(uri);

                                    // --- CRITICAL FIX: EXPLICITLY WHITELIST INTERNAL EXTENSION ASSETS ---
                                    if (url.rfind(L"chrome-extension://", 0) == 0) {
                                        return S_OK;
                                    }

                                    const wchar_t* blockedDomains[] = {
                                        L"doubleclick.net", L"googleadservices.com", L"googlesyndication.com",
                                        L"amazon-adsystem.com", L"taboola.com", L"outbrain.com"
                                    };
                                    for (const auto& domain : blockedDomains) {
                                        if (url.find(domain) != std::wstring::npos) {
                                            ComPtr<ICoreWebView2WebResourceResponse> response;
                                            g_env->CreateWebResourceResponse(nullptr, 403, L"Blocked", L"", &response);
                                            args->put_Response(response.Get());
                                            SendMessage(hWndStatus, SB_SETTEXT, 0, (LPARAM)_T("Domain Ad Blocked!"));
                                            break;
                                        }
                                    }
                                }
                                return S_OK;
                            }).Get(), nullptr);

                    // 3. YOUTUBE ADBLOCKER
                    LPCWSTR jsScript = L" \
                        const style = document.createElement('style'); \
                        style.innerHTML = ` \
                            ytd-ad-slot-renderer, \
                            ytd-companion-slot-renderer, \
                            ytd-player-legacy-desktop-watch-ads-renderer, \
                            ytd-promoted-sparkles-web-renderer, \
                            .ytd-video-masthead-ad-v3-renderer, \
                            .ytp-ad-image-overlay, \
                            div#root.style-scope.ytd-display-ad-renderer, \
                            div.ad-container { display: none !important; } \
                        `; \
                        document.head.appendChild(style); \
                        setInterval(() => { \
                            const skipBtn = document.querySelector('.ytp-ad-skip-button, .ytp-ad-skip-button-modern, .ytp-skip-ad-button'); \
                            if (skipBtn) { skipBtn.click(); } \
                            const isAdPlaying = document.querySelector('.ad-showing, .ytp-ad-player-overlay'); \
                            if (isAdPlaying) { \
                                const video = document.querySelector('video'); \
                                if (video && !isNaN(video.duration)) { \
                                    video.playbackRate = 16.0; \
                                    video.currentTime = video.duration - 0.1; \
                                } \
                            } \
                            const adOverlay = document.querySelector('.ytp-ad-overlay-close-button'); \
                            if (adOverlay) { adOverlay.click(); } \
                        }, 50); \
                    ";
                    webview->AddScriptToExecuteOnDocumentCreated(jsScript, nullptr);

                    // 4. PERSISTENT EXTENSION INJECTION (.js files)
                    if (g_extensionsEnabled) {
                        fs::path extDir = "extensions";
                        if (!fs::exists(extDir)) {
                            fs::create_directory(extDir);
                        }
                        else {
                            for (const auto& entry : fs::directory_iterator(extDir)) {
                                if (entry.path().extension() == ".js") {
                                    std::wstring customScript = LoadExtensionScript(entry.path());
                                    if (!customScript.empty()) {
                                        webview->AddScriptToExecuteOnDocumentCreated(customScript.c_str(), nullptr);
                                    }
                                }
                            }
                        }
                    }

                    for (int i = 0; i < g_tabs.size(); i++) {
                        if (g_tabs[i].controller) g_tabs[i].controller->put_IsVisible(i == activeIndex);
                    }

                    RECT bounds; GetClientRect(hWndMain, &bounds);
                    ResizeLayout(bounds.right, bounds.bottom);

                    // --- INJECT FLAPPY BIRD OR NAVIGATE URL ---
                    if (startUrl == L"ubrowse://flap") {
                        webview->NavigateToString(FLAPPY_BIRD_HTML);
                    }
                    else {
                        webview->Navigate(startUrl.c_str());
                    }
                }
            }
            return S_OK;
        }).Get());
}

LRESULT CALLBACK EditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        SendMessage(GetParent(hWnd), WM_COMMAND, MAKEWPARAM(IDC_GO_BUTTON, 0), 0);
        return 0;
    }
    return CallWindowProc(OldEditProc, hWnd, msg, wParam, lParam);
}

void ApplyTheme(HWND hwnd, BOOL darkMode) {
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    InvalidateRect(hwnd, NULL, TRUE);
}

int CALLBACK WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    fs::path ublockFullPath = fs::absolute("ublock");
    if (fs::exists(ublockFullPath)) {
        std::wstring argString = L"--load-extension=\"" + ublockFullPath.wstring() + L"\"";
        SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", argString.c_str());
    }

    LoadSettings();

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_BAR_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEX wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(g_isDarkMode ? GetStockObject(BLACK_BRUSH) : (HBRUSH)(COLOR_BTNFACE + 1));
    wcex.lpszClassName = _T("BrowserWindowClass");

    HICON hAppIcon = (HICON)LoadImage(NULL, _T("icon.ico"), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    wcex.hIcon = hAppIcon ? hAppIcon : LoadIcon(NULL, IDI_APPLICATION);
    wcex.hIconSm = wcex.hIcon;
    RegisterClassEx(&wcex);

    WNDCLASSEX wcexSettings = { 0 };
    wcexSettings.cbSize = sizeof(WNDCLASSEX);
    wcexSettings.lpfnWndProc = SettingsWndProc;
    wcexSettings.hInstance = hInstance;
    wcexSettings.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcexSettings.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcexSettings.lpszClassName = _T("SettingsWindowClass");
    RegisterClassEx(&wcexSettings);

    hWndMain = CreateWindow(_T("BrowserWindowClass"), _T("UBrowse Explorer 1.1"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800, NULL, NULL, hInstance, NULL);

    DwmSetWindowAttribute(hWndMain, DWMWA_USE_IMMERSIVE_DARK_MODE, &g_isDarkMode, sizeof(g_isDarkMode));
    RegisterHotKey(hWndMain, EASTER_EGG_HOTKEY_ID, MOD_CONTROL | MOD_SHIFT | MOD_ALT, VK_F5);

    hWndBack = CreateWindowEx(0, _T("BUTTON"), _T("<"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWndMain, (HMENU)IDC_BACK_BUTTON, hInstance, NULL);
    hWndForward = CreateWindowEx(0, _T("BUTTON"), _T(">"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWndMain, (HMENU)IDC_FORWARD_BUTTON, hInstance, NULL);
    hWndEdit = CreateWindowEx(0, _T("EDIT"), g_homePageUrl.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0, hWndMain, (HMENU)IDC_ADDRESS_BAR, hInstance, NULL);
    hWndButton = CreateWindowEx(0, _T("BUTTON"), _T("Go"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWndMain, (HMENU)IDC_GO_BUTTON, hInstance, NULL);

    hWndNewTab = CreateWindowEx(0, _T("BUTTON"), _T("+"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWndMain, (HMENU)IDC_NEW_TAB_BUTTON, hInstance, NULL);
    hWndCloseTab = CreateWindowEx(0, _T("BUTTON"), _T("X"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWndMain, (HMENU)IDC_CLOSE_TAB_BUTTON, hInstance, NULL);
    hWndSettings = CreateWindowEx(0, _T("BUTTON"), _T("Settings"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hWndMain, (HMENU)IDC_SETTINGS_BUTTON, hInstance, NULL);

    hWndTabs = CreateWindowEx(0, WC_TABCONTROL, _T(""), WS_CHILD | WS_VISIBLE | TCS_TABS | TCS_FIXEDWIDTH, 0, 0, 0, 0, hWndMain, (HMENU)IDC_TAB_CONTROL, hInstance, NULL);

    // Fix tab theme for dark mode
    SetWindowTheme(hWndTabs, L"Explorer", NULL);

    hWndStatus = CreateWindowEx(0, STATUSCLASSNAME, NULL, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hWndMain, (HMENU)IDC_STATUS_BAR, hInstance, NULL);
    SendMessage(hWndStatus, SB_SETTEXT, 0, (LPARAM)_T("Ready."));

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessage(hWndBack, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hWndForward, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hWndEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hWndButton, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hWndNewTab, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hWndCloseTab, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hWndSettings, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hWndTabs, WM_SETFONT, (WPARAM)hFont, TRUE);

    OldEditProc = (WNDPROC)SetWindowLongPtr(hWndEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);

    ShowWindow(hWndMain, nCmdShow);
    UpdateWindow(hWndMain);

    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (SUCCEEDED(result)) {
                    g_env = env;
                    CreateNewTab(g_homePageUrl);
                }
                return S_OK;
            }).Get());

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

void ResizeLayout(int width, int height) {
    if (!hWndEdit || !hWndTabs) return;

    int barHeight = 30;
    int navBtnWidth = 30;
    int goBtnWidth = 50;
    int settingsBtnWidth = 70;

    SendMessage(hWndStatus, WM_SIZE, 0, 0);
    RECT statusRect;
    GetClientRect(hWndStatus, &statusRect);
    int statusHeight = statusRect.bottom - statusRect.top;

    MoveWindow(hWndBack, 5, 5, navBtnWidth, barHeight, TRUE);
    MoveWindow(hWndForward, 5 + navBtnWidth + 5, 5, navBtnWidth, barHeight, TRUE);

    int addressBarX = 5 + navBtnWidth + 5 + navBtnWidth + 5;
    int addressBarWidth = width - addressBarX - goBtnWidth - navBtnWidth - navBtnWidth - settingsBtnWidth - 25;

    MoveWindow(hWndEdit, addressBarX, 5, addressBarWidth, barHeight, TRUE);

    int currentX = addressBarX + addressBarWidth + 5;
    MoveWindow(hWndButton, currentX, 5, goBtnWidth, barHeight, TRUE);

    currentX += goBtnWidth + 5;
    MoveWindow(hWndNewTab, currentX, 5, navBtnWidth, barHeight, TRUE);

    currentX += navBtnWidth + 5;
    MoveWindow(hWndCloseTab, currentX, 5, navBtnWidth, barHeight, TRUE);

    currentX += navBtnWidth + 5;
    MoveWindow(hWndSettings, currentX, 5, settingsBtnWidth, barHeight, TRUE);

    int tabY = 5 + barHeight + 5;
    int tabHeight = 25;
    MoveWindow(hWndTabs, 0, tabY, width, tabHeight, TRUE);

    int currentTab = TabCtrl_GetCurSel(hWndTabs);
    if (currentTab >= 0 && currentTab < g_tabs.size()) {
        if (g_tabs[currentTab].controller != nullptr) {
            RECT bounds;
            bounds.top = tabY + tabHeight;
            bounds.left = 0;
            bounds.right = width;
            bounds.bottom = height - statusHeight;
            g_tabs[currentTab].controller->put_Bounds(bounds);
        }
    }
}

LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    static HWND hChkDark, hChkExt, hListBox, hLabel, hEditHomePage, hLblHomePage;

    switch (message) {
    case WM_CREATE: {
        hChkDark = CreateWindowEx(0, _T("BUTTON"), _T("Enable Dark Mode"), WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            20, 20, 250, 30, hWnd, (HMENU)IDC_CHK_DARKMODE, NULL, NULL);
        hChkExt = CreateWindowEx(0, _T("BUTTON"), _T("Enable Extensions (.js folder)"), WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            20, 55, 250, 30, hWnd, (HMENU)IDC_CHK_EXTENSIONS, NULL, NULL);

        hLblHomePage = CreateWindowEx(0, _T("STATIC"), _T("Default Home/New Tab URL:"), WS_VISIBLE | WS_CHILD,
            20, 95, 250, 20, hWnd, (HMENU)IDC_LBL_HOMEPAGE, NULL, NULL);

        hEditHomePage = CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), g_homePageUrl.c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            20, 115, 290, 25, hWnd, (HMENU)IDC_EDIT_HOMEPAGE, NULL, NULL);

        hLabel = CreateWindowEx(0, _T("STATIC"), _T("Detected Active Extensions:"), WS_VISIBLE | WS_CHILD,
            20, 150, 250, 20, hWnd, NULL, NULL, NULL);

        hListBox = CreateWindowEx(WS_EX_CLIENTEDGE, _T("LISTBOX"), _T(""), WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
            20, 170, 290, 100, hWnd, (HMENU)IDC_LIST_EXTENSIONS, NULL, NULL);

        HWND hBtnSave = CreateWindowEx(0, _T("BUTTON"), _T("Save & Apply"), WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            105, 280, 120, 35, hWnd, (HMENU)IDC_BTN_SAVESETTINGS, NULL, NULL);

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessage(hChkDark, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hChkExt, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hLblHomePage, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hEditHomePage, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hListBox, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessage(hBtnSave, WM_SETFONT, (WPARAM)hFont, TRUE);

        SendMessage(hChkDark, BM_SETCHECK, g_isDarkMode ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessage(hChkExt, BM_SETCHECK, g_extensionsEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

        fs::path ublockDir = "ublock";
        if (fs::exists(ublockDir)) {
            SendMessageW(hListBox, LB_ADDSTRING, 0, (LPARAM)L"[Core System] uBlock Origin Active");
        }

        fs::path extDir = "extensions";
        if (fs::exists(extDir)) {
            for (const auto& entry : fs::directory_iterator(extDir)) {
                if (entry.path().extension() == ".js") {
                    std::wstring fileName = entry.path().filename().wstring();
                    SendMessageW(hListBox, LB_ADDSTRING, 0, (LPARAM)fileName.c_str());
                }
            }
        }

        if (SendMessage(hListBox, LB_GETCOUNT, 0, 0) == 0) {
            SendMessageW(hListBox, LB_ADDSTRING, 0, (LPARAM)L"(No extensions found)");
            EnableWindow(hListBox, FALSE);
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORBTN: {
        if (g_isDarkMode) {
            SetTextColor((HDC)wParam, RGB(255, 255, 255));
            SetBkColor((HDC)wParam, RGB(32, 32, 32));
            static HBRUSH hDarkBrush = CreateSolidBrush(RGB(32, 32, 32));
            return (INT_PTR)hDarkBrush;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_BTN_SAVESETTINGS) {
            g_isDarkMode = (SendMessage(hChkDark, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_extensionsEnabled = (SendMessage(hChkExt, BM_GETCHECK, 0, 0) == BST_CHECKED);

            // Retrieve URL from the textbox
            int length = GetWindowTextLength(hEditHomePage);
            if (length > 0) {
                std::vector<wchar_t> buffer(length + 1);
                GetWindowTextW(hEditHomePage, &buffer[0], length + 1);
                g_homePageUrl = std::wstring(&buffer[0]);
            }

            SaveSettings();
            ApplyTheme(hWndMain, g_isDarkMode);
            ApplyTheme(hWnd, g_isDarkMode);
            MessageBox(hWnd, _T("Settings saved successfully! Restart browser to apply extension toggles."), _T("Success"), MB_OK | MB_ICONINFORMATION);
            DestroyWindow(hWnd);
        }
        break;
    }
    case WM_DESTROY:
        hWndSettingsWindow = NULL;
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        ResizeLayout(LOWORD(lParam), HIWORD(lParam));
        break;

        // DARK MODE FIX
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN: {
        if (g_isDarkMode) {
            SetTextColor((HDC)wParam, RGB(255, 255, 255));
            SetBkColor((HDC)wParam, RGB(32, 32, 32));
            static HBRUSH hDarkBrush = CreateSolidBrush(RGB(32, 32, 32));
            return (INT_PTR)hDarkBrush;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    case WM_HOTKEY: {
        if (wParam == EASTER_EGG_HOTKEY_ID && GetForegroundWindow() == hWnd) {
            int currentTab = TabCtrl_GetCurSel(hWndTabs);
            if (currentTab >= 0 && currentTab < g_tabs.size() && g_tabs[currentTab].webview) {
                std::wstring easterEggUrl = L"https://www.streetisreal.com/easteregg/";
                SetWindowTextW(hWndEdit, easterEggUrl.c_str());
                g_tabs[currentTab].webview->Navigate(easterEggUrl.c_str());
                SendMessage(hWndStatus, SB_SETTEXT, 0, (LPARAM)_T("Easter Egg Activated!"));
            }
        }
        break;
    }

    case WM_NOTIFY: {
        if (((LPNMHDR)lParam)->code == TCN_SELCHANGE) {
            int currentTab = TabCtrl_GetCurSel(hWndTabs);
            for (int i = 0; i < g_tabs.size(); i++) {
                if (g_tabs[i].controller) g_tabs[i].controller->put_IsVisible(i == currentTab);
            }
            RECT rect; GetClientRect(hWnd, &rect);
            ResizeLayout(rect.right, rect.bottom);
        }
        break;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        int currentTab = TabCtrl_GetCurSel(hWndTabs);

        if (wmId == IDC_SETTINGS_BUTTON) {
            if (hWndSettingsWindow == NULL) {
                hWndSettingsWindow = CreateWindowEx(WS_EX_TOPMOST, _T("SettingsWindowClass"), _T("Browser Settings"),
                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 350, 380, hWnd, NULL, NULL, NULL);
                DwmSetWindowAttribute(hWndSettingsWindow, DWMWA_USE_IMMERSIVE_DARK_MODE, &g_isDarkMode, sizeof(g_isDarkMode));
                ShowWindow(hWndSettingsWindow, SW_SHOW);
            }
            else {
                SetForegroundWindow(hWndSettingsWindow);
            }
        }
        else if (wmId == IDC_NEW_TAB_BUTTON) {
            CreateNewTab(g_homePageUrl);
        }
        else if (wmId == IDC_CLOSE_TAB_BUTTON) {
            if (currentTab >= 0 && currentTab < g_tabs.size()) {
                if (g_tabs[currentTab].controller) g_tabs[currentTab].controller->Close();
                g_tabs.erase(g_tabs.begin() + currentTab);
                TabCtrl_DeleteItem(hWndTabs, currentTab);

                if (g_tabs.empty()) {
                    PostQuitMessage(0);
                }
                else {
                    int newSelection = (currentTab >= g_tabs.size()) ? (int)g_tabs.size() - 1 : currentTab;
                    TabCtrl_SetCurSel(hWndTabs, newSelection);
                    if (g_tabs[newSelection].controller) g_tabs[newSelection].controller->put_IsVisible(TRUE);
                    RECT rect; GetClientRect(hWnd, &rect);
                    ResizeLayout(rect.right, rect.bottom);
                }
            }
        }
        else if (currentTab >= 0 && currentTab < g_tabs.size() && g_tabs[currentTab].webview) {
            auto webview = g_tabs[currentTab].webview;
            if (wmId == IDC_GO_BUTTON) {
                int length = GetWindowTextLength(hWndEdit);
                if (length > 0) {
                    std::vector<wchar_t> buffer(length + 1);
                    GetWindowTextW(hWndEdit, &buffer[0], length + 1);
                    std::wstring url(&buffer[0]);

                    // --- INTERCEPT CUSTOM PROTOCOL HERE ---
                    if (url == L"ubrowse://flap") {
                        webview->NavigateToString(FLAPPY_BIRD_HTML);
                    }
                    else {
                        // Standard web navigation
                        if (url.find(L"://") == std::wstring::npos) {
                            url = L"https://" + url;
                            SetWindowTextW(hWndEdit, url.c_str());
                        }
                        webview->Navigate(url.c_str());
                    }
                }
            }
            else if (wmId == IDC_BACK_BUTTON) {
                BOOL cb; webview->get_CanGoBack(&cb); if (cb) webview->GoBack();
            }
            else if (wmId == IDC_FORWARD_BUTTON) {
                BOOL cf; webview->get_CanGoForward(&cf); if (cf) webview->GoForward();
            }
        }
        break;
    }

    case WM_DESTROY:
        UnregisterHotKey(hWnd, EASTER_EGG_HOTKEY_ID);
        if (hWndSettingsWindow) DestroyWindow(hWndSettingsWindow);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}