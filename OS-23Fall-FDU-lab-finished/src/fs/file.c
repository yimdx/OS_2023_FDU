#include "file.h"
#include <common/defines.h>
#include <common/spinlock.h>
#include <common/sem.h>
#include <fs/inode.h>
#include <common/list.h>
#include <kernel/mem.h>

// the global file table.
static struct ftable ftable;

void init_ftable() {
    // TODO: initialize your ftable.
    for (int i = 0; i < NFILE; i++) {
        ftable.files[i].type = FD_NONE;
    }
    init_spinlock(&ftable.lock);
}

void init_oftable(struct oftable *oftable) {
    // TODO: initialize your oftable for a new process.
    for (int i = 0; i < NFILE; i++) {
        oftable->ofiles[i] = NULL;
    }
    init_spinlock(&oftable->lock);
}

/* Allocate a file structure. */
struct file* file_alloc() {
    /* TODO: LabFinal */

    _acquire_spinlock(&ftable.lock);
    for (int i = 0; i < NFILE; i++) {
        if (ftable.files[i].ref == 0) {
            ftable.files[i].ref = 1;
            release(&ftable.lock);
            return &ftable.files[i];
        }
    }
    _release_spinlock(&ftable.lock);
    return 0;
}

/* Increment ref count for file f. */
struct file* file_dup(struct file* f) {
    /* TODO: LabFinal */
    _acquire_spinlock(&ftable.lock);
    if (f->ref < NFILE) {
        f->ref++;
    }
    _release_spinlock(&ftable.lock);
    return f;
    return f;
}

/* Close file f. (Decrement ref count, close when reaches 0.) */
void file_close(struct file* f) {
    /* TODO: LabFinal */
    _acquire_spinlock(&ftable.lock);
    if (--f->ref == 0) {
        if (f->type == FD_INODE) {
            OpContext ctx;
            bcache.begin_op(&ctx);
            inodes.put(&ctx, f->ip);
            bcache.end_op(&ctx);
        } else if (f->type == FD_PIPE) {
            pipeClose(f->pipe, f->writable);
        }
        f->type = FD_NONE;
    }
    _release_spinlock(&ftable.lock);
}

/* Get metadata about file f. */
int file_stat(struct file* f, struct stat* st) {
    /* TODO: LabFinal */
    if (f->type == FD_INODE && f->ip) {
        inodes.lock(f->ip);
        stati(f->ip, st);
        inodes.unlock(f->ip);
        return 0;
    }
    return -1;
}

/* Read from file f. */
isize file_read(struct file* f, char* addr, isize n) {
    /* TODO: LabFinal */

    if (f->type == FD_INODE && f->readable) {
        inodes.lock(f->ip);
        isize r = inodes.read(f->ip, addr, f->off, n);
        inodes.unlock(f->ip);
        if (r > 0) f->off += r;
    
        return r;
    } else if (f->type == FD_PIPE && f->readable) {
        return pipeRead(f->pipe, addr, n);
    }
    return -1;
    // return 0;
}

/* Write to file f. */
isize file_write(struct file* f, char* addr, isize n) {
    /* TODO: LabFinal */
    if (f->type == FD_INODE && f->writable) {
        OpContext ctx;
        bcache.begin_op(&ctx);
        inodes.lock(f->ip);
        usize r = inodes.write(&ctx, f->ip, (u8*)(addr), f->off, n);
        inodes.unlock(f->ip);
        bcache.end_op(&ctx);
        if (r > 0) f->off += r;
        return r;
    } else if (f->type == FD_PIPE && f->writable) {
        return pipeWrite(f->pipe, addr, n);
    }
    return -1;
    // return 0;
}