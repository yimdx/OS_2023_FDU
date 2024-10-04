#include <common/bitmap.h>
#include <common/string.h>
#include <fs/cache.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/proc.h>

/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block device and super block to use.
            Correspondingly, you should NEVER use global instance of
            them, e.g. `get_super_block`, `block_device`

    @see init_bcache
 */
static const SuperBlock *sblock;

/**
    @brief the reference to the underlying block device.
 */
static const BlockDevice *device; 

/**
    @brief global lock for block cache.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, etc.
 */
static SpinLock lock;

/**
    @brief the list of all allocated in-memory block.

    We use a linked list to manage all allocated cached blocks.

    You can implement your own data structure if you like better performance.

    @see Block
 */
static ListNode head;

static LogHeader header; // in-memory copy of log header block.
SpinLock loglock;
/**
    @brief a struct to maintain other logging states.
    
    You may wonder where we store some states, e.g.
    
    * how many atomic operations are running?
    * are we checkpointing?
    * how to notify `end_op` that a checkpoint is done?

    Put them here!

    @see cache_begin_op, cache_end_op, cache_sync
 */
struct {
    /* your fields here */
    // enum opstate{RUN, COMMITED, CHECKED} state;
    int committing;
    int outstanding; 
    Semaphore busy;
    SleepLock lock;
} log;

// read the content from disk.
static INLINE void device_read(Block *block) {
    device->read(block->block_no, block->data);
}

// write the content back to disk.
static INLINE void device_write(Block *block) {
    device->write(block->block_no, block->data);
}

// read log header from disk.
static INLINE void read_header() {
    device->read(sblock->log_start, (u8 *)&header);
}

// write log header back to disk.
static INLINE void write_header() {
    device->write(sblock->log_start, (u8 *)&header);
}

// initialize a block struct.
static void init_block(Block *block) {
    block->block_no = 0;
    init_list_node(&block->node);
    block->acquired = false;
    block->pinned = false;

    init_sleeplock(&block->lock);
    block->valid = false;
    memset(block->data, 0, sizeof(block->data));
}

// see `cache.h`.
static usize get_num_cached_blocks() {
    // TODO
    int block_number = 0;
    for (ListNode *n = head.next; n != &head; n = n->next) {
        block_number++;
    }
    // printk("block number: %d\n", block_number);
    return block_number;
}

// see `cache.h`.
static Block *cache_acquire(usize block_no) {
    // printk("block_no: %d\n", block_no);
    _acquire_spinlock(&lock);
    ListNode *current = head.next;
    while (current != &head) {
        Block *block = container_of(current, Block, node);
        if (block->block_no == block_no) {
            // printk("find the block, block_no = %d\n", block_no);
            block->acquired = true;
            _release_spinlock(&lock);
            setup_checker(0);
            ASSERT(acquire_sleeplock(0, &block->lock));
            if (!block->valid && !block->pinned) {
                // printk("device read point 1, block_no = %d\n", block_no);
                device_read(block);
                block->valid = true;
            }
            checker_end_ctx(0);
            return block;
        }
        current = current->next;
    }
    // If block not in cache, find an unused buffer (LRU)
    for (ListNode *n = head.prev; n != &head; n = n->prev) {
        Block *b = container_of(n, Block, node);
        if (!b->acquired && !b->pinned) {
            b->block_no = block_no;
            b->acquired = true;
            b->valid = false;
            _release_spinlock(&lock);
            setup_checker(0);
            ASSERT(acquire_sleeplock(0, &b->lock));
            // printk("device read point 2, block_no = %d\n", block_no);
            device_read(b); // Read block from disk
            b->valid = true;
            checker_end_ctx(0);
            return b;
        }
    }
    // printk("cache_acquire: no available buffers");
    Block* b = kalloc(sizeof(Block));
    init_block(b);
    b->block_no = block_no;
    b->acquired = true;
    b->valid = false;
    _release_spinlock(&lock);
    setup_checker(0);
    ASSERT(acquire_sleeplock(0, &b->lock));
    _acquire_spinlock(&lock);
    // printk("device read point 3\n");
    device_read(b); // Read block from disk
    b->valid = true;
    checker_end_ctx(0);
    _insert_into_list(head.prev, &b->node);
    _release_spinlock(&lock);
    return b;

}


// see `cache.h`.
static void cache_release(Block *block) {
    // TODO
    setup_checker(0);
    checker_begin_ctx(0);
    release_sleeplock(0, &block->lock);
    _acquire_spinlock(&lock);
    block->acquired = false;

    // Move to the front of the list to implement LRU
    _detach_from_list(&block->node);

    if(!block->pinned && get_num_cached_blocks() > EVICTION_THRESHOLD){    
        kfree(block);
    }else{
        _insert_into_list(&head, &block->node);
    }
    _release_spinlock(&lock);
}

void recover_from_log(){
     _acquire_spinlock(&loglock);
    usize tail = 0; // if committed, copy from log to disk
    for (tail = 0; tail < header.num_blocks; tail++) {
        Block *from = cache_acquire(sblock->log_start+tail+1); // log block
        Block *to = cache_acquire(header.block_no[tail]); // cache block
        memmove(to->data, from->data, BLOCK_SIZE);
        // printk("commit, write to disk: %lld\n", to->block_no);
        device_write(to);  // write the log
        cache_release(from);
        cache_release(to);
    }
    header.num_blocks = 0;
    write_header();
    _release_spinlock(&loglock);
}
// see `cache.h`.
void init_bcache(const SuperBlock *_sblock, const BlockDevice *_device) {
    sblock = _sblock;
    device = _device;

    init_spinlock(&lock);
    init_list_node(&head);

    // Initialize all buffer blocks
    for (int i = 0; i < EVICTION_THRESHOLD; i++) {
        // Block* b = &buf[i];
        Block* b = kalloc(sizeof(Block));
        init_block(b);
        _insert_into_list(&head, &b->node);
    }

    log.committing = 0;
    log.outstanding = 0;
    init_spinlock(&loglock);
    init_sleeplock(&log.lock);
    read_header();
    recover_from_log();
    // TODO
}

// see `cache.h`.
static void cache_begin_op(OpContext *ctx) {
    // TODO
    // printk("begin op\n");
    if(!ctx) PANIC();
    while(1){
        setup_checker(0);
        _acquire_spinlock(&loglock);
        if(log.committing){
            // printk("op commiting\n");
            _release_spinlock(&loglock);
            ASSERT(acquire_sleeplock(0, &log.lock));
        }else if(header.num_blocks + (log.outstanding+1)*OP_MAX_NUM_BLOCKS > LOG_MAX_SIZE){
            // printk("op too many blocks\n");
            _release_spinlock(&loglock);
            ASSERT(acquire_sleeplock(0, &log.lock));
        } else {
            // printk("op begins\n");
            log.outstanding += 1;
            ctx->rm = OP_MAX_NUM_BLOCKS;
            _release_spinlock(&loglock);
            break;
        }
        checker_end_ctx(0);
    }
}



// see `cache.h`.
static void cache_sync(OpContext *ctx, Block *block) {
    // TODO
    // printk("sync, block_no = %d\n", block->block_no);
    if(!ctx){
        _acquire_spinlock(&lock);
        device_write(block);
        _release_spinlock(&lock);
    }else{
        _acquire_spinlock(&loglock);
        block->pinned = true;
        usize i = 0;
        for (i = 0; i < header.num_blocks; i++) {
            if (header.block_no[i] == block->block_no)   // log absorbtion
            break;
        }
        header.block_no[i] = block->block_no;
        if(i == header.num_blocks){
            if(ctx->rm == 0) ASSERT(0);
            header.num_blocks++;
            ctx->rm--;
            // printk("add head number, rm = %lld\n", ctx->rm);
        }
        _release_spinlock(&loglock);
    };  
}

void commit(){
    usize tail;
    // printk("commit,header number block: %lld\n", header.num_blocks);
    for (tail = 0; tail < header.num_blocks; tail++) {
        Block *to = cache_acquire(sblock->log_start+tail+1); // log block
        Block *from = cache_acquire(header.block_no[tail]); // cache block
        memmove(to->data, from->data, BLOCK_SIZE);
        // printk("commit, write to log: %lld\n", to->block_no);
        device_write(to);  // write the log
        cache_release(from);
        cache_release(to);
    }
    write_header();

    // printk("commit,header number block: %lld\n", header.num_blocks);
    for (tail = 0; tail < header.num_blocks; tail++) {
        Block *from = cache_acquire(sblock->log_start+tail+1); // log block
        Block *to = cache_acquire(header.block_no[tail]); // cache block
        memmove(to->data, from->data, BLOCK_SIZE);
        // printk("commit, write to disk: %lld\n", to->block_no);
        device_write(to);  // write the disk
        to->pinned = false;
        cache_release(from);
        cache_release(to);
    }
    header.num_blocks = 0;
    write_header();
}

// see `cache.h`.
static void cache_end_op(OpContext *ctx) {
    // TODO
    // printk("end op\n");
    if(!ctx) PANIC();

    int do_commit = 0;

    _acquire_spinlock(&loglock);
    log.outstanding -= 1;
    if(log.committing)
        PANIC();
    if(log.outstanding == 0){
        do_commit = 1;
        log.committing = 1;
    } else {
        post_all_sem(&log.lock);
    }
    _release_spinlock(&loglock);

    if(do_commit){
        // call commit w/o holding locks, since not allowed
        // to sleep with locks.
        // printk("commit\n");
        _acquire_spinlock(&loglock);
        commit();
        log.committing = 0;
        post_all_sem(&log.lock);
        _release_spinlock(&loglock);
    }

}

// see `cache.h`.
static usize cache_alloc(OpContext *ctx) {
    // TODO
    // printk("bitmapstart:%lld", sblock->bitmap_start);
    for(usize b_i = sblock->bitmap_start; b_i < sblock->bitmap_start + 1 + sblock->num_blocks/BIT_PER_BLOCK; b_i++){
        // printk("here\n");
        Block* b = cache_acquire(b_i);
        for(usize i = 0; i < BIT_PER_BLOCK && i+(b_i-sblock->bitmap_start)*BIT_PER_BLOCK < sblock->num_blocks; i++){
            //  printk("alloc check: %lld\n", i);
            u8 m = 1 << (i % 8);
            if((b->data[i/8] & m) == 0){  // Is block free?
                b->data[i/8] |= m;  // Mark block in use.
                cache_sync(ctx, b);
                cache_release(b);
                // printk("alloc: %lld\n", (b_i-sblock->bitmap_start) * BIT_PER_BLOCK + i);
                auto new_b = cache_acquire((b_i-sblock->bitmap_start) * BIT_PER_BLOCK + i);
                memset(new_b->data, 0, sizeof(new_b->data));
                cache_sync(ctx, new_b);
                cache_release(new_b);
                return (b_i-sblock->bitmap_start) * BIT_PER_BLOCK + i ;
            }
        }
        cache_release(b);
    }
    PANIC();
}

// see `cache.h`.
static void cache_free(OpContext *ctx, usize block_no) {
        int bi, m;
        bi = block_no % BIT_PER_BLOCK;
        Block *b = cache_acquire(sblock->bitmap_start + block_no/BIT_PER_BLOCK);
        m = 1 << (bi % 8);
        b->data[bi/8] &= ~m;
        cache_sync(ctx, b);
        cache_release(b);
}

BlockCache bcache = {
    .get_num_cached_blocks = get_num_cached_blocks,
    .acquire = cache_acquire,
    .release = cache_release,
    .begin_op = cache_begin_op,
    .sync = cache_sync,
    .end_op = cache_end_op,
    .alloc = cache_alloc,
    .free = cache_free,
};