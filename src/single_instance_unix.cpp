#include "single_instance.hpp"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <cstdio>
#include <string>

static int         g_lockFd   = -1;
static std::string g_lockPath;

bool TryAcquireSingleInstance(const char* name) {
    g_lockPath = std::string("/tmp/") + name + ".lock";
    g_lockFd   = open(g_lockPath.c_str(), O_CREAT | O_RDWR, 0666);
    if (g_lockFd < 0) return false;
    if (flock(g_lockFd, LOCK_EX | LOCK_NB) != 0) {
        close(g_lockFd);
        g_lockFd = -1;
        return false;
    }
    return true;
}

void ReleaseSingleInstance() {
    if (g_lockFd >= 0) {
        flock(g_lockFd, LOCK_UN);
        close(g_lockFd);
        g_lockFd = -1;
    }
}
