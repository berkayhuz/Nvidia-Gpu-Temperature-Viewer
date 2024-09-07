#include <windows.h>
#include <shellapi.h>
#include <nvapi.h>
#include <tchar.h>
#include <iostream>
#include <shlobj.h>

HINSTANCE hInst;
NOTIFYICONDATA nid;
NvPhysicalGpuHandle gpuHandles[NVAPI_MAX_PHYSICAL_GPUS];
NvU32 gpuCount = 0;
int currentTemperature = 0;
char gpuName[64] = { 0 };

HANDLE hMutex;

bool IsInStartup()
{
    HKEY hKey;
    WCHAR szPath[MAX_PATH];
    GetModuleFileName(NULL, szPath, MAX_PATH);

    LONG lRes = RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &hKey);
    if (lRes == ERROR_SUCCESS)
    {
        WCHAR szValue[MAX_PATH];
        DWORD dwSize = sizeof(szValue);
        lRes = RegQueryValueEx(hKey, L"GpuTemperature", NULL, NULL, (LPBYTE)szValue, &dwSize);
        RegCloseKey(hKey);

        if (lRes == ERROR_SUCCESS && wcscmp(szPath, szValue) == 0)
        {
            return true;
        }
    }
    return false;
}

void AddToStartup()
{
    if (IsInStartup())
        return;

    HKEY hKey;
    LONG lRes = RegCreateKeyEx(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL);
    if (lRes == ERROR_SUCCESS)
    {
        WCHAR szPath[MAX_PATH];
        GetModuleFileName(NULL, szPath, MAX_PATH);

        RegSetValueEx(hKey, L"GpuTemperature", 0, REG_SZ, (const BYTE*)szPath, (DWORD)((wcslen(szPath) + 1) * sizeof(WCHAR)));

        RegCloseKey(hKey);
    }
}

bool UpdateGpuInfo()
{
    NvAPI_Status status = NvAPI_Initialize();
    if (status != NVAPI_OK)
        return false;

    status = NvAPI_EnumPhysicalGPUs(gpuHandles, &gpuCount);
    if (status != NVAPI_OK || gpuCount == 0)
        return false;

    NV_GPU_THERMAL_SETTINGS thermalSettings;
    ZeroMemory(&thermalSettings, sizeof(thermalSettings));
    thermalSettings.version = NV_GPU_THERMAL_SETTINGS_VER;
    status = NvAPI_GPU_GetThermalSettings(gpuHandles[0], 0, &thermalSettings);

    if (status == NVAPI_OK)
    {
        if (thermalSettings.count > 0)
        {
            currentTemperature = thermalSettings.sensor[0].currentTemp;
        }
        else
        {
            return false;
        }
    }

    status = NvAPI_GPU_GetFullName(gpuHandles[0], gpuName);
    if (status != NVAPI_OK)
        return false;

    return true;
}

void UpdateTrayIcon()
{
    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcScreen, 32, 32);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));

    RECT rect = { 0, 0, 32, 32 };
    FillRect(hdcMem, &rect, hBrush);

    DeleteObject(hBrush);

    HFONT hFont = CreateFont(21,
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, VARIABLE_PITCH, TEXT("Arial"));
    HFONT hFontOld = (HFONT)SelectObject(hdcMem, hFont);

    TCHAR tempText[32];
    swprintf_s(tempText, sizeof(tempText) / sizeof(TCHAR), _T("%d°C"), currentTemperature);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    SetBkMode(hdcMem, TRANSPARENT);
    TextOut(hdcMem, 5, 5, tempText, lstrlen(tempText));

    ICONINFO iconInfo;
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = hbmMem;
    iconInfo.hbmColor = hbmMem;
    HICON hIcon = CreateIconIndirect(&iconInfo);

    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = hIcon;
    TCHAR tooltip[256];
    swprintf_s(tooltip, sizeof(tooltip) / sizeof(TCHAR), _T("GPU: %S\nTemp: %d°C"), gpuName, currentTemperature);
    lstrcpy(nid.szTip, tooltip);
    Shell_NotifyIcon(NIM_MODIFY, &nid);

    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    DeleteObject(hFont);
    DestroyIcon(hIcon);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_USER + 1:
        if (lParam == WM_RBUTTONUP)
        {
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, 2, L"Help");
            AppendMenu(hMenu, MF_STRING, 1, L"Logout");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == 1)
        {
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
        }
        else if (LOWORD(wParam) == 2)
        {
            ShellExecute(NULL, L"open", L"https://www.github.com/berkayhuz", NULL, NULL, SW_SHOWNORMAL);
        }
        break;
    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void ShowTrayIcon(HINSTANCE hInstance)
{
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TrayIconClass";
    RegisterClass(&wc);

    HWND hwnd = CreateWindow(wc.lpszClassName, L"TrayIconWindow", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpy(nid.szTip, L"This is a system tray icon");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    hMutex = CreateMutex(NULL, TRUE, L"GpuTemperatureAppMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        return 0;
    }

    AddToStartup();

    ShowTrayIcon(hInstance);

    if (!UpdateGpuInfo())
    {
        MessageBox(NULL, L"Failed to initialize NVAPI or obtain GPU information.", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    UpdateTrayIcon();

    SetTimer(NULL, 1, 1000, [](HWND hwnd, UINT msg, UINT_PTR idEvent, DWORD dwTime)
        {
            if (UpdateGpuInfo())
            {
                UpdateTrayIcon();
            }
        });

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    NvAPI_Unload();
    CloseHandle(hMutex);
    return (int)msg.wParam;
}
