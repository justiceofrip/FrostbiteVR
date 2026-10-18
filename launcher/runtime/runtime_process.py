"""Exact read-only process creation identity for portable startup helpers."""
import ctypes as c
from ctypes import wintypes as w


def creation_filetime(process):
    kernel=c.WinDLL('kernel32',use_last_error=True)
    kernel.GetProcessTimes.argtypes=[w.HANDLE]+[c.POINTER(w.FILETIME)]*4
    kernel.GetProcessTimes.restype=w.BOOL
    values=[w.FILETIME() for _ in range(4)]
    if not kernel.GetProcessTimes(process.h,*(c.byref(x) for x in values)):
        raise OSError(c.get_last_error(),'Read process creation time')
    return (values[0].dwHighDateTime<<32)|values[0].dwLowDateTime
