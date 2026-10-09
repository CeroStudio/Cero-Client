#pragma once
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
extern HANDLE g_mc_process;
typedef HANDLE process_handle_t;
#define PROCESS_HANDLE_INVALID NULL
#else
#include <sys/types.h>
extern pid_t g_mc_process;
typedef pid_t process_handle_t;
#define PROCESS_HANDLE_INVALID (-1)
#endif

int process_run(const char* exe, const char* const argv[]);
void process_kill(void);

int  process_spawn(const char* exe, const char* const argv[], process_handle_t* out);
int  process_wait(process_handle_t h);
void process_close(process_handle_t h);
void process_terminate(process_handle_t h);