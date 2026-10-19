// Declarations-only stand-in for the Windows SDK subset the BF4 adapter uses, so the
// Windows translation units can be TYPE-CHECKED with g++/clang on Linux CI
// (tools/wincheck/check.sh). Never linked, never shipped, never used on Windows.
#pragma once
#ifdef _WIN32
#error "fvr_winshim.h is for non-Windows syntax checks only"
#endif
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <strings.h>

#define WINAPI
#define CALLBACK
#define STDMETHODCALLTYPE
#define __cdecl
#define __declspec(x)
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260

using BOOL = int; using BYTE = std::uint8_t; using WORD = std::uint16_t; using DWORD = std::uint32_t;
using LONG = std::int32_t; using ULONG = std::uint32_t; using UINT = unsigned; using SHORT = std::int16_t;
using ULONGLONG = std::uint64_t; using HRESULT = std::int32_t; using LPVOID = void*; using LPCVOID = const void*;
using WCHAR = wchar_t; using LPCWSTR = const wchar_t*; using LPWSTR = wchar_t*; using LPCSTR = const char*;
using LPSTR = char*; using ATOM = WORD; using SIZE_T = std::size_t; using LONG_PTR = std::intptr_t;
using UINT_PTR = std::uintptr_t; using LPARAM = LONG_PTR; using WPARAM = UINT_PTR; using LRESULT = LONG_PTR;
struct HWND__ { int unused; }; using HWND = HWND__*;
struct HINSTANCE__ { int unused; }; using HINSTANCE = HINSTANCE__*; using HMODULE = HINSTANCE;
using HANDLE = void*; struct HMENU__ { int unused; }; using HMENU = HMENU__*;
struct HICON__ { int unused; }; using HICON = HICON__*; using HCURSOR = HICON; struct HBRUSH__ { int unused; }; using HBRUSH = HBRUSH__*;
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-1)))

struct POINT { LONG x, y; }; using LPPOINT = POINT*;
struct RECT { LONG left, top, right, bottom; }; using LPRECT = RECT*;
struct SYSTEMTIME { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; };
struct FILETIME { DWORD lo, hi; };
struct WIN32_FILE_ATTRIBUTE_DATA { DWORD dwFileAttributes; FILETIME c, a, w; DWORD nFileSizeHigh, nFileSizeLow; };
enum GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard };
struct SECURITY_ATTRIBUTES; using LPSECURITY_ATTRIBUTES = SECURITY_ATTRIBUTES*;
using LPTHREAD_START_ROUTINE = DWORD (*)(LPVOID);
using WNDPROC = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);
struct WNDCLASSEXW { UINT cbSize, style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance;
                     HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName, lpszClassName; HICON hIconSm; };
struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; }; using LPMSG = MSG*;

#define MOVEFILE_REPLACE_EXISTING 0x1
#define GA_ROOT 2
#define WS_OVERLAPPED 0x0L
#define WS_OVERLAPPEDWINDOW 0x00CF0000L
#define SW_SHOW 5
#define PM_REMOVE 1
#define WM_DESTROY 0x0002
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define MK_LBUTTON 0x0001
#define VK_LBUTTON 0x01
#define DLL_PROCESS_ATTACH 1
#define FILE_MAP_WRITE 0x0002
#define FILE_MAP_READ 0x0004
#define PAGE_READWRITE 0x04
#define SYNCHRONIZE 0x00100000L
#define WAIT_TIMEOUT 258L
#define LOAD_WITH_ALTERED_SEARCH_PATH 0x00000008
#define CP_UTF8 65001
#define S_OK 0
#define E_FAIL static_cast<HRESULT>(0x80004005L)
#define SUCCEEDED(hr) ((static_cast<HRESULT>(hr)) >= 0)
#define FAILED(hr) ((static_cast<HRESULT>(hr)) < 0)
#define LOWORD(l) (static_cast<WORD>(static_cast<std::uintptr_t>(l) & 0xffff))
#define HIWORD(l) (static_cast<WORD>((static_cast<std::uintptr_t>(l) >> 16) & 0xffff))
#define MAKELPARAM(l, h) (static_cast<LPARAM>(static_cast<DWORD>((static_cast<WORD>(l)) | (static_cast<DWORD>(static_cast<WORD>(h)) << 16))))

struct IMAGE_DOS_HEADER { WORD e_magic; WORD pad[29]; LONG e_lfanew; };
struct IMAGE_FILE_HEADER { WORD Machine, NumberOfSections; DWORD TimeDateStamp, PointerToSymbolTable, NumberOfSymbols;
                           WORD SizeOfOptionalHeader, Characteristics; };
struct IMAGE_OPTIONAL_HEADER64 { WORD Magic; BYTE rest[238]; };
struct IMAGE_NT_HEADERS { DWORD Signature; IMAGE_FILE_HEADER FileHeader; IMAGE_OPTIONAL_HEADER64 OptionalHeader; };
struct IMAGE_SECTION_HEADER { BYTE Name[8]; union { DWORD PhysicalAddress; DWORD VirtualSize; } Misc; DWORD VirtualAddress,
                              SizeOfRawData, PointerToRawData, PointerToRelocations, PointerToLinenumbers;
                              WORD NumberOfRelocations, NumberOfLinenumbers; DWORD Characteristics; };
#define IMAGE_FIRST_SECTION(nt) (reinterpret_cast<IMAGE_SECTION_HEADER*>(const_cast<BYTE*>(reinterpret_cast<const BYTE*>(nt)) + 24 + (nt)->FileHeader.SizeOfOptionalHeader))

BOOL GetFileAttributesExA(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
BOOL MoveFileExA(LPCSTR, LPCSTR, DWORD);
void GetLocalTime(SYSTEMTIME*);
HANDLE CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, DWORD*);
BOOL CloseHandle(HANDLE);
HANDLE GetCurrentProcess();
DWORD GetCurrentProcessId();
inline int _strnicmp(const char* a, const char* b, std::size_t n) { return strncasecmp(a, b, n); }
int _wcsicmp(const wchar_t*, const wchar_t*);
DWORD GetWindowThreadProcessId(HWND, DWORD*);
HWND GetAncestor(HWND, UINT);
ULONGLONG GetTickCount64();
BOOL GetClientRect(HWND, LPRECT);
BOOL ClientToScreen(HWND, LPPOINT);
BOOL PostMessageW(HWND, UINT, WPARAM, LPARAM);
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
HMODULE GetModuleHandleW(LPCWSTR);
ATOM RegisterClassExW(const WNDCLASSEXW*);
HWND CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
BOOL DestroyWindow(HWND);
BOOL UnregisterClassW(LPCWSTR, HINSTANCE);
BOOL DisableThreadLibraryCalls(HMODULE);
using FARPROC = std::intptr_t (*)();
FARPROC GetProcAddress(HMODULE, LPCSTR);
HANDLE OpenFileMappingW(DWORD, BOOL, LPCWSTR);
HANDLE CreateFileMappingW(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCWSTR);
LPVOID MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
BOOL UnmapViewOfFile(LPCVOID);
HANDLE OpenProcess(DWORD, BOOL, DWORD);
DWORD WaitForSingleObject(HANDLE, DWORD);
HMODULE LoadLibraryExW(LPCWSTR, HANDLE, DWORD);
HMODULE LoadLibraryW(LPCWSTR);
int MultiByteToWideChar(UINT, DWORD, LPCSTR, int, LPWSTR, int);
int WideCharToMultiByte(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, BOOL*);
DWORD GetModuleFileNameW(HMODULE, LPWSTR, DWORD);
BOOL ShowWindow(HWND, int);
BOOL AdjustWindowRect(LPRECT, DWORD, BOOL);
BOOL PtInRect(const RECT*, POINT);
BOOL PeekMessageW(LPMSG, HWND, UINT, UINT, UINT);
BOOL TranslateMessage(const MSG*);
LRESULT DispatchMessageW(const MSG*);
void PostQuitMessage(int);
BOOL SetEnvironmentVariableW(LPCWSTR, LPCWSTR);
void Sleep(DWORD);
BOOL EnumProcessModules(HANDLE, HMODULE*, DWORD, DWORD*);
UINT GetSystemDirectoryW(LPWSTR, UINT);
DWORD GetCurrentThreadId();
DWORD GetFileAttributesW(LPCWSTR);
#define INVALID_FILE_ATTRIBUTES (static_cast<DWORD>(-1))
inline std::FILE* _wfopen(const wchar_t*, const wchar_t*) { return nullptr; }
DWORD GetModuleBaseNameA(HANDLE, HMODULE, LPSTR, DWORD);

// ---- COM / DXGI / D3D11 subset ------------------------------------------------------
struct GUID { DWORD a; WORD b, c; BYTE d[8]; };
using IID = GUID; using REFIID = const IID&;
template <typename T> const GUID& fvr_uuidof() { static const GUID g{}; return g; }
#define __uuidof(T) fvr_uuidof<T>()
struct IUnknown { virtual HRESULT QueryInterface(REFIID, void**) = 0; virtual ULONG AddRef() = 0; virtual ULONG Release() = 0; };
enum DXGI_FORMAT { DXGI_FORMAT_UNKNOWN = 0, DXGI_FORMAT_R8G8B8A8_UNORM = 28, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
                   DXGI_FORMAT_B8G8R8A8_UNORM = 87, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91 };
enum DXGI_SWAP_EFFECT { DXGI_SWAP_EFFECT_DISCARD = 0, DXGI_SWAP_EFFECT_FLIP_DISCARD = 4 };
#define DXGI_USAGE_RENDER_TARGET_OUTPUT 0x00000020UL
struct DXGI_RATIONAL { UINT Numerator, Denominator; };
struct DXGI_MODE_DESC { UINT Width, Height; DXGI_RATIONAL RefreshRate; DXGI_FORMAT Format; UINT ScanlineOrdering, Scaling; };
struct DXGI_SAMPLE_DESC { UINT Count, Quality; };
struct DXGI_SWAP_CHAIN_DESC { DXGI_MODE_DESC BufferDesc; DXGI_SAMPLE_DESC SampleDesc; UINT BufferUsage; UINT BufferCount;
                              HWND OutputWindow; BOOL Windowed; DXGI_SWAP_EFFECT SwapEffect; UINT Flags; };
struct LUID { DWORD LowPart; LONG HighPart; };
struct DXGI_ADAPTER_DESC { WCHAR Description[128]; UINT VendorId, DeviceId, SubSysId, Revision; SIZE_T DedicatedVideoMemory,
                           DedicatedSystemMemory, SharedSystemMemory; LUID AdapterLuid; };
struct DXGI_ADAPTER_DESC1 { WCHAR Description[128]; UINT VendorId, DeviceId, SubSysId, Revision; SIZE_T DedicatedVideoMemory,
                            DedicatedSystemMemory, SharedSystemMemory; LUID AdapterLuid; UINT Flags; };
struct IDXGIAdapter : IUnknown { virtual HRESULT GetDesc(DXGI_ADAPTER_DESC*) = 0; };
struct IDXGIAdapter1 : IDXGIAdapter { virtual HRESULT GetDesc1(DXGI_ADAPTER_DESC1*) = 0; };
struct IDXGIFactory1 : IUnknown { virtual HRESULT EnumAdapters1(UINT, IDXGIAdapter1**) = 0; };
struct IDXGISwapChain : IUnknown {
    virtual HRESULT Present(UINT, UINT) = 0;
    virtual HRESULT GetBuffer(UINT, REFIID, void**) = 0;
    virtual HRESULT GetDesc(DXGI_SWAP_CHAIN_DESC*) = 0;
    virtual HRESULT GetDevice(REFIID, void**) = 0;
};
struct ID3D11Device;
struct ID3D11DeviceChild : IUnknown { virtual void GetDevice(ID3D11Device**) = 0; };
struct ID3D11Resource : ID3D11DeviceChild {};
enum D3D11_USAGE { D3D11_USAGE_DEFAULT = 0, D3D11_USAGE_IMMUTABLE = 1, D3D11_USAGE_DYNAMIC = 2, D3D11_USAGE_STAGING = 3 };
#define D3D11_BIND_SHADER_RESOURCE 0x8L
#define D3D11_CPU_ACCESS_READ 0x20000L
enum D3D11_MAP { D3D11_MAP_READ = 1 };
struct D3D11_MAPPED_SUBRESOURCE { void* pData; UINT RowPitch; UINT DepthPitch; };
struct D3D11_SUBRESOURCE_DATA { const void* pSysMem; UINT SysMemPitch, SysMemSlicePitch; };
struct D3D11_TEXTURE2D_DESC { UINT Width, Height, MipLevels, ArraySize; DXGI_FORMAT Format; DXGI_SAMPLE_DESC SampleDesc;
                              D3D11_USAGE Usage; UINT BindFlags, CPUAccessFlags, MiscFlags; };
struct ID3D11Texture2D : ID3D11Resource { virtual void GetDesc(D3D11_TEXTURE2D_DESC*) = 0; };
struct ID3D11RenderTargetView : ID3D11DeviceChild {};
struct ID3D11DepthStencilView : ID3D11DeviceChild {};
struct D3D11_RENDER_TARGET_VIEW_DESC;
struct ID3D11DeviceContext : ID3D11DeviceChild {
    virtual void OMSetRenderTargets(UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*) = 0;
    virtual void ClearRenderTargetView(ID3D11RenderTargetView*, const float[4]) = 0;
    virtual void CopyResource(ID3D11Resource*, ID3D11Resource*) = 0;
    virtual void ResolveSubresource(ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT) = 0;
    virtual HRESULT Map(ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*) = 0;
    virtual void Unmap(ID3D11Resource*, UINT) = 0;
};
struct ID3D11Device : IUnknown {
    virtual void GetImmediateContext(ID3D11DeviceContext**) = 0;
    virtual HRESULT CreateRenderTargetView(ID3D11Resource*, const D3D11_RENDER_TARGET_VIEW_DESC*, ID3D11RenderTargetView**) = 0;
    virtual HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**) = 0;
};
enum D3D_DRIVER_TYPE { D3D_DRIVER_TYPE_UNKNOWN, D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_REFERENCE, D3D_DRIVER_TYPE_NULL,
                       D3D_DRIVER_TYPE_SOFTWARE, D3D_DRIVER_TYPE_WARP };
enum D3D_FEATURE_LEVEL { D3D_FEATURE_LEVEL_11_0 = 0xb000 };
#define D3D11_SDK_VERSION 7
HRESULT D3D11CreateDeviceAndSwapChain(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                                      const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*,
                                      ID3D11DeviceContext**);
