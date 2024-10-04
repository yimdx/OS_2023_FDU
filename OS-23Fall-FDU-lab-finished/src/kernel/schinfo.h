#pragma once

#include <common/list.h>
struct proc; // dont include proc.h here
struct timer;

// embedded data for cpus
struct sched
{
    // TODO: customize your sched info
    struct proc* thisproc;
    struct proc* idle;
};

// embeded data for procs
struct schinfo
{
    // TODO: customize your sched info
    ListNode runnable_queue;
    struct timer* t;
    int cnt;
    int runtime;
};
