#pragma once 

#include <string>
#include "pager.h" 

/***************************************************************************************************
 *                                           Pager Utils                                           *
 ***************************************************************************************************/
 
/*
 * Updates the bits of the page table entry  
 */
void set_pte_bits(page_table_entry_t &pte,
                    int ppage_,     
                    int read_enable_,      
                    int write_enable_,     
                    int dirty_,            
                    int referenced_);
//

/*
 * Takes in a pointer to the file name, copies the string 
 * 
 * If the filename does not reside completely in the valid portion of the arena 
 * return a null pointer
 */
bool read_string_from_va(const char* filename_va, std::string& output);

/*
* Performs the Clock Eviction LRU Algorithm 
* Returns the PPN of the page to be replaced
*/
unsigned int evict();

/*
* Wrapper function to get the next available physical page
* 
* If the next open physical page is larger than max number of phys pages
* Return the evicted page 
*/
unsigned int get_next_ppn();

/*
 * Translates a virtual address into a physical address
 * 
 * Returns nullptr if it's an invalid virtual address             
 */
char* virtual_to_phys(const char* virtual_addr);

/*
 * DEBUGGING: Print out contents of page_map
 */
void print_page_map();

/*
 * Update reference bits in page_map to align with PTE's
 */
void update_reference_bits();

// assert states of physical pages and ptes match
void check_states();

/*
 * Print data in our file_backed_pages data structure
 */
void print_file_backed_pages();

// file_backed_fault
int file_backed_fault(page_table_entry_t &pte, file_info_t &disk_info, unsigned int next_page,
    void* destination, unsigned int vpn);

// swap_block_reservation
void swap_block_reservation(int & block);

// swap_back_fault_in_memory
int swap_back_fault_in_memory(page_table_entry_t &pte, file_info_t &disk_info, unsigned int vpn);

// swap_back_disk
int swap_back_disk(page_table_entry_t &pte, file_info_t &disk_info, unsigned int next_page,
    void* destination, bool write_flag, unsigned int vpn);

// copy_on_write_disk
void copy_on_write_disk(page_table_entry_t &pte, file_info_t &disk_info, unsigned int next_page,
    void* destination, bool write_flag, unsigned int vpn);
