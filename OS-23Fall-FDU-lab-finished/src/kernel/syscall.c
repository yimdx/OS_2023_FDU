#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>

void* syscall_table[NR_SYSCALL];

void syscall_entry(UserContext* context)
{
    // TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    // printk("system_call\n");
    u64 id = 0, ret = 0;
    id = context->x[7];
    
     if (id < NR_SYSCALL)
    {
        u64 arg[6];
        arg[0] = context->x0;
        for (int i = 1; i < 6; ++i)
            arg[i] = context->x[i-1];
        ret = ((u64 (*)(u64, u64, u64, u64, u64, u64))syscall_table[id])(arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
        context->x0 = ret;
    }
    else{
        context->x0 = -1;
    }
}

// check if the virtual address [start,start+size) is READABLE by the current
// user process
bool user_readable(const void *start, usize size) {
    // TODO
}

// check if the virtual address [start,start+size) is READABLE & WRITEABLE by
// the current user process
bool user_writeable(const void *start, usize size) {
    // TODO
}

// get the length of a string including tailing '\0' in the memory space of
// current user process return 0 if the length exceeds maxlen or the string is
// not readable by the current user process
usize user_strlen(const char *str, usize maxlen) {
    for (usize i = 0; i < maxlen; i++) {
        if (user_readable(&str[i], 1)) {
            if (str[i] == 0)
                return i + 1;
        } else
            return 0;
    }
    return 0;
}