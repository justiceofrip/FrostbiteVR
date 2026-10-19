#pragma once
// Declarations-only MinHook 1.3.4 subset for non-Windows syntax checks.
#include "fvr_winshim.h"
enum MH_STATUS { MH_UNKNOWN = -1, MH_OK = 0 };
#define MH_ALL_HOOKS nullptr
MH_STATUS MH_Initialize();
MH_STATUS MH_CreateHook(LPVOID target, LPVOID detour, LPVOID* original);
MH_STATUS MH_CreateHookApi(LPCWSTR module, LPCSTR proc, LPVOID detour, LPVOID* original);
MH_STATUS MH_EnableHook(LPVOID target);
MH_STATUS MH_DisableHook(LPVOID target);
