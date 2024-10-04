#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <sys/stat.h>
/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block cache and super block to use.
            Correspondingly, you should NEVER use global instance of
            them.

    @see init_inodes
 */
static const SuperBlock* sblock;

/**
    @brief the reference to the underlying block cache.
 */
static const BlockCache* cache;

/**
    @brief global lock for inode layer.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, ref counts, etc.
 */
static SpinLock lock;

/**
    @brief the list of all allocated in-memory inodes.

    We use a linked list to manage all allocated inodes.

    You can implement your own data structure if you want better performance.

    @see Inode
 */
static ListNode head;


// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no) {
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry* get_entry(Block* block, usize inode_no) {
    return ((InodeEntry*)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32* get_addrs(Block* block) {
    return ((IndirectBlock*)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock* _sblock, const BlockCache* _cache) {
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode* inode) {
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext* ctx, InodeType type) {
    ASSERT(type != INODE_INVALID);

    Block *b;
    InodeEntry *entry;
    _acquire_spinlock(&lock);
    for(u32 i = 1; i < sblock->num_inodes; i++){
        b = cache->acquire(sblock->inode_start + i / INODE_PER_BLOCK);
        entry = (InodeEntry*)b->data + i % INODE_PER_BLOCK;
        if(entry->type == INODE_INVALID){
            memset(entry, 0, sizeof(InodeEntry));
            entry->type = type;
            cache->sync(ctx, b);
            cache->release(b);
            _release_spinlock(&lock);
            return i;
        }
        cache->release(b);
    }
    _release_spinlock(&lock);
    PANIC();
    // TODO
    return 0;
}

// see `inode.h`.
static void inode_lock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    setup_checker(0);
    ASSERT(acquire_sleeplock(0, &inode->lock));
    checker_end_ctx(0);

    Block *b;
    InodeEntry *entry;

    if(!inode->valid){
        inode->valid = true;
        b = cache->acquire(sblock->inode_start + inode->inode_no / INODE_PER_BLOCK);
        entry = (InodeEntry*)b->data + inode->inode_no % INODE_PER_BLOCK;
        memcpy(&inode->entry, entry, sizeof(InodeEntry));
        cache->release(b);
    }

    // TODO
}

// see `inode.h`.
static void inode_unlock(Inode* inode) {
    ASSERT(inode->rc.count > 0);
    ASSERT(inode);
    setup_checker(0);
    checker_begin_ctx(0);
    release_sleeplock(0, &inode->lock);
    // TODO
}

// see `inode.h`.
static void inode_sync(OpContext* ctx, Inode* inode, bool do_write) {
    // TODO
    Block* b = cache->acquire(sblock->inode_start + inode->inode_no / INODE_PER_BLOCK);
    InodeEntry* entry = (InodeEntry*)b->data + inode->inode_no % INODE_PER_BLOCK;
    if(do_write && inode->valid){
        memmove(entry, &inode->entry, sizeof(InodeEntry));
        cache->sync(ctx, b);
    }
    else if(!inode->valid && !do_write){
        memmove(&inode->entry, entry, sizeof(InodeEntry));
        inode->valid = true;
    }else if(inode->valid && !do_write){
    }else{
        PANIC();
    }
    cache->release(b);
}

// see `inode.h`.
static Inode* inode_get(usize inode_no) {
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    _acquire_spinlock(&lock);
    // TODO
    Inode* inode;
    _for_in_list(p, &head) {
        if(p == &head) continue;
        inode = container_of(p, Inode, node);
        if (inode->inode_no == inode_no) {
            _increment_rc(&inode->rc);
            _release_spinlock(&lock);
            return inode;
        }
    }
    inode = (Inode*)kalloc(sizeof(Inode));
    init_inode(inode);
    inode->inode_no = inode_no;
    _increment_rc(&inode->rc);
    _insert_into_list(&head, &inode->node);
    _release_spinlock(&lock);
    return inode;
}
// see `inode.h`.
static void inode_clear(OpContext* ctx, Inode* inode) {
    // TODO
    Block *b;
    u32* addrs;
    for(int i = 0; i < INODE_NUM_DIRECT; i++){
        if(inode->entry.addrs[i]){
            cache->free(ctx, inode->entry.addrs[i]);
            inode->entry.addrs[i] = 0;
        }
    }
    if(inode->entry.indirect){
        b = cache->acquire(inode->entry.indirect);
        addrs = ((IndirectBlock*)b->data)->addrs;
        for(u32 i = 0; i < INODE_NUM_INDIRECT; i++){
            if(addrs[i]){
                cache->free(ctx, addrs[i]);
                addrs[i] = 0;
            }
        }
        cache->sync(ctx, b);
        cache->release(b);
        cache->free(ctx, inode->entry.indirect);
        inode->entry.indirect = 0;
    }
    inode->entry.num_bytes = 0;
    inode->valid = true;
    inode_sync(ctx, inode, true);
}

// see `inode.h`.
static Inode* inode_share(Inode* inode) {
    // TODO
    _acquire_spinlock(&lock);
    _increment_rc(&inode->rc);
    _release_spinlock(&lock);
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext* ctx, Inode* inode) {
    // TODO
    Block *b;
    InodeEntry* entry;
    _acquire_spinlock(&lock);
    if(inode->entry.num_links == 0){
        int r = inode->rc.count;
        if(r == 1){
            inode_clear(ctx, inode);
            b = cache->acquire(sblock->inode_start + inode->inode_no / INODE_PER_BLOCK);
            entry = (InodeEntry*)(b->data) + inode->inode_no % INODE_PER_BLOCK;
            entry->type = INODE_INVALID;
            cache->sync(ctx, b);
            cache->release(b);
            inode->valid = false;
            _detach_from_list(&inode->node);
            kfree(inode);
            _release_spinlock(&lock);
            return;
        }
    }
    _decrement_rc(&inode->rc);
    _release_spinlock(&lock);
    return;
}

/**
    @brief get which block is the offset of the inode in.

    e.g. `inode_map(ctx, my_inode, 1234, &modified)` will return the block_no
    of the block that contains the 1234th byte of the file
    represented by `my_inode`.

    If a block has not been allocated for that byte, `inode_map` will
    allocate a new block and update `my_inode`, at which time, `modified`
    will be set to true.

    HOWEVER, if `ctx == NULL`, `inode_map` will NOT try to allocate any new block,
    and when it finds that the block has not been allocated, it will return 0.
    
    @param[out] modified true if some new block is allocated and `inode`
    has been changed.

    @return usize the block number of that block, or 0 if `ctx == NULL` and
    the required block has not been allocated.

    @note the caller must hold the lock of `inode`.
 */
static usize inode_map(OpContext* ctx,
                       Inode* inode,
                       usize offset,
                       bool* modified) {
    // TODO
    u32 addr;
    u32* addrs;
    Block * b;
    if (modified) {
        *modified = false;
    }
    u32 block_number = offset / BLOCK_SIZE;
    if (block_number < INODE_NUM_DIRECT) {
        addr = inode->entry.addrs[block_number];
        if (addr == NULL){
            if(ctx == NULL) return 0;
            addr = inode->entry.addrs[block_number] = cache->alloc(ctx);
            if (modified) {
                *modified = true;
            }
        }
        return addr;
    }
    block_number = block_number - INODE_NUM_DIRECT;
    // printk("here%d\n",block_number);
    if (block_number < INODE_NUM_INDIRECT) {
        addr = inode->entry.indirect;
        // printk("here");
        if (addr == NULL){
            // printk("here");
            if(ctx == NULL){
                return 0;
            }
            addr = inode->entry.indirect = cache->alloc(ctx);
            if (modified) {
                *modified = true;
            }
        }
        b = cache->acquire(addr);
        addrs = ((IndirectBlock*)b->data)->addrs;
        addr = addrs[block_number];
        if (addr == NULL){
            if(ctx == NULL){
                cache->release(b);
                return 0;
            }
            addr = addrs[block_number] = cache->alloc(ctx);
            if (modified) {
                *modified = true;
            }
        }
        cache->sync(ctx, b);
        cache->release(b);
        return addr;
        //TODO
    }
    return 0;
}

// see `inode.h`.
static usize inode_read(Inode* inode, u8* dest, usize offset, usize count) {
    InodeEntry* entry = &inode->entry;
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    // TODO

    usize total, m;
    Block* b;
    bool modified = false;
    for(total = 0; total < count; total = total + m, offset = offset + m, dest = dest + m){
        b = cache->acquire(inode_map(NULL, inode, offset, &modified));
        m = (count - total) < (BLOCK_SIZE - offset%BLOCK_SIZE) ? (count - total) : (BLOCK_SIZE - offset%BLOCK_SIZE);
        memmove(dest, b->data + offset % BLOCK_SIZE, m);
        cache->release(b);
    }
    return count;
}

// see `inode.h`.
static usize inode_write(OpContext* ctx,
                         Inode* inode,
                         u8* src,
                         usize offset,
                         usize count) {
    InodeEntry* entry = &inode->entry;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    // TODO
    usize total, m;
    Block* b;
    bool modified = false;
    for(total = 0; total < count; total = total + m, offset = offset + m, src = src + m){
        b = cache->acquire(inode_map(ctx, inode, offset, &modified));
        m = (count - total) < (BLOCK_SIZE - offset%BLOCK_SIZE) ? (count - total) : (BLOCK_SIZE - offset%BLOCK_SIZE);
        memmove(b->data + offset % BLOCK_SIZE, src, m);
        cache->sync(ctx, b);
        cache->release(b);
    }
    if(count > 0 && inode->entry.num_bytes < offset){
            inode->entry.num_bytes = offset;
            inode_sync(ctx, inode, true);
        }
    return count;
}

// see `inode.h`.
static usize inode_lookup(Inode* inode, const char* name, usize* index) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    DirEntry dentry; 
    if(entry->type != INODE_DIRECTORY)
        PANIC();
    for(u32 i = 0; i < entry->num_bytes / sizeof(DirEntry); i++){
        inode_read(inode, (u8*)&dentry, i*sizeof(DirEntry), sizeof(DirEntry));
        if(!strncmp(dentry.name, name, FILE_NAME_MAX_LENGTH)){
            if(index) *index = i;
            return dentry.inode_no;
        }
    }
    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext* ctx,
                          Inode* inode,
                          const char* name,
                          usize inode_no) {
    InodeEntry* entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);
    
    // TODO
    usize index;
    if(inode_lookup(inode, name, &index) != 0){
        return -1;
    }
    DirEntry dentry;
    strncpy(dentry.name, name, FILE_NAME_MAX_LENGTH);
    dentry.inode_no = inode_no;
    usize offset = inode->entry.num_bytes;
    inode_write(ctx, inode, (u8*)(&dentry), offset, sizeof(DirEntry));
    return offset;
}

// see `inode.h`.
static void inode_remove(OpContext* ctx, Inode* inode, usize index) {
    // TODO
    DirEntry entry;
    usize offset = index * sizeof(DirEntry);
    inode_read(inode,(u8*)&entry, offset, sizeof(DirEntry));
    memset(&entry, 0, sizeof(DirEntry));
    inode_write(ctx, inode, (u8*)&entry, offset, sizeof(DirEntry));

}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};


/* LabFinal */

/**
    @brief read the next path element from `path` into `name`.
    
    @param[out] name next path element.
    @return const char* a pointer offseted in `path`, without leading `/`. If no
    name to remove, return NULL.
    @example 
    skipelem("a/bb/c", name) = "bb/c", setting name = "a",
    skipelem("///a//bb", name) = "bb", setting name = "a",
    skipelem("a", name) = "", setting name = "a",
    skipelem("", name) = skipelem("////", name) = NULL, not setting name.
 */
static const char* skipelem(const char* path, char* name) {
    const char* s;
    int len;

    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;
    s = path;
    while (*path != '/' && *path != 0)
        path++;
    len = path - s;
    if (len >= FILE_NAME_MAX_LENGTH)
        memmove(name, s, FILE_NAME_MAX_LENGTH);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

/**
    @brief look up and return the inode for `path`.
    If `nameiparent`, return the inode for the parent and copy the final
    path element into `name`.
    
    @param path a relative or absolute path. If `path` is relative, it is
    relative to the current working directory of the process.
    @param[out] name the final path element if `nameiparent` is true.
    @return Inode* the inode for `path` (or its parent if `nameiparent` is true), 
    or NULL if such inode does not exist.
    @example
    namex("/a/b", false, name) = inode of b,
    namex("/a/b", true, name) = inode of a, setting name = "b",
    namex("/", true, name) = NULL (because "/" has no parent!)
 */
static Inode* namex(const char* path,
                    bool nameiparent,
                    char* name,
                    OpContext* ctx) {
    /* TODO: LabFinal */
    return 0;
}

Inode* namei(const char* path, OpContext* ctx) {
    char name[FILE_NAME_MAX_LENGTH];
    return namex(path, false, name, ctx);
}

Inode* nameiparent(const char* path, char* name, OpContext* ctx) {
    return namex(path, true, name, ctx);
}

/**
    @brief get the stat information of `ip` into `st`.
    
    @note the caller must hold the lock of `ip`.
 */
void stati(Inode* ip, struct stat* st) {
    st->st_dev = 1;
    st->st_ino = ip->inode_no;
    st->st_nlink = ip->entry.num_links;
    st->st_size = ip->entry.num_bytes;
    switch (ip->entry.type) {
        case INODE_REGULAR:
            st->st_mode = S_IFREG;
            break;
        case INODE_DIRECTORY:
            st->st_mode = S_IFDIR;
            break;
        case INODE_DEVICE:
            st->st_mode = 0;
            break;
        default:
            PANIC();
    }
}
