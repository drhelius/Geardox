/*
 * Geardox - MS-DOS PC/AT Emulator
 * Copyright (C) 2026  Ignacio Sanchez

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 *
 */

#ifndef PROCESS_UTILS_H
#define PROCESS_UTILS_H

#if defined(_WIN32)
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#define PROCESS_UTILS_POSIX
#endif

inline unsigned long get_process_id()
{
#if defined(_WIN32)
    return (unsigned long)GetCurrentProcessId();
#elif defined(PROCESS_UTILS_POSIX)
    return (unsigned long)getpid();
#else
    return 1;
#endif
}

inline bool is_process_running(unsigned long pid)
{
    if (pid == 0)
        return false;

#if defined(_WIN32)
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);

    if (process == NULL)
        return GetLastError() == ERROR_ACCESS_DENIED;

    bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
#elif defined(PROCESS_UTILS_POSIX)
    return (kill((pid_t)pid, 0) == 0) || (errno == EPERM);
#else
    return pid == get_process_id();
#endif
}

#endif /* PROCESS_UTILS_H */
