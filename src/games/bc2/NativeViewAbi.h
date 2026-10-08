#pragma once
#include <cstdint>
#include <Windows.h>
namespace fvr::bc2 {
// x86 native ABI only. The factory copies the source character range and
// registers a zero-reference view. AddRef owns it; final Release unregisters.
struct NativeStringRange {const char* begin;const char* end;const char* capacity;};
using CreateViewFn=void*(__thiscall*)(void*,const NativeStringRange*);
using ViewRefFn=unsigned(__thiscall*)(void*);
using SetViewActiveFn=void(__thiscall*)(void*,bool);
inline bool RestoreBorrowedViewPointer(volatile LONG* slot,unsigned fresh,unsigned saved)noexcept {
    return unsigned(InterlockedCompareExchange(slot,LONG(saved),LONG(fresh)))==fresh;
}
static_assert(sizeof(NativeStringRange)==12);
}
