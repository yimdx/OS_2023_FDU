#include <kernel/mem.h>
#include <kernel/sched.h>
#include <fs/pipe.h>
#include <common/string.h>

int pipeAlloc(File** f0, File** f1) {
    // TODO

    Pipe *p = kalloc();
    if (p == 0) {
        return -1; // Allocation failed
    }
    
    // Initialize the pipe
    init_spinlock(&p->lock);
    init_sem(&p->wlock, 0);
    init_sem(&p->rlock, 0);
    p->nread = p->nwrite = 0;
    p->readopen = p->writeopen = 1;
    // Allocate two file structures for the read and write ends of the pipe
    *f0 = file_alloc(); // filealloc() allocates a new file object
    *f1 = file_alloc();
    if (*f0 == 0 || *f1 == 0) {
        // Cleanup if allocation failed
        if (*f0) file_close(*f0);
        if (*f1) file_close(*f1);
        kfree(p);
        return -1;
    }

    // Initialize file structures
    (*f0)->type = FD_PIPE;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pipe = p;
    
    (*f1)->type = FD_PIPE;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pipe = p;

    return 0; // Success
}

void pipeClose(Pipe* pi, int writable) {
    // TODO

    _acquire_spinlock(&pi->lock);
    
    if (writable) {
        pi->writeopen = 0;
        post_all_sem(&pi->rlock); // Wake up any blocked readers
    } else {
        pi->readopen = 0;
        post_all_sem(&pi->wlock); // Wake up any blocked writers
    }
    
    if (pi->readopen == 0 && pi->writeopen == 0) {
        _release_spinlock(&pi->lock);
        kfree(pi); // Assuming freePipe frees the Pipe structure
    } else {
        _release_spinlock(&pi->lock);
    }

}

int pipeWrite(Pipe* pi, u64 addr, int n) {
    // TODO

    _acquire_spinlock(&pi->lock);
    for (int i = 0; i < n; i++) {
        // Wait if the pipe is full
        while ((pi->nwrite == (pi->nread + PIPESIZE)) && pi->readopen) {
            wait_sem(&pi->wlock);
        }
        if (!pi->readopen) {
            _release_spinlock(&pi->lock);
            return -1; // Read end is closed
        }
        pi->data[pi->nwrite++ % PIPESIZE] = ((char*)addr)[i];
        post_all_sem(&pi->rlock); // Wake up any blocked readers
    }
    _release_spinlock(&pi->lock);
    return n;
}

int pipeRead(Pipe* pi, u64 addr, int n) {
    // TODO

    _acquire_spinlock(&pi->lock);
    int i;
    for (i = 0; i < n; i++) {
        // Wait if the pipe is empty
        while ((pi->nread == pi->nwrite) && pi->writeopen) {
            wait_sem(&pi->rlock);
        }
        if (pi->nread == pi->nwrite) {
            break; // Pipe is empty and write end is closed
        }
        ((char*)addr)[i] = pi->data[pi->nread++ % PIPESIZE];
        post_all_sem(&pi->wlock); // Wake up any blocked writers
    }
    _release_spinlock(&pi->lock);
    return i; // Number of bytes read
}