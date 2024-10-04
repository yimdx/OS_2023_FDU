#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>


struct proc root_proc;
static SpinLock treelock;

void kernel_entry();
void proc_entry();

define_early_init(proc_tree)
{
    init_spinlock(&treelock);
    
}


#define MAX_PID 2048
#define BITMAP_SIZE (MAX_PID / 32 + ((MAX_PID % 32) ? 1 : 0))

struct PidMap {
    u32 pid_map[BITMAP_SIZE];
    int last_pid;
};

static struct PidMap pid_alloc;

define_early_init(pidalloc){
    pid_alloc.last_pid = -1;
    for (int i = 0; i < BITMAP_SIZE; ++i) {
        pid_alloc.pid_map[i] = 0;
    }
}
int get_pid() {
    int offset = (pid_alloc.last_pid + 1) % MAX_PID;
    for (int pid = offset; pid < MAX_PID; ++pid) {
        int index = pid / 32;
        int bit = pid % 32;
        u32 mask = 1U << bit;
        if (!(pid_alloc.pid_map[index] & mask)) {
            pid_alloc.pid_map[index] |= mask; 
            pid_alloc.last_pid = pid;
            return pid;
        }
    }
    for (int pid = 0; pid < offset; ++pid) {
        int index = pid / 32;
        int bit = pid % 32;
        u32 mask = 1U << bit;
        if (!(pid_alloc.pid_map[index] & mask)) {
            pid_alloc.pid_map[index] |= mask;
            pid_alloc.last_pid = pid;
            return pid;
        }
    }
    return -1; 
}

bool check_pid(int pid){
    return (pid >= 0 && pid < MAX_PID);
}

void release_pid(int pid) {
    ASSERT(pid >= 0 && pid < MAX_PID);
        int index = pid / 32;
        int bit = pid % 32;
        u32 mask = 1U << bit;
        pid_alloc.pid_map[index] &= ~mask;
}

void set_parent_to_this(struct proc* proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    _acquire_spinlock(&treelock);
    proc->parent = thisproc();
    _insert_into_list(&thisproc()->children, &proc->ptnode);
    _release_spinlock(&treelock);

}

NO_RETURN void exit(int code)
{
    // TODO
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 4. notify the parent
    // 5. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    _acquire_spinlock(&treelock);
    struct proc* this = thisproc();
    this->exitcode = code;
    kfree_page(this->kstack);
    free_pgdir(&this->pgdir);    
    if(!_empty_list(&thisproc()->children)){
        _for_in_list(p, &thisproc()->children){
            if(p == &thisproc()->children) continue;
            struct proc* candidate = container_of(p, struct proc, ptnode);
            candidate->parent = &root_proc;
            if(is_zombie(candidate)) {
                post_sem(&root_proc.childexit);
            }
        }       
        auto children = _detach_from_list(&thisproc()->children);
        if(children) _merge_list(&root_proc.children, children);
    }
    post_sem(&thisproc()->parent->childexit);
    _release_spinlock(&treelock);
    _acquire_sched_lock();
    _sched(ZOMBIE);
    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int* exitcode)
{
    // TODO
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    // NOTE: be careful of concurrency
    // if(thisproc()->pid == 0) printk("this pid, id = %d\n", thisproc()->pid);
    _acquire_spinlock(&treelock);
    auto this = thisproc();
    if(_empty_list(&this->children)) {
        _release_spinlock(&treelock);
        return -1;
    }
    _release_spinlock(&treelock);
    bool ret = wait_sem(&this->childexit);
    if(!ret) printk("signal interrupted\n");
    _acquire_spinlock(&treelock);
    _for_in_list(p, &thisproc()->children){
        if(p == &thisproc()->children) continue;
        struct proc* candidate = container_of(p, struct proc, ptnode);
        if(is_zombie(candidate)){
            *exitcode = candidate->exitcode;
            int id = candidate->pid;
            _detach_from_list(&candidate->ptnode);
            release_pid(candidate->pid);
            kfree(candidate);
            _release_spinlock(&treelock);
            return id;
        }
    }
     _release_spinlock(&treelock);
     PANIC();
    return -1;
}

struct proc* dfs_tree(struct proc* fa, int pid){
    // printk("dfs search pid = %d, this = %d\n", pid, fa->pid);
    _for_in_list(p, &fa->children){
        if(p == &fa->children) continue;
        struct proc* child = container_of(p, struct proc, ptnode);
        
        if(child->pid == pid && child->state != UNUSED){
            child->killed = 1;
            _release_spinlock(&treelock);
            alert_proc(child);
            return child;
        }
        struct proc* res = dfs_tree(child,pid);
        if (res != NULL) return res;
        
    }
    return NULL;
};

int kill(int pid)
{
    _acquire_spinlock(&treelock);
    if(!check_pid(pid)){
        printk("invalid pid\n");
        _release_spinlock(&treelock);
        return -1;
    }
    if(!dfs_tree(&root_proc, pid)) {
        _release_spinlock(&treelock);
        return -1;
    }
    _release_spinlock(&treelock);
    return 0;
    // TODO
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
    
}

int start_proc(struct proc* p, void(*entry)(u64), u64 arg)
{
    // TODO
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency
    _acquire_spinlock(&treelock);
    if(p->parent == NULL){
        p->parent = &root_proc;
        _insert_into_list(&root_proc.children, &p->ptnode);
    }
    _release_spinlock(&treelock);
    p->kcontext->lr = (u64)&proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;
    int id = p->pid;
    activate_proc(p);
    return id;
}

void init_proc(struct proc* p)
{
    // TODO
    // setup the struct proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    memset(p, 0, sizeof(*p));
    _acquire_spinlock(&treelock);
    p->killed = 0;
    p->idle = 0;
    p->state = UNUSED;
    p->pid = get_pid();
    init_sem(&p->childexit, 0);
    init_pgdir(&p->pgdir);
    p->kstack = kalloc_page();
    init_schinfo(&p->schinfo);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    p->kcontext = (KernelContext*)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(KernelContext) - sizeof(UserContext));
    p->ucontext = (UserContext*)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(UserContext));
    // printk("init proc pid = %d\n", p->pid);
    _release_spinlock(&treelock);

}

struct proc* create_proc()
{
    struct proc* p = kalloc(sizeof(struct proc));
    init_proc(p);
    return p;
}

define_init(root_proc)
{
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}


/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 */
void trap_return();
int fork() { /* TODO: Your code here. */
}