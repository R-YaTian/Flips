// Module name: Floating IPS, Windows sfx module
// Author: R-YaTian
// Licence: GPL v3 or higher

#include "flips.h"

#ifdef FLIPS_WINDOWS_SFX
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

std::wstring ConvertUtf8ToWstring(const std::string& utf8Str) {
    std::wstring wstr(utf8Str.begin(), utf8Str.end());
    return wstr;
}

double scaleFactor = 1.0;

class file_w32 : public file {
    size_t size;
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
    }

    file_w32(HANDLE io, uint64_t sizetsize) : io(io)
    {
        GetFileSizeEx(io, (PLARGE_INTEGER)&size);
    }

public:
    size_t len() { return size; }

    bool read(uint8_t* target, size_t start, size_t len)
    {
        OVERLAPPED ov = {0};
        ov.Offset = start;
        ov.OffsetHigh = start>>16>>16;
        DWORD actuallen;
        return (ReadFile(io, target, len, &actuallen, &ov) && len==actuallen);
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

//TODO: implement properly
//also ensure input==output works if implementing this, rather than getting a file sharing violation
//applies even when selecting multiple patches, of which one overwrites input
filemap* filemap::create(LPCWSTR filename) { return filemap::create_fallback(filename); }

HWND hwndMain=NULL;

struct {
    unsigned char lastRomType;
    bool enableAutoRomSelector;
} static state;

HWND hwndProgress;
bool bpsdCancel;
LRESULT CALLBACK bpsdProgressWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void bpsdeltaBegin()
{
    bpsdCancel=false;
    RECT mainwndpos;
    GetWindowRect(hwndMain, &mainwndpos);
    hwndProgress=CreateWindowA(
                        "flips", flipsversion,
                        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_BORDER,
                        mainwndpos.left + 53 * scaleFactor, mainwndpos.top + 27 * scaleFactor,
                        101 * scaleFactor, 39 * scaleFactor, hwndMain, NULL, GetModuleHandle(NULL), NULL);
    SetWindowLongPtrA(hwndProgress, GWLP_WNDPROC, (LONG_PTR)bpsdProgressWndProc);

    ShowWindow(hwndProgress, SW_SHOW);
    EnableWindow(hwndMain, FALSE);

    bpsdeltaProgress(NULL, 0, 1);
}

bool bpsdeltaProgress(void* userdata, size_t done, size_t total)
{
    if (!bpsdeltaGetProgress(done, total)) return !bpsdCancel;
    if (hwndProgress) InvalidateRect(hwndProgress, NULL, false);
    MSG Msg;
    while (PeekMessage(&Msg, NULL, 0, 0, PM_REMOVE)) DispatchMessage(&Msg);
    return !bpsdCancel;
}

LRESULT CALLBACK bpsdProgressWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_ERASEBKGND: return TRUE;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            RECT rc;
            BeginPaint(hwnd, &ps);
            GetClientRect(hwnd, &rc);
            FillRect(ps.hdc, &rc, GetSysColorBrush(COLOR_3DFACE));
            SetBkColor(ps.hdc, GetSysColor(COLOR_3DFACE));
            SelectObject(ps.hdc, (HFONT)GetStockObject(DEFAULT_GUI_FONT));
            DrawTextA(ps.hdc, bpsdProgStr, -1, &rc, DT_CENTER | DT_NOCLIP);
            EndPaint(hwnd, &ps);
        }
        break;
    case WM_CLOSE:
        bpsdCancel=true;
        break;
    default:
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void bpsdeltaEnd()
{
    EnableWindow(hwndMain, TRUE);
    DestroyWindow(hwndProgress);
    hwndProgress=NULL;
}

bool SelectRom(LPWSTR filename, LPCWSTR title, bool output)
{
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize=sizeof(ofn);
    ofn.hwndOwner=hwndMain;
    ofn.lpstrFilter=TEXT("选择常见 ROM 文件\0*.smc;*.sfc;*.nes;*.gb;*.gbc;*.gba;*.nds;*.vb;*.sms;*.smd;*.md;*.ngp;*.n64;*.z64\0所有文件 (*.*)\0*.*\0");
    ofn.lpstrFile=filename;
    ofn.nMaxFile=MAX_PATH;
    ofn.nFilterIndex=state.lastRomType;
    ofn.lpstrTitle=title;
    ofn.Flags=OFN_PATHMUSTEXIST|(output?OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    ofn.lpstrDefExt=TEXT("smc");
    if (!output && !GetOpenFileName(&ofn)) return false;
    if ( output && !GetSaveFileName(&ofn)) return false;
    state.lastRomType=ofn.nFilterIndex;
    return true;
}

UINT mboxtype[]={ MB_OK, MB_OK, MB_OK|MB_ICONWARNING, MB_OK|MB_ICONWARNING, MB_OK|MB_ICONERROR, MB_OK|MB_ICONERROR };

LPCWSTR patchextensions[]={
    NULL,//unused, ty_null
    TEXT("bps"),
    TEXT("ips"),
};

int a_ApplyPatch(LPCWSTR clipatchname)
{
    WCHAR patchnames[65536];
    patchnames[0]='\0';
    bool multiplePatches;
    if (clipatchname)
    {
        multiplePatches=false;
        wcscpy(patchnames, clipatchname);
    }
    else
    {
    }

    //get rom name and apply

    if (!multiplePatches)
    {
        file* patch = file::create(patchnames);
        if (patch)
        {

        WCHAR inromname_buf[MAX_PATH];
        LPCWSTR inromname=NULL;
        if (state.enableAutoRomSelector) inromname=FindRomForPatch(patch, NULL);
        if (!inromname)
        {
            inromname=inromname_buf;
            inromname_buf[0]='\0';
            if (!SelectRom(inromname_buf, TEXT("选择要打补丁的文件"), false)) goto cancel;
        }
        WCHAR outromname[MAX_PATH];
        wcscpy(outromname, inromname);
        LPWSTR outrompath=GetBaseName(outromname);
        LPWSTR patchpath=GetBaseName(patchnames);
        if (outrompath && patchpath)
        {
            wcscpy(outrompath, patchpath);
            LPWSTR outromext=GetExtension(outrompath);
            LPWSTR inromext=GetExtension(inromname);
            if (*inromext && *outromext) wcscpy(outromext, inromext);
        }
        if (!SelectRom(outromname, TEXT("选择输出文件"), true)) goto cancel;
        struct errorinfo errinf=ApplyPatchMem(patch, inromname, true, outromname, NULL, state.enableAutoRomSelector);
        delete patch;
        MessageBoxA(hwndMain, errinf.description, flipsversion, mboxtype[errinf.level]);
        return errinf.level;
        }
    cancel:
        delete patch;
        return 0;
    }
    else
    {
#define max(a, b) (a > b ? a : b)
        if (state.enableAutoRomSelector)
        {
            LPCWSTR foundRom=NULL;
            bool canUseFoundRom=true;
            bool usingFoundRom=false;

        redo: ;
            WCHAR thisFileNameWithPath[MAX_PATH];
            bool anySuccess=false;
            enum { e_none, e_notice, e_warning, e_invalid, e_io_rom_write, e_io_rom_read, e_no_auto, e_io_read_patch } worsterror=e_none;
            LPCWSTR messages[8]={
                    L"所有补丁均应用成功!",//e_none
                    L"所有补丁均应用成功!",//e_notice (ignore)
                    L"所有补丁已应用, 但一个或多个补丁可能已损坏或创建不当...",//e_warning
                    L"部分补丁已应用, 但并非所有给定的补丁都有效...",//e_invalid
                    L"部分补丁已应用, 但并非所有请求输出的 ROM 都已创建成功...",//e_rom_io_write
                    L"部分补丁已应用, 但并非所有输入的 ROM 都可被读取...",//e_io_rom_read
                    L"部分补丁已应用, 但并非所有需要的 ROM 文件都能定位路径...",//e_no_auto
                    L"部分补丁已应用, 但并非所有给定的补丁都可被读取...",//e_io_read_patch
                };

            wcscpy(thisFileNameWithPath, patchnames);
            LPWSTR thisFileName=wcschr(thisFileNameWithPath, '\0');
            *thisFileName='\\';
            thisFileName++;

            LPWSTR thisPatchName=wcschr(patchnames, '\0')+1;
            while (*thisPatchName)
            {
                wcscpy(thisFileName, thisPatchName);
                file* patch = file::create(thisFileNameWithPath);
                {
                if (!patch)
                {
                    worsterror=max(worsterror, e_io_read_patch);
                    canUseFoundRom=false;
                    goto multi_auto_next;
                }
                bool possible;
                LPCWSTR romname=FindRomForPatch(patch, &possible);
                if (usingFoundRom)
                {
                    if (!romname) romname=foundRom;
                    else goto multi_auto_next;
                }
                else
                {
                    if (!romname)
                    {
                        if (possible) canUseFoundRom=false;
                        worsterror=max(worsterror, e_no_auto);
                        goto multi_auto_next;
                    }
                }
                if (!foundRom) foundRom=romname;
                if (foundRom!=romname) canUseFoundRom=false;

                wcscpy(GetExtension(thisFileName), GetExtension(romname));
                struct errorinfo errinf=ApplyPatchMem(patch, romname, true, thisFileNameWithPath, NULL, true);

                if (errinf.level==el_broken) worsterror=max(worsterror, e_invalid);
                if (errinf.level==el_notthis) worsterror=max(worsterror, e_no_auto);
                if (errinf.level==el_warning) worsterror=max(worsterror, e_warning);
                if (errinf.level<el_notthis) anySuccess=true;
                else canUseFoundRom=false;
                }
            multi_auto_next:
                delete patch;
                thisPatchName=wcschr(thisPatchName, '\0')+1;
            }
            if (anySuccess)
            {
                if (worsterror==e_no_auto && foundRom && canUseFoundRom && !usingFoundRom)
                {
                    usingFoundRom=true;
                    goto redo;
                }
                int severity=(worsterror==e_none ? el_ok : el_warning);
                MessageBoxW(hwndMain, messages[worsterror], ConvertUtf8ToWstring(std::string(flipsversion)).c_str(), mboxtype[severity]);
                return severity;
            }
        }
        WCHAR inromname[MAX_PATH];
        inromname[0]='\0';
        if (!SelectRom(inromname, TEXT("选择要打补丁的文件"), false)) return 0;
        WCHAR thisFileNameWithPath[MAX_PATH];
        wcscpy(thisFileNameWithPath, patchnames);
        LPWSTR thisFileName=wcschr(thisFileNameWithPath, '\0');
        *thisFileName='\\';
        thisFileName++;
        LPWSTR thisPatchName=wcschr(patchnames, '\0')+1;
        LPCWSTR romExtension=GetExtension(inromname);
        filemap* inrommap=filemap::create(inromname);
        struct mem inrom=inrommap->get();
        bool anySuccess=false;
        enum { e_none, e_notice, e_warning, e_invalid_this, e_invalid, e_io_write, e_io_read, e_io_read_rom } worsterror=e_none;
        enum errorlevel severity[2][8]={
                 { el_ok,  el_ok,  el_warning,el_broken,      el_broken,  el_broken, el_broken, el_broken },
                 { el_ok,  el_ok,  el_warning,el_warning,     el_warning, el_warning,el_warning,el_broken },
            };
        LPCWSTR messages[2][8]={
                {
                    //no error-free
                    NULL,//e_none
                    NULL,//e_notice
                    NULL,//e_warning
                    L"选择的补丁文件均非此 ROM 的有效补丁!",//e_invalid_this
                    L"选择的补丁文件均无效!",//e_invalid
                    L"无法写入任何 ROM 文件!",//e_io_write
                    L"无法读取任何补丁文件!",//e_io_read
                    L"无法读取输入的 ROM 文件",//e_io_read_rom
                },{
                    //at least one error-free
                    L"所有补丁均应用成功!",//e_none
                    L"所有补丁均应用成功!",//e_notice
                    L"所有补丁已应用, 但一个或多个补丁可能已损坏或创建不当...",//e_warning
                    L"部分补丁已应用, 但并非所有给定的补丁都对当前 ROM 文件有效...",//e_invalid_this
                    L"部分补丁已应用, 但并非所有给定的补丁都有效...",//e_invalid
                    L"部分补丁已应用, 但并非所有请求输出的 ROM 都已创建成功...",//e_io_write
                    L"部分补丁已应用, 但并非所有给定的补丁都可被读取...",//e_io_read
                    NULL,//e_io_read_rom
                },
            };
        if (inrom.ptr)
        {
            bool removeheaders=shouldRemoveHeader(inromname, inrom.len);
            while (*thisPatchName)
            {
                wcscpy(thisFileName, thisPatchName);
                file* patch = file::create(thisFileNameWithPath);
                if (patch)
                {
                    LPWSTR patchExtension=GetExtension(thisFileName);
                    wcscpy(patchExtension, romExtension);
                    struct errorinfo errinf=ApplyPatchMem2(patch, inrom, removeheaders, true, thisFileNameWithPath, NULL);

                    if (errinf.level==el_broken) worsterror=max(worsterror, e_invalid);
                    if (errinf.level==el_notthis) worsterror=max(worsterror, e_invalid_this);
                    if (errinf.level==el_warning) worsterror=max(worsterror, e_warning);
                    if (errinf.level<el_notthis)
                    {
                        if (state.enableAutoRomSelector && !anySuccess) AddToRomList(patch, inromname);
                        anySuccess=true;
                    }
                    delete patch;
                }
                else worsterror=max(worsterror, e_io_read);
                thisPatchName=wcschr(thisPatchName, '\0')+1;
            }
        }
        else worsterror=e_io_read_rom;
        delete inrommap;
        MessageBoxW(hwndMain, messages[anySuccess][worsterror], ConvertUtf8ToWstring(std::string(flipsversion)).c_str(), mboxtype[severity[anySuccess][worsterror]]);
        return severity[anySuccess][worsterror];
#undef max
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_COMMAND:
        {
            if (wParam==1) a_ApplyPatch(NULL);
            if (wParam==2) break;
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
    hwndMain=CreateWindowA(
                "flips", flipsversion,
                WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_BORDER|WS_MINIMIZEBOX,
                CW_USEDEFAULT, CW_USEDEFAULT, 240 * scaleFactor, 70 * scaleFactor, NULL, NULL, GetModuleHandle(NULL), NULL);

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
