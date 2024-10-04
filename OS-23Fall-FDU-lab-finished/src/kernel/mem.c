#include "aarch64/intrinsic.h"
#include "kernel/printk.h"
#include <aarch64/mmu.h>
#include <common/checker.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/rc.h>
#include <common/spinlock.h>
#include <driver/memlayout.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <driver/memlayout.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/defines.h>
#include <common/string.h>

#define K_DEBUG 0

#define FAIL(...)                                                              \
    {                                                                          \
        printk(__VA_ARGS__);                                                   \
        while (1)                                                              \
            ;                                                                  \
    }

static struct Block {
    struct Block *next;             // free list
}memory_table[13];

typedef struct Block Block;
typedef struct PageHeader {
    u64 blocksize;                 // 16 bit aligned
}PageHeader;


RefCount alloc_page_cnt;            // how many pages are allocated
static SpinLock mem_lock;           // spinlock on memory_table
static SpinLock page_lock;          // spinlock on page granlarity

// All usable pages are added to the queue.
static QueueNode* pages;            // fix-lengthed meta-data of the allocator are stored in .bss
extern char end[];

struct page page_refs[(PAGE_BASE(PHYSTOP) + PAGE_SIZE)/PAGE_SIZE];
// init used page to 0
define_early_init(alloc_page_cnt){
    init_rc(&alloc_page_cnt);
}

// init locks and clear pages
define_early_init(pages){
    for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE; p < P2K(PHYSTOP); p += PAGE_SIZE) add_to_queue(&pages, (QueueNode*)p); 
    for (u64 i = 0; i <= 12; i++){
        memory_table[i].next = NULL;
    }
    init_spinlock(&mem_lock);
    init_spinlock(&page_lock);
}

// alloc pages
void* kalloc_page(){
    _acquire_spinlock(&page_lock);
    _increment_rc(&alloc_page_cnt);
    QueueNode* new_page = fetch_from_queue(&pages);
    memset(new_page, 0, PAGE_SIZE);
    page_refs[K2P(new_page)/PAGE_SIZE].ref.count = 1;
    _release_spinlock(&page_lock);
    return new_page;
}

u64 left_page_cnt() { return PAGE_COUNT - alloc_page_cnt.count; }
bool zero_page_alloced = false;
void* zero_page;
define_init(zero_page){
    zero_page = kalloc_page();
}
WARN_RESULT void *get_zero_page() {
    return zero_page;
    // TODO
    // Return the shared zero page
}

// free page
void ref_page(void* p){
     _acquire_spinlock(&page_lock);
    _increment_rc(&page_refs[K2P(p)/PAGE_SIZE].ref);
    _release_spinlock(&page_lock);
}
void kfree_page(void* p){
    _acquire_spinlock(&page_lock);
    _decrement_rc(&page_refs[K2P(p)/PAGE_SIZE].ref);
    if(page_refs[K2P(p)/PAGE_SIZE].ref.count == 0 && p != zero_page){
        _decrement_rc(&alloc_page_cnt);
    // memset(p, 0, PAGE_SIZE);
        add_to_queue(&pages, (QueueNode*)p);
    }
    _release_spinlock(&page_lock);
}

u64 pow(u64 a, u64 b){
    u64 ans = 1;
    while(b > 0){
        if(b % 2 == 1) ans = ans * a;
        b = b / 2;
        a = a * a;
    }
    return ans;
}

u64 get_lowest_pow(u64 size){
    if(size <= 0) return 0;
    size -= 1;
    for(u64 i = 0;i <= 12; i++){
        if(size == 0) return i;
        size = size / 2;
    }
    return 12;
}

// Allocate: fetch a page from the queue of usable pages.

// find the page of an address
PageHeader* get_page_header(void* p){
    return (PageHeader*)((u64)p - (u64)p % PAGE_SIZE);
}

// add to free list
void add_memory(u64 blocksize, void* p){
    u64 mem_key = get_lowest_pow(blocksize);
    ((Block*)p)->next =  memory_table[mem_key].next;
        memory_table[mem_key].next = p;
}

// get a free block from free list
void* pop_memory(u64 blocksize){
    u64 mem_key = get_lowest_pow(blocksize);
    void* ret = memory_table[mem_key].next;
    memory_table[mem_key].next = memory_table[mem_key].next->next;
    return ret;
}

// Free: add the page to the queue of usable pages.

void* kalloc(isize size_input)
{
    _acquire_spinlock(&mem_lock);
    u64 size = (u64)size_input;
    if(size < 8) size = 8;
    u64 mem_key = get_lowest_pow(size);
    u64 blocksize = pow(2, mem_key);
    if(memory_table[mem_key].next == NULL){
        PageHeader *pagestart = kalloc_page();
        pagestart -> blocksize = blocksize;
        for(struct Block* p = (Block*)((u64)pagestart + 64); (u64)p + blocksize <= ((u64)pagestart + PAGE_SIZE); p = (Block* )((u64)p + blocksize)){
            add_memory(blocksize, p);
        }
    }
    void* ret = pop_memory(blocksize);
    _release_spinlock(&mem_lock);
    return ret;
}

void kfree(void* p)
{
    PageHeader* pagestart = get_page_header(p);
    u64 blocksize = pagestart->blocksize;
    _acquire_spinlock(&mem_lock);
    add_memory(blocksize, p);
    _release_spinlock(&mem_lock);
}