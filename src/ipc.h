#ifndef GOCONTROLL_HMI_IPC_H
#define GOCONTROLL_HMI_IPC_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/**
 * Find the PID of a running process by its executable basename.
 * Scans /proc/<pid>/comm.
 *
 * @param name basename to look for (e.g. "gocontroll_demo")
 * @return     the PID, or -1 if not found.
 */
pid_t ipc_find_pid(const char *name);

/**
 * Read n bytes from remote process's virtual memory at remote_addr into dst.
 * Uses process_vm_readv(2); no kernel copy, zero syscalls beyond the one.
 *
 * @return  number of bytes read, or -1 on error (errno set).
 */
ssize_t ipc_read(pid_t pid, uintptr_t remote_addr, void *dst, size_t n);

/**
 * Write n bytes from src into remote_addr of the remote process.
 * Uses process_vm_writev(2).
 */
ssize_t ipc_write(pid_t pid, uintptr_t remote_addr, const void *src, size_t n);

#endif
