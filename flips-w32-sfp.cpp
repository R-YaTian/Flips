// Module name: Floating IPS, Windows sfx module
// Author: R-YaTian
// Licence: GPL v3 or higher

#include "flips.h"

#ifdef FLIPS_WINDOWS_SFP
#include <locale>
#include <string>
#include <windows.h>

// Get Desktop DPI (Windows 7+)
int GetDesktopDpi() {
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return dpi;
}

double GetDpiScaleFactor() {
    int dpi = GetDesktopDpi();
    return dpi / 96.0;  // 96 DPI = 1.0
}

void CenterWindow(HWND hwnd) {
    RECT rect;
    GetWindowRect(hwnd, &rect);  // Get current window size

    int windowWidth = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;

    // Get screen size
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Calc new window position
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;

    SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

double scaleFactor = 1.0;

class file_w32 : public file {
    size_t size;
    size_t offset;
    HANDLE io;

public:
    static file* create(LPCWSTR filename)
    {
        HANDLE io = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (io==INVALID_HANDLE_VALUE) return NULL;
        return new file_w32(io, (size_t)0);
    }

private:
    file_w32(HANDLE io, uint32_t sizetsize) : io(io)
    {
        size = GetFileSize(io, NULL);
        offset = 0;
    }

    file_w32(HANDLE io, uint64_t sizetsize) : io(io)
    {
        GetFileSizeEx(io, (PLARGE_INTEGER)&size);
        offset = 0;
    }

public:
    size_t len() { return size; }

    bool read(uint8_t* target, size_t start, size_t len)
    {
        OVERLAPPED ov = {0};
        ov.Offset = start + offset;
        ov.OffsetHigh = ov.Offset >> 16 >> 16;
        DWORD actuallen;
        return (ReadFile(io, target, len, &actuallen, &ov) && len==actuallen);
    }

    void findPatchOffset()
    {
        uint8_t buffer[4] = {0};
        read(buffer, size - 4, 4);
        uint32_t addr;
        addr = buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | (buffer[3] << 24);
        offset = addr;
        size = size - addr - 4;
    }

    ~file_w32() { CloseHandle(io); }
};

file* file::create(LPCWSTR filename) { return file_w32::create(filename); }
bool file::exists(LPCWSTR filename) { return GetFileAttributes(filename) != INVALID_FILE_ATTRIBUTES; }

class filewrite_w32 : public filewrite {
    HANDLE io;

public:
    static filewrite* create(LPCWSTR filename)
    {
        HANDLE io = CreateFile(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (!io) return NULL;
        return new filewrite_w32(io);
    }

private:
    filewrite_w32(HANDLE io) : io(io) {}

public:
    bool append(const uint8_t* data, size_t len)
    {
        DWORD truelen;
        return (WriteFile(io, data, len, &truelen, NULL) && truelen==len);
    }

    ~filewrite_w32() { CloseHandle(io); }
};

filewrite* filewrite::create(LPCWSTR filename) { return filewrite_w32::create(filename); }
filemap* filemap::create(LPCWSTR filename) { return filemap::create_fallback(filename); }

HWND hwndMain=NULL;

void bpsdeltaBegin() { return; }
bool bpsdeltaProgress(void* userdata, size_t done, size_t total) { return false; }
void bpsdeltaEnd() { return; }

bool SelectRom(LPWSTR filename, LPCWSTR title, bool output)
{
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize=sizeof(ofn);
    ofn.hwndOwner=hwndMain;
    ofn.lpstrFilter=TEXT("选择常见 ROM 文件\0*.smc;*.sfc;*.nes;*.gb;*.gbc;*.gba;*.nds;*.vb;*.sms;*.smd;*.md;*.ngp;*.n64;*.z64;*.bin\0所有文件 (*.*)\0*.*\0");
    ofn.lpstrFile=filename;
    ofn.nMaxFile=MAX_PATH;
    ofn.nFilterIndex=0;
    ofn.lpstrTitle=title;
    ofn.Flags=OFN_PATHMUSTEXIST|(output?OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    ofn.lpstrDefExt=TEXT("smc");
    if (!output && !GetOpenFileName(&ofn)) return false;
    if ( output && !GetSaveFileName(&ofn)) return false;
    return true;
}

UINT mboxtype[] = { MB_OK, MB_OK, MB_OK|MB_ICONWARNING, MB_OK|MB_ICONWARNING, MB_OK|MB_ICONERROR, MB_OK|MB_ICONERROR };

LPCWSTR patchextensions[] = {
    NULL,//unused, ty_null
    TEXT("bps"),
    TEXT("ips"),
};

int a_ApplyPatch(LPCWSTR inromname)
{
    WCHAR selfPath[MAX_PATH];
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);

    file* patch = file::create(selfPath);
    file_w32* fPtr = static_cast<file_w32*>(patch);
    fPtr->findPatchOffset();

    if (patch)
    {
        WCHAR inromname_buf[MAX_PATH];
        if (!inromname)
        {
            inromname = inromname_buf;
            inromname_buf[0] = '\0';
            if (!SelectRom(inromname_buf, TEXT("选择要打补丁的文件"), false))
            {
                delete patch;
                return 0;
            }
        }

        WCHAR outromname[MAX_PATH];
        wcscpy(outromname, inromname);
        LPWSTR outrompath = GetBaseName(outromname);
        LPWSTR patchpath = GetBaseName(selfPath);
        if (outrompath && patchpath)
        {
            wcscpy(outrompath, patchpath);
            LPWSTR outromext = GetExtension(outrompath);
            LPWSTR inromext = GetExtension(inromname);
            if (*inromext && *outromext) wcscpy(outromext, inromext);
        }
        if (!SelectRom(outromname, TEXT("选择输出文件"), true))
        {
            delete patch;
            return 0;
        }
        struct errorinfo errinf = ApplyPatchMem(patch, inromname, true, outromname, NULL, false);
        delete patch;
        MessageBoxA(hwndMain, errinf.description, "Flips-sfp", mboxtype[errinf.level]);
        return errinf.level;
    }

    delete patch;
    return 0;
}

void a_ShowAbout()
{
    WCHAR selfPath[MAX_PATH];
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);
    LPWSTR fname = GetBaseName(selfPath);
    size_t len = wcslen(fname);
    if (wcslen(fname) > 4) fname[len - 4] = L'\0';

    MessageBoxW(hwndMain,
                L"Flips-sfp 模块是自释放式的补丁程序\n"
                "补丁数据已内置于该可执行文件内部\n"
                "该自释放补丁程序由 Flips 简体加强版生成\n"
                "程序作者: R-YaTian",
                fname,
                MB_ICONINFORMATION);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_COMMAND:
        {
            if (wParam==1) a_ApplyPatch(NULL);
            if (wParam==2) a_ShowAbout();
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;
    case WM_DESTROY:
        {
            if (hwnd==hwndMain) PostQuitMessage(0);
            break;
        }
    default:
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

static HFONT try_create_font(const char * name, int size)
{
    return CreateFontA(-size*96/72 * scaleFactor, 0, 0, 0, FW_NORMAL,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH|FF_DONTCARE,
                       name);
}

int ShowMainWindow(HINSTANCE hInstance, int nCmdShow)
{
    scaleFactor = GetDpiScaleFactor();
    WNDCLASSA wc;
    wc.style=0;
    wc.lpfnWndProc=WindowProc;
    wc.cbClsExtra=0;
    wc.cbWndExtra=0;
    wc.hInstance=GetModuleHandle(NULL);
    wc.hIcon=LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(0));
    wc.hCursor=LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground=GetSysColorBrush(COLOR_3DFACE);
    wc.lpszMenuName=NULL;
    wc.lpszClassName="flips";
    RegisterClassA(&wc);

    MSG msg;
    hwndMain = CreateWindowA(
               "flips", "Flips-sfp",
               WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_BORDER|WS_MINIMIZEBOX,
               CW_USEDEFAULT, CW_USEDEFAULT, 240 * scaleFactor, 63 * scaleFactor, NULL, NULL,
               GetModuleHandle(NULL), NULL);
    CenterWindow(hwndMain);

    HFONT hfont=try_create_font("Segoe UI", 9);
    if (!hfont) hfont=try_create_font("MS Shell Dlg 2", 8);
    if (!hfont) hfont=(HFONT)GetStockObject(DEFAULT_GUI_FONT);

    int buttonid=0;
    HWND lastbutton;
#define button(x,y,w,h, text) \
        do { \
            lastbutton=CreateWindow(L"BUTTON", text, WS_CHILD|WS_TABSTOP|WS_VISIBLE|(buttonid==0?(BS_DEFPUSHBUTTON|WS_GROUP):(BS_PUSHBUTTON)), \
                                            x, y, w, h, hwndMain, (HMENU)(uintptr_t)(buttonid+1), GetModuleHandle(NULL), NULL); \
            SendMessage(lastbutton, WM_SETFONT, (WPARAM)hfont, 0); \
            buttonid++; \
        } while(0)
    button(24 * scaleFactor,  6 * scaleFactor,  90 * scaleFactor, 23 * scaleFactor, L"应用补丁"); SetActiveWindow(lastbutton);
    button(122 * scaleFactor, 6 * scaleFactor,  90 * scaleFactor, 23 * scaleFactor, L"关于");

    ShowWindow(hwndMain, nCmdShow);

    while (GetMessageA(&msg, NULL, 0, 0)>0)
    {
        if (!IsDialogMessageA(hwndMain, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    return msg.wParam;
}

void GUIClaimConsole()
{
    //this one makes it act like a console app in all cases except it doesn't create a new console if
    // not launched from one (it'd swiftly go away on app exit anyways), and it doesn't like being
    // launched from cmd since cmd wants to run a new command if spawning a gui app (I can't make it
    // not be a gui app because that flashes a console; it acts sanely from batch files)

    bool claimstdin=(GetFileType(GetStdHandle(STD_INPUT_HANDLE))==FILE_TYPE_UNKNOWN);
    bool claimstdout=(GetFileType(GetStdHandle(STD_OUTPUT_HANDLE))==FILE_TYPE_UNKNOWN);
    bool claimstderr=(GetFileType(GetStdHandle(STD_ERROR_HANDLE))==FILE_TYPE_UNKNOWN);

    if (claimstdin || claimstdout || claimstderr) AttachConsole(ATTACH_PARENT_PROCESS);

    if (claimstdin) freopen("CONIN$", "rt", stdin);
    if (claimstdout) freopen("CONOUT$", "wt", stdout);
    if (claimstderr) freopen("CONOUT$", "wt", stderr);

    if (claimstdout) fputc('\r', stdout);
    if (claimstderr) fputc('\r', stderr);
}

HINSTANCE hInstance_;
int nCmdShow_;

int GUIShow(LPCWSTR filename)
{
    INITCOMMONCONTROLSEX initctrls;
    initctrls.dwSize = sizeof(initctrls);
    initctrls.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&initctrls);

    int ret;
    if (filename)
        ret = a_ApplyPatch(filename);
    else
        ret = ShowMainWindow(hInstance_, nCmdShow_);

    return ret;
}

int CALLBACK WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    hInstance_=hInstance;
    nCmdShow_=nCmdShow;
    int argc;
    wchar_t ** argv=CommandLineToArgvW(GetCommandLineW(), &argc);
    return flipsmain(argc, argv);
}

#endif
