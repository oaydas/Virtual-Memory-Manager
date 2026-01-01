#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>
#include <cstring>

#include "pager_utils.h"

// file_backed_fault
int file_backed_fault(page_table_entry_t &pte, file_info_t &disk_info, unsigned int next_page,
    void* destination, unsigned int vpn) {

    // check_states();

    auto &fname = disk_info.filename;
    auto &block = disk_info.block;

    if (file_read(fname.data(), block, destination) == -1) return -1; 

    // Shared file-backed page -> step 2
    auto &block_mapping = file_backed_pages[fname].block_to_file[block];
    block_mapping.ppn = next_page;

    // Set all shared file back pages to same ppn
    size_t n = block_mapping.ptes.size();
    for (size_t i = 0; i < n; i++) {
        
        auto pair = block_mapping.ptes.front();
        block_mapping.ptes.pop();

        block_mapping.ptes.push(pair);

        auto &pte_temp = process_map[pair.first].page_table[pair.second];

        set_pte_bits(pte_temp, next_page, 1, 1, 0, 0);

        page_map[next_page]->ptes.push(pair);
    }

    // Update state of phys memory
    page_map[next_page]->file_backed    = 1;
    page_map[next_page]->block          = block;
    page_map[next_page]->filename       = fname;
    page_map[next_page]->ref            = 0;
    page_map[next_page]->dirty          = 0;

    // check_states();
    return 0;
}

// swap file reservation -> copy on write
void swap_block_reservation(int &block) {
    swap_file[block].erase(current_pid);

    // reserve block
    unsigned int p = *open_swap_pages.begin();

    open_swap_pages.erase(p);

    block = p;

    swap_file[block].insert(current_pid);
}

// swap back fault in phys memory
int swap_back_fault_in_memory(page_table_entry_t &pte, file_info_t &disk_info, unsigned int vpn) {
    // Make sure ref is set to correct value before eviction
    if (pte.ppage != 0) {
        int n = page_map[pte.ppage]->ptes.size();

        for (int i=0; i<n; i++){
            auto p = page_map[pte.ppage]->ptes.front();
            page_map[pte.ppage]->ptes.pop();

            auto &pte_swap = process_map[p.first].page_table[p.second];

            if (p.first == current_pid && p.second == vpn) {
                continue;
            }

            pte_swap.referenced = 1;

            page_map[pte.ppage]->ptes.push(p);
        }
    }

    unsigned int next_page = get_next_ppn();

    void* destination = BASE_ADDR + (static_cast<size_t>(next_page * VM_PAGESIZE));

    std::memcpy(
        destination, // destination
        BASE_ADDR + (static_cast<size_t>(pte.ppage * VM_PAGESIZE)), // Source is the zero pinned page
        VM_PAGESIZE
    );

    if (pte.ppage != 0) {
        if (page_map[pte.ppage]->ptes.size() == 1) {
            auto &t = page_map[pte.ppage]->ptes.front();

            // Change write bit of old page to 1 because not being shared anymore
            process_map[t.first].page_table[t.second].write_enable = 1;
        }
    }

    // assert(disk_info.valid);

    if (swap_file[disk_info.block].size() > 1) {
        swap_block_reservation(disk_info.block);
    }

    set_pte_bits(pte, next_page, 1, 1, 0, 0);

    // ensure its swap block is set correctly
    page_map[next_page]->block          = disk_info.block;
    page_map[next_page]->file_backed    = 0;
    page_map[next_page]->ref            = 0;
    page_map[next_page]->dirty          = 0;

    page_map[next_page]->ptes.emplace(current_pid, vpn);

    // check_states();
    return 0;
}

int swap_back_disk(page_table_entry_t &pte, file_info_t &disk_info, unsigned int next_page,
    void* destination, bool write_flag, unsigned int vpn) {

    // std::cout << "swap_back_disk" << std::endl; 
    // check_states();

    if (file_read(nullptr, disk_info.block, destination) == -1) return -1;

    // swap file block reservation
    // std::cout << "disk info block size: " << swap_file[disk_info.block].size() << std::endl;
    if (swap_file[disk_info.block].size() > 1) { 
        for (auto &pid : swap_file[disk_info.block]) {

            auto &pte_swap = process_map[pid].page_table[vpn];
            
            set_pte_bits(pte_swap, next_page, 1, 0, 0, static_cast<int>(write_flag));
            
            if (pid != current_pid) {
                page_map[next_page]->ptes.emplace(pid, vpn);
            }
        }
        // std::cout<< "LOOP END\n";
        // set page state
        page_map[next_page]->ref          = static_cast<int>(write_flag);
        page_map[next_page]->block        = disk_info.block;
        page_map[next_page]->file_backed  = 0;
        page_map[next_page]->dirty        = 0;

        if (write_flag) {
            copy_on_write_disk(pte, disk_info, next_page, destination, write_flag, vpn);
        }
    }
    else {
        set_pte_bits(pte, next_page, 1, 1, 0, 0);

        page_map[next_page]->ref          = 0;
        page_map[next_page]->block        = disk_info.block;
        page_map[next_page]->file_backed  = 0;
        page_map[next_page]->dirty        = 0;
    }     

    page_map[pte.ppage]->ptes.emplace(current_pid, vpn);

    // check_states();
    return 0;
}

void copy_on_write_disk(page_table_entry_t &pte, file_info_t &disk_info, unsigned int next_page,
    void* destination, bool write_flag, unsigned int vpn) {

    // std::cout << "copy_on_write_disk\n";
    
    int old_block = disk_info.block;

    // assert(disk_info.valid);
    swap_block_reservation(disk_info.block);

    // if one page left then write enabled
    if (swap_file[old_block].size() == 1) {
        auto last_pid = *swap_file[old_block].begin();

        auto &pte_swap = process_map[last_pid].page_table[vpn];
        set_pte_bits(pte_swap, next_page, 1, 1, 0, 1);
    }

    // Copy on write after read fault
    auto swap_next_page = get_next_ppn();
    void* swap_destination = BASE_ADDR + (static_cast<size_t>(swap_next_page * VM_PAGESIZE));

    std::memcpy(
        swap_destination, // destination
        destination, // Source 
        VM_PAGESIZE
    );

    set_pte_bits(pte, swap_next_page, 1, 1, 0, 0);

    // ensure its swap block is set correctly
    page_map[pte.ppage]->block          = disk_info.block;
    page_map[pte.ppage]->file_backed    = 0;
    page_map[pte.ppage]->ref            = 0;
    page_map[pte.ppage]->dirty          = 0;
} //copy_on_write_disk

void set_pte_bits(page_table_entry_t &pte,
                int ppage_,     
                int read_enable_,      
                int write_enable_,     
                int dirty_,            
                int referenced_) 
{
    pte.ppage           = ppage_ >= 0 ? ppage_ : pte.ppage;
    pte.read_enable     = read_enable_ >= 0 ? read_enable_ : pte.read_enable;
    pte.write_enable    = write_enable_ >= 0 ? write_enable_ : pte.write_enable;
    pte.dirty           = dirty_ >= 0 ? dirty_ : pte.dirty;
    pte.referenced      = referenced_ >= 0 ? referenced_ : pte.referenced;
} // set_pte_bits()

bool read_string_from_va(const char *filename_va, std::string &output) {

    auto raw_virtual_addr = reinterpret_cast<uintptr_t>(filename_va);
    size_t idx = 0; // size of string

    while(true) {
        char* phys_ptr = virtual_to_phys(reinterpret_cast<char*>(raw_virtual_addr));

        if(phys_ptr == nullptr) {
            return false;
        } 

        while(true) {
            // NOTE: How to know if there even exists a null terminator? 
            if(*phys_ptr == '\0') {
                return true;
            }                  
            
            output += *phys_ptr;
            ++phys_ptr;
            ++idx;

            // if its a new page, move va and translate it again
            if((raw_virtual_addr + idx) % VM_PAGESIZE == 0) {
                raw_virtual_addr = raw_virtual_addr + idx;
                break;
            }
        }
    }
} // read_string_from_va()

unsigned int evict() {
    std::shared_ptr<phys_page_t> page;

    // print_page_map();

    // Update reference and dirty bit of physical page from associated pte's
    update_reference_bits();

    // std::cout << "Clock size: " << clock_queue.size() << std::endl;

    // Run clock algorithm, update pte's associated with physical page
    size_t n = clock_queue.size();
    for(size_t i = 0; i < n+1; ++i){
        page = clock_queue.front();

        // assert(page->block != -1);

        clock_queue.pop();
        clock_queue.push(page);

        // std::cout << "\n Page ppn: " << page->ppn << '\n';
        if(page->ref == 0){
            
            // std::cout << "\n ref==0 \n";
            page->ref = 1;

            break;
        }      
        
        // std::cout << "\n ref==1 \n";
        
        page->ref = 0;

        // Update reference bits of PTEs to 0 if associated with physical page
        auto phys_page = page;

        size_t n = phys_page->ptes.size();
        for (size_t i = 0; i < n; i++) {
            auto pair = phys_page->ptes.front();
            phys_page->ptes.pop();
            phys_page->ptes.push(pair);

            auto &pte = process_map[pair.first].page_table[pair.second];

            pte.referenced = 0;
        }
    }

    // std::cout << "Eviciting " << page->ppn << '\n'; 

    // print_page_map();

    // WRITE BACK if dirty
    if (page->dirty != 0){
        if(page->file_backed != 0){
            // write back to file
            file_write(page->filename.data(), page->block, BASE_ADDR + (page->ppn * VM_PAGESIZE));

        } else {
            file_write(nullptr, page->block, BASE_ADDR + (page->ppn * VM_PAGESIZE));

        }
    }
    // Erase ppn mapping to block of filename after eviction
    if(page->file_backed != 0) file_backed_pages[page->filename].block_to_file[page->block].ppn = 0;

    // notify all ptes with this phys_page thats its a non-resident
    while (!page->ptes.empty()){
        auto &pte = page->ptes.front();

        page_table_entry_t &entry = process_map[pte.first].page_table[pte.second];

        set_pte_bits(entry, 0, 0, 0, 0, 0);

        page->ptes.pop();
    }
    
    // set page to not dirty
    page->ref = 0;
    page->dirty = 0;
    page->file_backed = 0;
    page->block = -1;
    page->filename = "";

    return page->ppn;
} // evict()

unsigned int get_next_ppn() {
    if(open_phys_pages.empty()){
        return evict();
    }         
    
    unsigned int page = *open_phys_pages.begin();
    open_phys_pages.erase(page);

    clock_queue.push(page_map[page]);

    return page;
} // get_next_ppn()

char* virtual_to_phys(const char* virtual_addr) {
    auto raw_virtual_addr = reinterpret_cast<uintptr_t>(virtual_addr);
    
    // NOT VALID VIRTUAL ADDRESS
    if (raw_virtual_addr < ARENA_BASE || raw_virtual_addr >= ARENA_BASE + VM_ARENA_SIZE) {
        return nullptr;
    }

    auto& pcb = process_map[current_pid];
    
    // get vpn and offset
    auto vpn = static_cast<unsigned int>((raw_virtual_addr - ARENA_BASE) / VM_PAGESIZE);
    auto offset = static_cast<unsigned>((raw_virtual_addr - ARENA_BASE) % VM_PAGESIZE);

    // get PTE
    page_table_entry_t &pte = pcb.page_table[vpn];

    if (!pte.read_enable) {
        if (vm_fault(virtual_addr, 0) == -1) return nullptr;
    } 

    pte.referenced = 1;

    char* phys_ptr = reinterpret_cast<char*>(BASE_ADDR + (pte.ppage * VM_PAGESIZE) + offset);
    return phys_ptr;
    
} // virtual_to_phys()

/*
 * DEBUGGING: Print out contents of page_map
 */
void print_page_map() {
    std::shared_ptr<phys_page_t> page;

    // print clock queue
    unsigned int n = clock_queue.size();

    if (n > 0) {
        std::cout << "FRONT OF CLOCK QUEUE: " << clock_queue.front()->ppn << '\n';
    }
    for(size_t i = 0; i < n; ++i){
        page = clock_queue.front();

        // assert(page->block != -1);

        clock_queue.pop();
        clock_queue.push(page);

        std::cout << "PPN: " << page->ppn << "\n"
                << "  ref: " << page->ref << "\n"
                << "  dirty: " << page->dirty << "\n"
                << "  file_backed: " << page->file_backed << "\n"
                << "  block: " << page->block << "\n"
                << "  filename: " << page->filename << "\n";
    }

    std::cout << "--------------------------------------\n";
    std::cout << "--------------------------------------\n";
    std::cout << "--------------------------------------\n";

    for (const auto &entry : page_map) {
        unsigned int ppn = entry.first;
        const auto &page = entry.second;
        std::cout << "PPN: " << ppn << "\n"
                << "  ref: " << page->ref << "\n"
                << "  dirty: " << page->dirty << "\n"
                << "  file_backed: " << page->file_backed << "\n"
                << "  block: " << page->block << "\n"
                << "  filename: " << page->filename << "\n"
                << "  ptes:\n";

        size_t qsize = page->ptes.size();
        for (size_t i = 0; i < qsize; ++i) {
            auto p = page->ptes.front();
            std::cout << "    (pid: " << p.first << ", vpn: " << p.second << ")\n";
            page->ptes.pop();
            page->ptes.push(p); // restore order
        }

        std::cout << "--------------------------------------\n";
    }

}

void update_reference_bits() {
    // UPDATE REFERENCED & DIRTY BITS TO REFLECT PTE's
    for (size_t p = 1; p < MAX_PHYS_PAGES; p++) {
        auto &phys_page = page_map[p];

        size_t n = phys_page->ptes.size();

        bool dirty = false;
        bool ref = false;

        for (size_t i = 0; i < n; i++) {
            auto pair = phys_page->ptes.front();
            phys_page->ptes.pop();
            phys_page->ptes.push(pair);

            auto &pte = process_map[pair.first].page_table[pair.second];

            if (pte.referenced) {
                phys_page->ref = 1;
                ref = true;
            }
            if (pte.dirty) {
                phys_page->dirty = 1;
                dirty = true;
            }
            if (dirty && ref) {
                break;
            }
        }
    }
}

void check_states() {
    // Compare state of ptes in each physicsl pahe align
        assert(open_phys_pages.find(0) == open_phys_pages.end());

        for(auto &[pid, pcb]: process_map){
            for (size_t i = 0; i < pcb.next_vm_page; ++i){

                auto &file_info = pcb.pages_on_disk[i];
                auto &pte = pcb.page_table[i];

                if (file_info.file_backed){
                    assert(file_info.filename != "");
                } else {
                    // swap backed file
                    if (swap_file[file_info.block].size() > 1){
                        // std::cout << "Pid: " << pid  << ", vpn: " << i << ", pte.ppage: " 
                        // << pte.ppage << ", file_info.block: " << file_info.block << std::endl;

                        assert(pte.write_enable == 0);
                    }

                    if (swap_file[file_info.block].size() == 1 && pte.ppage != 0 && file_info.valid && pte.read_enable) {
                        assert(pte.write_enable == 1);
                    }

                }

            }
        }

        // file_backed_pages
        for (auto &[filename, map]: file_backed_pages) {
            for (auto &[block, fcb]: map.block_to_file) {

                int n = fcb.ptes.size();
                if(fcb.ppn != 0){
                    assert(fcb.ptes.size() == page_map[fcb.ppn]->ptes.size());
                 }
                for (int i = 0; i < n; i++){
                    auto pair = fcb.ptes.front();
                    fcb.ptes.pop();
                    fcb.ptes.push(pair);
                    auto &pte = process_map[pair.first].page_table[pair.second];
                    if (fcb.ppn == 0){
                        assert(pte.read_enable == 0);
                    }
                    if(pte.read_enable){
                        assert(pte.ppage == fcb.ppn);
                    }
    
                }
            }
        }

        for (size_t p = 1; p < MAX_PHYS_PAGES; p++) {
            auto &phys_page = page_map[p];

            size_t n = phys_page->ptes.size();

            if ( n > 0 ){
                assert(phys_page->block != -1);
            }

            if(n == 0 && !phys_page->file_backed){
                assert(phys_page->block == -1);
            }
            

            if(!phys_page->file_backed){
                assert(phys_page->filename == "");
            }

            for (size_t i = 0; i < n; i++) {
                auto pair = phys_page->ptes.front();
                phys_page->ptes.pop();
                phys_page->ptes.push(pair);

                auto &file_info = process_map[pair.first].pages_on_disk[pair.second];
                auto &pte = process_map[pair.first].page_table[pair.second];

                assert(phys_page->file_backed == file_info.file_backed);
                assert(phys_page->ppn == pte.ppage);
                assert(process_map[pair.first].pages_on_disk[pair.second].block == phys_page->block);
                if(!file_info.file_backed){
                    assert(open_swap_pages.find(file_info.block) == open_swap_pages.end());
                }

                if (phys_page->file_backed && pte.read_enable){
                    assert(pte.ppage != 0);
                    assert(phys_page->block != -1);
                }
            }
    }
}

void print_file_backed_pages() {
    // for each pte a fileback data structue, make a entry fot the child as well
    for (auto &[filename, map]: file_backed_pages) {
        for (auto &[block, fcb]: map.block_to_file) {

            int n = fcb.ptes.size();
            for (int i = 0; i < n; i++){

                auto pair = fcb.ptes.front();
                fcb.ptes.pop();
                fcb.ptes.push(pair);

                std::cout << "(filename, block): " << filename << ", " << block
                    << " ---> (pid, vpn): " << pair.first << ", " << pair.second << '\n';
            }
        }
    }
}

