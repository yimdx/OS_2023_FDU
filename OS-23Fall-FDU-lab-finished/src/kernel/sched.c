#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <driver/clock.h>

#define ELAPSE 7

extern bool panic_flag;

extern void swtch(KernelContext* new_ctx, KernelContext** old_ctx);

static SpinLock runlock;
static ListNode runnable_queue;

define_early_init(runnable_queue)
{
    init_spinlock(&runlock);
    init_list_node(&runnable_queue);
    
}
define_init(idle)
{
    for(int i = 0; i < NCPU; i++){
        struct proc* p = kalloc(sizeof(struct proc));
        p->idle = 1;
        p->state = RUNNING;
        init_schinfo(&p->schinfo);
        // p->kstack = kalloc_page();
        p->schinfo.t->elapse = ELAPSE;
        cpus[i].sched.thisproc = p;
        cpus[i].sched.idle = p;
    }
}

struct proc* thisproc()
{
    // TODO: return the current process
    return cpus[cpuid()].sched.thisproc;

}

void interrupt(struct timer* t)
{
    // printk("pid %d: interrupt\n", thisproc()->pid);
    t->data++;
    _acquire_sched_lock();
    _sched(RUNNABLE);
}

void init_schinfo(struct schinfo* p)
{
    // TODO: initialize your customized schinfo for every newly-created process
   init_list_node(&p->runnable_queue);
   p->t = kalloc(sizeof(struct timer));
   p->cnt = 0;
   p->t->triggered = false;
   p->t->elapse = ELAPSE;
   p->t->handler = interrupt;
}

void _acquire_sched_lock()
{
    // TODO: acquire the sched_lock if need
    _acquire_spinlock(&runlock);
}

void _release_sched_lock()
{
    // TODO: release the sched_lock if need
    _release_spinlock(&runlock);

}

bool is_zombie(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

bool is_unused(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == UNUSED;
    _release_sched_lock();
    return r;
}

bool _activate_proc(struct proc* p, bool onalert)
{
    // TODO
    // if the proc->state is RUNNING/RUNNABLE, do nothing
    // if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
    // else: panic
    _acquire_sched_lock();
    // printk("activate proc, pid = %d\n", p->pid);
    if(p->state == RUNNABLE || p->state == RUNNING){
        _release_sched_lock();
        return false;
    }else if(p->state == SLEEPING || p->state == UNUSED){
        p->state = RUNNABLE;
        _insert_into_list(&runnable_queue, &p->schinfo.runnable_queue);
    }else if(p->state == DEEPSLEEPING){
        if(onalert == true) {
            _release_sched_lock();
            return false;
        }
        else{
            p->state = RUNNABLE;
            _insert_into_list(&runnable_queue, &p->schinfo.runnable_queue);
        }
    }else{
        _release_sched_lock();
        return false;
    }
    _release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    
    thisproc()->state = new_state;
    if (new_state == SLEEPING || new_state == ZOMBIE || new_state == DEEPSLEEPING)
        _detach_from_list(&thisproc()->schinfo.runnable_queue);
    
}

static struct proc* pick_next()
{
    struct proc* ans = NULL;
    _for_in_list(p, &runnable_queue){
        if(p == &runnable_queue) continue;
        struct proc* candidate = container_of(p, struct proc, schinfo.runnable_queue);
        if(candidate->state == RUNNABLE){
            if(!ans) {
                ans = candidate;
                break;
            }
            else if(ans->schinfo.cnt > candidate->schinfo.cnt) ans = candidate;
        }
    }
    if(ans){
        ans->schinfo.cnt++;
        return ans;
    }
    // printk("***********************switch to idle*************************\n");
    return cpus[cpuid()].sched.idle;
    // TODO: if using simple_sched, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process

}

static void update_this_proc(struct proc* p)
{
    // TODO: if using simple_sched, you should implement this routinue
    // update thisproc to the choosen process, and reset the clock interrupt if need 
    

    cpus[cpuid()].sched.thisproc = p;
    // _acquire_sched_lock();
    // if(!p->idle && p->state == RUNNABLE) 
        set_cpu_timer(p->schinfo.t);
    // _release_sched_lock();
}

const char * print_str = "switch to pid = %d , x0 = %llx, lr = %llx,\n";

// A simple scheduler.
// You are allowed to replace it with whatever you like.
static void simple_sched(enum procstate new_state)
{
    auto this = thisproc();
    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    if(thisproc()->schinfo.t->triggered == false) cancel_cpu_timer(thisproc()->schinfo.t);
    auto next = pick_next();
    update_this_proc(next);
    // printk("switch to pid = %d, state = %d\n", next->pid, next->state);
    ASSERT(next->state == RUNNABLE || next->idle == 1);
    next->state = RUNNING;
    if (next != this)
    {
        // attach_pgdir(&next->pgdir);
        // printk(print_str, next->pid, next->kcontext->x0,  next->kcontext->lr);
        // printk("switch to pid = %d, state = %d, kcont = %llx, ucont = %llx\n", next->pid, next->state, K2P(next->kcontext), K2P(next->ucontext));
        swtch(next->kcontext, &this->kcontext);
    }
    _release_sched_lock();
}

__attribute__((weak, alias("simple_sched"))) void _sched(enum procstate new_state);

u64 proc_entry(void(*entry)(u64), u64 arg)
{
    _release_sched_lock();
    set_return_addr(entry);
    return arg;
}

