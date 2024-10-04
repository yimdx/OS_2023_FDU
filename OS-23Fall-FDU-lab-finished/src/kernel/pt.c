#include <aarch64/intrinsic.h>
#include <common/string.h>
#include <kernel/mem.h>
#include <kernel/pt.h>
#include <kernel/printk.h>
#include <kernel/paging.h>
// #define DEBUG 1

PTEntriesPtr get_pte(struct pgdir *pgdir, u64 va, bool alloc) {
    // TODO
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.
    u64 pd_index_0 = (va >> 39) & 0x1FF; 
    u64 pd_index_1 = (va >> 30) & 0x1FF; 
    u64 pd_index_2 = (va >> 21) & 0x1FF; 
    u64 pt_index = (va >> 12) & 0x1FF;
#ifdef DEBUG
    printk("pgdir = %lld, va = %lld\n", (u64)(pgdir->pt), va);
#endif
    if (pgdir->pt == NULL) {
        if (!alloc) {
            return NULL; 
        }
        pgdir->pt = kalloc_page();
        if (pgdir->pt == NULL) {
            return NULL;
        }
    }
    auto pde_1 = (PTEntriesPtr)(pgdir->pt[pd_index_0]);
#ifdef DEBUG
    printk("pd_index_0 = %lld, pde_1 = %lld\n", pd_index_0, (u64)pde_1);
#endif
    if (pde_1 == NULL) {
        if (!alloc) {
            return NULL; 
        }
        pde_1 = (PTEntriesPtr)kalloc_page();
        pgdir->pt[pd_index_0] = K2P(pde_1)|PTE_TABLE;
        if (pde_1 == NULL) {
            return NULL;
        }
    }
    pde_1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pgdir->pt[pd_index_0]));
    auto pde_2 = (PTEntriesPtr)(pde_1[pd_index_1]);
#ifdef DEBUG
    printk("pd_index_1 = %lld, pde_2 = %lld\n", pd_index_1, (u64)pde_2);
#endif
    if (pde_2 == NULL) {
        if (!alloc) {
            return NULL; 
        }
        pde_2 = (PTEntriesPtr)kalloc_page();
        pde_1[pd_index_1] = K2P(pde_2)|PTE_TABLE;
        if (pde_2 == NULL) {
            return NULL;
        }
    }
    pde_2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pde_1[pd_index_1]));
    auto pde_3 = (PTEntriesPtr)(pde_2[pd_index_2]);
#ifdef DEBUG
    printk("pd_index_2 = %lld, pde_3 = %lld\n", pd_index_2, (u64)pde_3);
#endif
    if (pde_3 == NULL) {
        if (!alloc) {
            return NULL; 
        }
        pde_3 = kalloc_page();
        pde_2[pd_index_2] = K2P(pde_3)|PTE_TABLE;
        if (pde_3 == NULL) {
            return NULL;
        }
    }
    pde_3 = (PTEntriesPtr)P2K(PTE_ADDRESS(pde_2[pd_index_2]));
    PTEntriesPtr pte = &(pde_3[pt_index]);
#ifdef DEBUG
    printk("alloced, pgdir = %lld, va = %lld\n", (u64)(pgdir->pt), va);
#endif
    return pte;
}

void init_pgdir(struct pgdir *pgdir) { 
    pgdir->pt = kalloc_page();; 
    init_list_node(&pgdir->section_head);
    init_spinlock(&pgdir->lock);
    _acquire_spinlock(&pgdir->lock);
    init_sections(&pgdir->section_head);
    _release_spinlock(&pgdir->lock);
}

void free_pgdir(struct pgdir *pgdir) {
    // TODO
    free_sections(pgdir);
    if(pgdir->pt == NULL) return;
    for(u64 i = 0; i < N_PTE_PER_TABLE; i++){
        auto pde_1 = (PTEntriesPtr)(pgdir->pt[i]);
        if(!pde_1) continue;
        pde_1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pgdir->pt[i]));
        for(u64 i = 0; i < N_PTE_PER_TABLE; i++){
            auto pde_2 = (PTEntriesPtr)(pde_1[i]);
            if(!pde_2) continue;
            pde_2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pde_1[i]));
            for(u64 i = 0; i < N_PTE_PER_TABLE; i++){
                auto pde_3 = (PTEntriesPtr)(pde_2[i]);
                if(!pde_3) continue;
                pde_3 = (PTEntriesPtr)P2K(PTE_ADDRESS(pde_2[i]));
                kfree_page(pde_3);
            }
            kfree_page(pde_2);
        }
        kfree_page(pde_1);
    }   
    kfree_page(pgdir->pt);
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
}

void attach_pgdir(struct pgdir *pgdir) {
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}

void vmmap(struct pgdir *pd, u64 va, void *ka, u64 flags) {
    // TODO
    // Map virtual address 'va' to the physical address represented by kernel
    // address 'ka' in page directory 'pd', 'flags' is the flags for the page
    // table entry

    PTEntriesPtr pte;
    pte = get_pte(pd, va, 1);
    *pte =  K2P(ka) | flags | PTE_TABLE;
    ref_page(ka);
}