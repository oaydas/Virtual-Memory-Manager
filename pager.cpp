#include <cstddef>
#include <cassert>
#include <cstring>
#include <memory>
#include <iostream>

#include "pager.h"
#include "pager_utils.h"

uintptr_t ARENA_BASE = reinterpret_cast<uintptr_t>(VM_ARENA_BASEADDR);
unsigned char* BASE_ADDR;

unsigned int MAX_PHYS_PAGES;

pid_t current_pid;

std::unordered_map<unsigned int, std::shared_ptr<phys_page_t>> page_map;
std::queue<std::shared_ptr<phys_page_t>> clock_queue;
std::unordered_map<pid_t, pcb_t> process_map;
std::unordered_map<std::string, block_map> file_backed_pages;
std::unordered_set<unsigned int> open_phys_pages;
std::unordered_set<unsigned int> open_swap_pages;
std::vector<std::unordered_set<pid_t>> swap_file;

int num_swap_block_available;
/*
 * vm_init
 *
 * Called when the pager starts.  It should set up any internal data structures
 * needed by the pager.
 *
 * vm_init is passed the number of physical memory pages and the number
 * of blocks in the swap file.
 */
void vm_init(unsigned int memory_pages, unsigned int swap_blocks){
    // initialize the globals 
    BASE_ADDR = static_cast<unsigned char*>(vm_physmem);
    MAX_PHYS_PAGES = memory_pages;

    swap_file.resize(swap_blocks);
    num_swap_block_available = swap_blocks;

    // initialize physical page data strcutre
    // i = 0 is the zero pinned page which is never evicted
    for(unsigned int i = 1 ; i < memory_pages; ++i){
        std::shared_ptr<phys_page_t> page = std::make_shared<phys_page_t>(); 
        page->ppn = i;
        page_map[i] = page;
        open_phys_pages.insert(i);
    }

    for(unsigned int i = 0; i < swap_blocks; ++i){
        open_swap_pages.insert(i);
    }

    // Create zero pinned page
    std::memset(BASE_ADDR, 0, VM_PAGESIZE);
    // check_states();
} // vm_init()

/*
 * vm_create
 * Called when a parent process (parent_pid) creates a new process (child_pid).
 * vm_create should cause the child's arena to have the same mappings and data
 * as the parent's arena.  If the parent process is not being managed by the
 * pager, vm_create should consider the arena to be empty.
 * Note that the new process is not run until it is switched to via vm_switch.
 * Returns 0 on success, -1 on failure.
 */
int vm_create(pid_t parent_pid, pid_t child_pid){
    // check_states();
    // std::cout << "vm_create called\n";
    // check_states();
    // If the process is not being managed by the pager
    if (process_map.find(parent_pid) == process_map.end()){
        process_map[child_pid];
    } 
    else {
        pcb_t &parent = process_map[parent_pid];

        if(parent.num_swap_reserved > num_swap_block_available){
            return -1;
        }

        process_map[child_pid] = parent;

        num_swap_block_available -= parent.num_swap_reserved;

        // make sure pages are marked as shared (swap_backed)
        for(size_t i = 0; i < process_map[child_pid].next_vm_page; ++i){
            auto &file_info = process_map[child_pid].pages_on_disk[i];

            auto &parent_pte = process_map[parent_pid].page_table[i];
            auto &child_pte = process_map[child_pid].page_table[i];

            if(!file_info.file_backed){

                // Add count to pages pointing at block in swap file
                swap_file[file_info.block].insert(child_pid);

                parent_pte.write_enable = 0;
                child_pte.write_enable = 0;

                // If parent page is currently RESIDENT, add child PTE to physical page
                if (parent_pte.read_enable && parent_pte.ppage != 0) {
                    page_map[parent_pte.ppage]->ptes.emplace(child_pid, i);
                }

            }
            else {
                auto &pte_q = file_backed_pages[file_info.filename].block_to_file[file_info.block].ptes;

                pte_q.emplace(child_pid, i);

                if (parent_pte.read_enable && parent_pte.ppage != 0) {
                    page_map[parent_pte.ppage]->ptes.emplace(child_pid, i);
                }
            }
        }
    }
    // check_states();
    return 0;
} // vm_create()

/*
 * vm_switch
 *
 * Called when the kernel is switching to a new process, with process
 * identifier "pid".
 */
void vm_switch(pid_t pid){
    // check_states();
    // std::cout << "vm_switch called\n";
    // check_states();
    // assert(process_map.find(pid) != process_map.end());

    current_pid = pid;
    page_table_base_register = process_map[pid].page_table;

    // check_states();
} // vm_switch()


/*
 * vm_fault
 *
 * Called when current process has a fault at virtual address addr.  write_flag
 * is true if the access that caused the fault is a write.
 * Returns 0 on success, -1 on failure.
 */
int vm_fault(const void* addr, bool write_flag){
    // check_states();
    // print_page_map();
    auto va = reinterpret_cast<uintptr_t>(addr);
    auto& pcb = process_map[current_pid];

    // NOT VALID VIRTUAL ADDRESS
    if (va < ARENA_BASE || va >= ARENA_BASE + VM_ARENA_SIZE)  {
        return -1;
    }

    // get vpn
    auto vpn = static_cast<unsigned int>((va - ARENA_BASE) / VM_PAGESIZE);

    // Get pte & disk_info
    page_table_entry_t &pte = pcb.page_table[vpn];
    file_info_t &disk_info = pcb.pages_on_disk[vpn];

    // Invalid Virtual Address
    if (!disk_info.valid) {
        return -1;
    }

    if (disk_info.file_backed) {
        // Find next available page in physical memory & handle eviction
        unsigned int next_page = get_next_ppn();

        void* destination = BASE_ADDR + (static_cast<size_t>(next_page * VM_PAGESIZE));
        return file_backed_fault(pte, disk_info, next_page, destination, vpn);
    }

    if (pte.read_enable && !pte.write_enable) {

        return swap_back_fault_in_memory(pte, disk_info, vpn);
    }

    if (!pte.read_enable) {
        // Find next available page in physical memory & handle eviction
        unsigned int next_page = get_next_ppn();

        void* destination = BASE_ADDR + (static_cast<size_t>(next_page * VM_PAGESIZE));
        return swap_back_disk(pte, disk_info, next_page, destination, write_flag, vpn);
    }


    return 0;
}

/*
 * vm_destroy
 *
 * Called when current process exits.  This gives the pager a chance to
 * clean up any resources used by the process.
 */
void vm_destroy(){
    // Update dirty and reference bits of phys memory pages before any pte is destroyed
    update_reference_bits();

    num_swap_block_available += process_map[current_pid].num_swap_reserved;

    for(size_t i = 0; i < process_map[current_pid].next_vm_page; ++i){

        page_table_entry_t& pte = process_map[current_pid].page_table[i];

        file_info_t& file_info = process_map[current_pid].pages_on_disk[i];

        if(!file_info.file_backed){
            swap_file[file_info.block].erase(current_pid);

            if (swap_file[file_info.block].size() == 0) {
                open_swap_pages.insert(file_info.block);
            }
            else if (swap_file[file_info.block].size() == 1) {
                // std::cout << "File_info.block: " << file_info.block << std::endl;
                auto last_pid = *swap_file[file_info.block].begin();

                auto &pte_swap = process_map[last_pid].page_table[i];

                if (pte_swap.read_enable && pte_swap.ppage != 0) {
                    // std::cout << "READ WRITE SET TO 1" << std::endl;
                    set_pte_bits(pte_swap, -1, 1, 1, -1, -1);
                }
            }
        }
        else {
            // should remove it from the file_backed_pages data stryctyre;
            auto &ptes_q = file_backed_pages[file_info.filename].block_to_file[file_info.block].ptes;
            size_t e = ptes_q.size();
            for(size_t j = 0; j < e; ++j){
                auto &pair = ptes_q.front();
                ptes_q.pop();
                if (pair.first != current_pid){
                    ptes_q.push(pair);
                }
            }
        }

        set_pte_bits(pte, 0, 0, 0, 0, 0);
    }

    // correct the state of our global phys_page map
    for (size_t p = 1; p < MAX_PHYS_PAGES; p++) {
        auto &phys_page = page_map[p];

        size_t n = phys_page->ptes.size();
        // remove entrys that are from this process
        for (size_t i = 0; i < n; i++) {
            auto& pair = phys_page->ptes.front();
            phys_page->ptes.pop();

            if (pair.first != current_pid) {
                phys_page->ptes.push(pair);
            }
        }

        // clear and set free the phys_page if it only was for this process
        if (phys_page->ptes.empty() && !phys_page->file_backed) {
            open_phys_pages.insert(p);

            // remove from clock algorithm:
            size_t n = clock_queue.size();
            for(size_t i = 0; i < n; ++i){
                auto page = clock_queue.front();
                clock_queue.pop();

                if (page->ppn != p) {
                    clock_queue.push(page);
                }
            }

            phys_page->ref = 0;
            phys_page->dirty = 0;
            phys_page->file_backed = 0;
            phys_page->block = -1;
            phys_page->filename = "";
        }

    }
    process_map.erase(current_pid);

    // std::cout << "END" << std::endl;
    // check_states();
}

/*
 * vm_map
 *
 * A request by the current process for the lowest invalid virtual page in
 * the process's arena to be declared valid.  On success, vm_map returns
 * the lowest address of the new virtual page.  vm_map returns nullptr if
 * the arena is full.
 *
 * If filename is nullptr, block is ignored, and the new virtual page is
 * backed by the swap file, is initialized to all zeroes (from the
 * application's perspective), and private (i.e., not shared with any other
 * virtual page).  In this case, vm_map returns nullptr if the swap file is
 * out of space.
 *
 * If filename is not nullptr, it points to a null-terminated C string that
 * specifies a file (the name of the file is specified relative to the pager's
 * current working directory).  In this case, the new virtual page is backed
 * by the specified file at the specified block and is shared with other virtual
 * pages that are mapped to that file and block.  The C string pointed to by
 * filename must reside completely in the valid portion of the arena.
 * In this case, vm_map returns nullptr if the C string pointed to by filename
 * is not completely in the valid part of the arena.
 */
void* vm_map(const char* filename, unsigned int block){
    // check_states();
    // std::cout << "vm_map called\n";
    // check_states();
    auto &pcb = process_map[current_pid];

    unsigned int vpn = pcb.next_vm_page;

    if (static_cast<uintptr_t>(vpn * VM_PAGESIZE) >= VM_ARENA_SIZE){
        return nullptr; // arena is full
    }

    uintptr_t address = reinterpret_cast<uintptr_t>(VM_ARENA_BASEADDR) + (static_cast<uintptr_t>(vpn * VM_PAGESIZE));

    // swap back page reservation
    if (filename == nullptr) {
        if(num_swap_block_available < 1){
            return nullptr;
        }

        set_pte_bits(pcb.page_table[vpn], 0, 1, 0, 0, 0);

        ++pcb.next_vm_page;

        // reserve block
        unsigned int p = *open_swap_pages.begin();

        open_swap_pages.erase(p);

        pcb.pages_on_disk[vpn].block = p;

        swap_file[p].insert(current_pid);

        // eager block reservation count
        pcb.num_swap_reserved++;
        num_swap_block_available--;

        // insert into vp_page_map
    } else {
        std::string fname;

        if (!read_string_from_va(filename, fname)) {
            return nullptr;
        }

        auto &block_mapping = file_backed_pages[fname].block_to_file[block];

        // Shared file-backed page -> step 1
        if (block_mapping.ppn) {
            auto &ppn = block_mapping.ppn;

            page_map[ppn]->ptes.emplace(current_pid, vpn);
            block_mapping.ptes.emplace(current_pid, vpn);

            set_pte_bits(
                pcb.page_table[vpn], 
                ppn, 
                1, 1, 
                0, 
                0);
        }
        else {
            set_pte_bits(pcb.page_table[vpn], 0, 0, 0, 0, 0);

            block_mapping.ptes.emplace(current_pid, vpn);
        }

        pcb.pages_on_disk[vpn].file_backed = true;
        pcb.pages_on_disk[vpn].filename = fname;
        pcb.pages_on_disk[vpn].block = block;

        ++pcb.next_vm_page;
    }
    
    pcb.pages_on_disk[vpn].valid = true;

    // check_states();
    return reinterpret_cast<void*>(address);
}