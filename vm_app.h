/*
 * vm_app.h
 *
 * Public routines for clients of the pager
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

#define _VM_APP_H_

#ifdef _VM_PAGER_H_
#error Do not include both vm_app.h and vm_pager.h in the same program
#endif

#include <sys/types.h>
#include "vm_arena.h"

/*
 * vm_map
 *
 * Ask for the lowest invalid virtual page in the process's arena to be
 * declared valid.  On success, vm_map returns the lowest address of the
 * new virtual page.  vm_map returns nullptr if the arena is full.
 *
 * If filename is nullptr, block is ignored, and the new virtual page is
 * backed by the swap file, is initialized to all zeroes, and is private
 * (i.e., not shared with any other virtual page).  In this case, vm_map
 * returns nullptr if the swap file is out of space.
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
 * vm_yield
 *
 * Ask operating system to yield the CPU to another process.
 * The infrastructure's scheduler is non-preemptive, so a process runs until
 * it calls vm_yield() or exits.
 */
void vm_yield();
