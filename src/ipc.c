#define _GNU_SOURCE
#include "ipc.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

pid_t ipc_find_pid(const char *name)
{
    DIR *dir = opendir("/proc");
    if (!dir) return -1;

    pid_t found = -1;
    struct dirent *de;
    while ((de = readdir(dir))) {
        if (de->d_type != DT_DIR) continue;
        char *end;
        long pid = strtol(de->d_name, &end, 10);
        if (*end != '\0' || pid <= 0) continue;

        char path[64];
        snprintf(path, sizeof(path), "/proc/%ld/comm", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char comm[128] = {0};
        if (fgets(comm, sizeof(comm), f)) {
            comm[strcspn(comm, "\n")] = '\0';
            if (strcmp(comm, name) == 0) {
                found = (pid_t)pid;
                fclose(f);
                break;
            }
        }
        fclose(f);
    }
    closedir(dir);
    return found;
}

ssize_t ipc_read(pid_t pid, uintptr_t remote_addr, void *dst, size_t n)
{
    struct iovec local  = { .iov_base = dst,                    .iov_len = n };
    struct iovec remote = { .iov_base = (void *)remote_addr,    .iov_len = n };
    return process_vm_readv(pid, &local, 1, &remote, 1, 0);
}

ssize_t ipc_write(pid_t pid, uintptr_t remote_addr, const void *src, size_t n)
{
    struct iovec local  = { .iov_base = (void *)src,            .iov_len = n };
    struct iovec remote = { .iov_base = (void *)remote_addr,    .iov_len = n };
    return process_vm_writev(pid, &local, 1, &remote, 1, 0);
}
