#include <aarch64/mmu.h>
#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <common/string.h>
#include <fs/block_device.h>
#include <fs/cache.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <kernel/printk.h>
#include <kernel/proc.h>
#include <kernel/pt.h>
#include <kernel/sched.h>

define_rest_init(paging) {
    // TODO
    // nothing to do
}

void init_sections(ListNode *section_head) {
    // TODO
    // init_list_node(section_head);
    struct section *heap_section = kalloc(sizeof(struct section));
    if (heap_section == NULL) {
        PANIC();
        return;
    }
    heap_section->flags = ST_HEAP;
    heap_section->begin = 0x0;
    heap_section->end = 0x0; // init heap 0
    init_list_node(&heap_section->stnode);
    _insert_into_list(section_head, &heap_section->stnode);

}

void free_sections(struct pgdir *pd) {
    // TODO
    _acquire_spinlock(&(pd->lock));
    _for_in_list(p, &pd->section_head){
        if(p == &pd->section_head) continue;
        struct section* cur_section = container_of(p, struct section, stnode);
        for(auto start = PAGE_BASE(cur_section->begin); start < cur_section->end; start+=PAGE_SIZE){
            PTEntriesPtr pte = get_pte(&(thisproc()->pgdir), start, false);
            if(pte && ((*pte) & PTE_VALID)){
                void* ka = (void*)P2K(PTE_ADDRESS(*pte));
                kfree_page(ka);
                *(pte) = 0;
            }
        }
    }
    arch_tlbi_vmalle1is();
    
    //todo delete section list
    ListNode *node = pd->section_head.next;
    while (node != &(pd->section_head)) {
        struct section* cur_section = container_of(node, struct section, stnode);
        node = node->next;
        kfree(cur_section);
    }
    _release_spinlock(&(pd->lock));
}

u64 sbrk(i64 size) {
    // TODO:
    // Increase the heap size of current process by `size`
    // If `size` is negative, decrease heap size
    // `size` must be a multiple of PAGE_SIZE
    // Return the previous heap_end
    if(size % PAGE_SIZE) return -1;
    _acquire_spinlock(&(thisproc()->pgdir.lock));
    _for_in_list(p, &thisproc()->pgdir.section_head){
        if(p == &thisproc()->pgdir.section_head) continue;
        struct section* cur_section = container_of(p, struct section, stnode);
        if(cur_section->flags & ST_HEAP){
            auto ret = cur_section->end;
            cur_section->end += size;
            if(size < 0){
                ASSERT(cur_section->end >= cur_section->begin);
                //todo free all pages
                for(auto start = PAGE_BASE(cur_section->end); start < PAGE_BASE(ret); start+=PAGE_SIZE){
                    PTEntriesPtr pte = get_pte(&(thisproc()->pgdir), start, false);
                    if(pte && ((*pte) & PTE_VALID)){
                        void* ka = (void*)P2K(PTE_ADDRESS(*pte));
                        kfree_page(ka);
                        *(pte) = 0;
                    }
                }
            }
            _release_spinlock(&(thisproc()->pgdir.lock));
            arch_tlbi_vmalle1is();
            return ret;
        }
    }
    _release_spinlock(&(thisproc()->pgdir.lock));
    return -1;
}

int pgfault_handler(u64 iss) {
    struct proc *p = thisproc();
    struct pgdir *pd = &p->pgdir;
    iss = iss;
    u64 addr = arch_get_far(); // Attempting to access this address caused the
                               // page fault
    // TODO:
    // 1. Find the section struct that contains the faulting address `addr`
    // 2. Check section flags to determine page fault type
    // 3. Handle the page fault accordingly
    // 4. Return to user code or kill the process
    
    struct section *fault_section = NULL;
    _acquire_spinlock(&(pd->lock));
    ListNode *node = pd->section_head.next;
    while (node != &(pd->section_head)) {
        struct section *sec = container_of(node, struct section, stnode);
        if (addr >= sec->begin && addr < sec->end) {
            fault_section = sec;
            break;
            // find the wrong seg;
        }
        node = node->next;
    }
    _release_spinlock(&(pd->lock));
    // seg fault
    if(!fault_section){
        // struct section *heap = container_of(pd->section_head.next, struct section, stnode);
        // printk("addr=%lld, end=%lld\n", addr, heap->end);
        PANIC();
        return -1;
    }
    //check
    if(fault_section->flags & ST_HEAP){
        // new page 
        // printk("enter lazy allocation\n");
        u64 pageBoundary = PAGE_BASE(addr);
        void* mem = kalloc_page();
        if (mem == 0) {
            PANIC();
            return -1;
        }
        PTEntriesPtr pte = get_pte(pd, addr, 1);
        if(PTE_FLAGS(*pte) & PTE_RO && (iss&0x2)){
            // copy on write
            void* original_page = (void*)P2K(PTE_ADDRESS(*get_pte(pd, addr, 1)));
            memcpy(mem, original_page, PAGE_SIZE);
            kfree_page(original_page);
        }
        // lazy allocation
        vmmap(pd, pageBoundary, mem, PTE_USER_DATA);
        // release ref
        kfree_page(mem);
   

    //other kind of seg should be concerned, but in this lab it is enough.
    }else{
        thisproc()->killed = 1;
        PANIC();
        return -1;
    }

    // Step 4: Return to user code
    // PANIC();
    arch_tlbi_vmalle1is();
    return 0; // Success
    
}