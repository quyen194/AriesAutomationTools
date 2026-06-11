#include "single_instance.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

static HANDLE g_mutex = nullptr;

bool TryAcquireSingleInstance(const char* name) {
    g_mutex = CreateMutexA(nullptr, TRUE, name);
    if (!g_mutex) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_mutex);
        g_mutex = nullptr;
        return false;
    }
    return true;
}

void ReleaseSingleInstance() {
    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
        g_mutex = nullptr;
    }
}
