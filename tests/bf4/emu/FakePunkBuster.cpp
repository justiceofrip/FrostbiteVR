// Built as pbcl.dll for the emulator only. Its presence in the module list is the
// multiplayer evidence the adapter must react to. Never packaged.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
