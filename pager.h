#pragma once 

#include <string>
#include <queue> 
#include <unordered_map>
#include <unordered_set>
#include <memory>

#include "vm_pager.h"
#include "vm_arena.h"


/*************************
 * Global pager variables *
 *************************
 *
 * NUM_VPAGES: Size of our physical page and limited amount os swap_blocks
 *
 * Common conversions for arithmetic:
 *  >> arena_base
 *  >> base
 *  >> max_physical_pages
 *  >> max_swap_blocks
 * 
 * Variables to keep track of free swap back file pages
 *  >> num_swap_pgs 
 *  >> next_open_swap_pg
 *  >> next_open_phys_pg
 *
 * Last but not least: current_pid
 */

//
static constexpr unsigned NUM_VPAGES = VM_ARENA_SIZE / VM_PAGESIZE;

extern uintptr_t ARENA_BASE;
extern unsigned char* BASE_ADDR;

extern unsigned int MAX_PHYS_PAGES;

extern pid_t current_pid;

extern int num_swap_block_available;

/*
 * phys_page_t:
 * 
 * Data structure for the clock replacement algorithm
 * Assists in making page evictions
 */
struct phys_page_t {
    unsigned int ppn = 0;                       // physical page offset
    int ref = 0;                                // referenced bit
    int dirty = 0;                              // dirty bit
    int file_backed = 0;                        // file_backed bit
    int block = -1;                             // block -- if -1 its invalid
    std::string filename = "";                       // filename
    std::queue<std::pair<pid_t, unsigned int>> ptes;     // list of pid, vpn for each place this phys_page was pointed to
};

/*
 * file_info_t:
 * 
 * This struct as well as the read_fault map allows us to 
 * defer the work of reading in the file until the first read
 */
struct file_info_t {
    bool file_backed = false;   // is this pte file backed
    int block = 0;             // the block of the file or the swap file that this maps to -- -1 if not set (assert)
    bool valid = false;
    std::string filename;       // name of the file
};

/*
 * Pager Control Block (pcb_t):
 * 
 * This will map each pid_t to the respective pcb.
 */
struct pcb_t {
    page_table_entry_t  page_table[NUM_VPAGES];
    file_info_t  pages_on_disk [NUM_VPAGES];          // this is necessary in the situation that the page is evicted and replaced and is in the memory
    unsigned int next_vm_page = 0;
    int num_swap_reserved;
};

/* 
 * File Control Block (fcb_t):
 * 
 * Will be stored in a map to all us to map filenames
 * to respective physical addresses if its a resident
 * Multiple page table entries to the same file
 * 
 */
struct fcb_t {
    unsigned int ppn = 0;
    std::queue<std::pair<pid_t, unsigned int>> ptes;
};

struct block_map {
    std::unordered_map<unsigned int, fcb_t> block_to_file;
};

/* 
 * {<key> = PPN | <value> = physical_page_t} 
 * This will map physical page numbers to any useful information.
 * This will allow us to evict stuff effectively
 * Map to update clock algo
 */
extern std::unordered_map<unsigned int, std::shared_ptr<phys_page_t>> page_map;

/*
 * Queue for the clock eviction algorithm
 */
extern std::queue<std::shared_ptr<phys_page_t>> clock_queue;

/*
 * This will map each pid_t to the respective pcb.  
 */
extern std::unordered_map<pid_t, pcb_t> process_map;

/*
 * Map each filename to their respective fcb
 */
extern std::unordered_map<std::string, block_map> file_backed_pages;

/*
 * Keep track of open physical pages
 */
extern std::unordered_set<unsigned int> open_phys_pages;

/*
 * Keep track of open swap pages
 */
extern std::unordered_set<unsigned int> open_swap_pages;

/*
 * Keep track of how many swap_back pages pointing at a block in swap file
 */
extern std::vector<std::unordered_set<pid_t>> swap_file;