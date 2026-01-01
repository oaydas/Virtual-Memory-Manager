/*
 * vm_pager.h
 *
 * Header file for the pager
 */

#pragma once

#if !defined(__cplusplus) || __cplusplus < 202000L
#error Please configure your compiler to use C++20 or higher
#endif

#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ < 12
#error Please use g++ version 12 or higher
#endif

#if defined(__clang__) && defined(__clang_major__) && __clang_major__ < 16
#error Please use clang++ version 16 or higher
#endif

#define _VM_PAGER_H_

#ifdef _VM_APP_H_
#error Do not include both vm_app.h and vm_pager.h in the same program
#endif

#include <sys/types.h>
#include "vm_arena.h"

/*
 * vm_init
 *
 * Called when the pager starts.  It should set up any internal data structures
 * needed by the pager.
 *
 * vm_init is passed the number of physical memory pages and the number
 * of blocks in the swap file.
 */
void vm_init(unsigned int memory_pages, unsigned int swap_blocks);

/*
 * vm_create
 * Called when a parent process (parent_pid) creates a new process (child_pid).
 * vm_create should cause the child's arena to have the same mappings and data
 * as the parent's arena.  If the parent process is not being managed by the
 * pager, vm_create should consider the arena to be empty.
 * Note that the new process is not run until it is switched to via vm_switch.
 * Returns 0 on success, -1 on failure.
 */
int vm_create(pid_t parent_pid, pid_t child_pid);

/*
 * vm_switch
 *
 * Called when the kernel is switching to a new process, with process
 * identifier "pid".
 */
void vm_switch(pid_t pid);

/*
 * vm_fault
 *
 * Called when current process has a fault at virtual address addr.  write_flag
 * is true if the access that caused the fault is a write.
 * Returns 0 on success, -1 on failure.
 */
int vm_fault(const void* addr, bool write_flag);

/*
 * vm_destroy
 *
 * Called when current process exits.  This gives the pager a chance to
 * clean up any resources used by the process.
 */
void vm_destroy();

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
void* vm_map(const char* filename, unsigned int block);

/*
 * file_read
 *
 * Read page from the specified file and block into buf.
 * If filename is nullptr, the data is read from the swap file.  buf should
 * be an address in vm_physmem.
 * Returns 0 on success; -1 on failure.
 */
int file_read(const char* filename, unsigned int block, void* buf);

/*
 * file_write
 *
 * Write page from buf to the specified file and block.
 * If filename is nullptr, the data is written to the swap file.  buf should
 * be an address in vm_physmem.
 * Returns 0 on success; -1 on failure.
 */
int file_write(const char* filename, unsigned int block, const void* buf);

extern void* const vm_physmem;

/*
 * **************************************
 * * Definition of page table structure *
 * **************************************
 */

/*
 * Format of a page table entry (PTE).  A page table is a fixed-size array of
 * PTEs (one entry per page in the arena).
 *
 * read_enable=0 ==> loads to this virtual page will fault
 * write_enable=0 ==> stores to this virtual page will fault
 * The MMU does not support write-only pages {read_enable=0, write_enable=1}
 *
 * ppage refers to the physical page for this virtual page (unused if
 * both read_enable and write_enable are 0)
 * dirty will be set when page is successfully written
 * referenced will be set when page is successfully read or written
 */
struct page_table_entry_t {
    unsigned int ppage : 28;            /* bit 0-27 */
    unsigned int read_enable : 1;       /* bit 28 */
    unsigned int write_enable : 1;      /* bit 29 */
    unsigned int dirty : 1;             /* bit 30 */
    unsigned int referenced : 1;        /* bit 31 */
};

/*
 * MMU's page table base register.  This variable is defined by the
 * infrastructure, but its contents are controlled completely by the pager.
 * It should point to the PTE for the first page in the current process's arena.
 */
extern page_table_entry_t* page_table_base_register;
