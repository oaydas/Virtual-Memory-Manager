# Virtual Memory Manager (Pager)

A user-level virtual memory pager implemented in **C++**, responsible for managing per-process address spaces, handling page faults, and coordinating physical memory, swap-backed pages, and file-backed mappings.

This project models key operating system concepts including demand paging, copy-on-write, page replacement, and shared memory, with a strong emphasis on correctness, resource accounting, and realistic VM semantics.

---

## Overview

The pager manages a fixed-size virtual address arena for each process and transparently handles memory accesses by:

- Resolving page faults
- Loading pages from swap or files
- Evicting physical pages using a clock replacement algorithm
- Supporting shared file-backed pages
- Implementing copy-on-write semantics for swap-backed memory

The implementation closely mirrors the responsibilities of a real OS virtual memory subsystem while operating entirely in user space.

---

## Supported Operations

### Process Management
- `vm_create`: Fork-style address space creation
- `vm_switch`: Context switch between processes
- `vm_destroy`: Cleanup of all resources when a process exits

### Virtual Memory Operations
- `vm_map`: Map a new virtual page (swap-backed or file-backed)
- `vm_fault`: Handle read/write page faults
- Lazy page allocation and loading
- Automatic page eviction under memory pressure

---

## Virtual Address Space Model

- Each process owns a contiguous virtual arena starting at `VM_ARENA_BASEADDR`
- Pages are mapped sequentially from the lowest available virtual page number
- Each virtual page is tracked via:
  - Page table entry (PTE)
  - Disk metadata (swap block or file/block pair)
  - Validity, reference, and dirty state

Invalid or out-of-bounds accesses result in failure.

---

## Physical Memory Management

### Physical Pages
- Fixed number of physical pages
- Page 0 is a pinned, zero-filled page that is never evicted
- Remaining pages are dynamically allocated and reclaimed

### Replacement Policy
- Clock (second-chance) algorithm
- Each physical page tracks:
  - Reference bit
  - Dirty bit
  - Owning processes and virtual pages

Pages are evicted only when necessary and written back to disk if dirty.

---

## Swap-Backed Pages

- Swap-backed pages are private by default
- Swap blocks are reserved eagerly at `vm_map` time
- Pages are initially zero-filled
- Copy-on-write is enforced on fork:
  - Parent and child share swap blocks
  - Write access is revoked
  - A private copy is created on write fault

Swap space accounting is strictly enforced to prevent overcommitment.

---

## File-Backed Pages

- Pages may be backed by a file and block offset
- Multiple processes may share the same file-backed page
- Shared physical pages are used whenever possible
- Write access is enabled only when safe
- File-backed metadata tracks all referencing PTEs

File-backed mappings are lazily loaded on demand.

---

## Page Fault Handling

The pager distinguishes between several fault cases:

- Non-resident page faults
- Write faults on copy-on-write pages
- File-backed faults
- Swap-backed faults with or without residency

On each fault:
1. A physical page is allocated or evicted
2. Data is loaded from swap or file if necessary
3. Page table entries are updated
4. Reference and dirty bits are set appropriately

Invalid faults result in failure.

---

## Concurrency and Correctness Guarantees

- Precise tracking of shared physical pages
- Accurate reference counting for swap blocks
- Safe reclamation of physical and swap resources
- Deterministic cleanup on process exit
- No memory leaks or dangling mappings

State transitions are carefully ordered to avoid inconsistent mappings or lost data.

---

## Key Implementation Details

- Clock queue for eviction management
- Explicit separation of:
  - Page table state
  - Disk-backed metadata
  - Physical page ownership
- Deferred loading of pages (demand paging)
- Eager reservation of swap space
- Zero-page optimization via pinned physical page

---

## Technologies Used

- **C++17**
- STL containers (`unordered_map`, `queue`, `vector`, `unordered_set`)
- Low-level pointer arithmetic
- Custom pager and disk abstractions
- OS-inspired memory management patterns

---

## Summary

This project implements a fully functional virtual memory pager with realistic operating system behavior, including demand paging, copy-on-write, page replacement, and shared memory.

It demonstrates deep understanding of virtual memory systems, careful state management, and robust handling of complex edge cases commonly encountered in real-world operating systems.
