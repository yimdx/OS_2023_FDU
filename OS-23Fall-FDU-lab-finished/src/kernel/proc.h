#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <fs/file.h>
#include <fs/inode.h>
#include <kernel/pt.h>
#include <kernel/schinfo.h>

#define NOFILE 1024 /* open files per process */

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, DEEPSLEEPING, ZOMBIE };

typedef struct UserContext
{
    // TODO: customize your trap frame
    u64 spsr, elr;
    u64 ttbr0, sp, x0;
    u64 x[17];
    
} UserContext;

typedef struct KernelContext
{
    // TODO: customize your context
    u64 lr, x0, x1;
    u64 x[11]; // x19-x29

} KernelContext;

struct proc {
    bool killed;
    bool idle;
    int pid;
    int exitcode;
    enum procstate state;
    Semaphore childexit;
    ListNode children;
    ListNode ptnode;
    struct proc *parent;
    struct schinfo schinfo;
    struct pgdir pgdir;
    void *kstack;
    UserContext *ucontext;
    KernelContext *kcontext;
    struct oftable oftable;
    Inode *cwd; // current working dictionary
};

// void init_proc(struct proc*);
WARN_RESULT struct proc *create_proc();
int start_proc(struct proc *, void (*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
WARN_RESULT int wait(int *exitcode);
WARN_RESULT int kill(int pid);
WARN_RESULT int fork();