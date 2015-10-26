#ifndef _WORKER_H_FILE__
#define _WORKER_H_FILE__
//#define ALLOW_PINNING
#include "globals.h"

void* start_routine(void* arg);

extern __thread long cttr;
void barrier(int tid, int num_threads);

#ifdef ALLOW_PINNING
#include <sched.h>
#include <sys/syscall.h>
#include <syscall.h>
#include <sys/types.h>

pid_t gettid(void);
void pin(pid_t t, int cpu);
#endif

#if defined(__APPLE__)
#define pthread_yield() pthread_yield_np()
#endif

#endif //_WORKER_H_FILE__
