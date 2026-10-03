/* Copyright (C) 2025 Ahmed Gheith and contributors.
 *
 * Use restricted to classroom projects.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "vmm.h"

#include "ext2.h"
#include "idt.h"
#include "machine.h"
#include "physmem.h"
#include "print.h"
#include "shared.h"
#include "system_main.h"
#include <cstdint>

constexpr static uint64_t LOG_PAGE_SIZE = 12;
constexpr static uint64_t PAGE_SIZE = 1 << LOG_PAGE_SIZE;

namespace impl {

struct PageFaultTrapFrame {
  std::uint64_t rax;
  std::uint64_t rbx;
  std::uint64_t rcx;
  std::uint64_t rdx;
  std::uint64_t rdi;
  std::uint64_t rsi;
  std::uint64_t rbp;
  std::uint64_t r8;
  std::uint64_t r9;
  std::uint64_t r10;
  std::uint64_t r11;
  std::uint64_t r12;
  std::uint64_t r13;
  std::uint64_t r14;
  std::uint64_t r15;
  std::uint64_t error_code;
  std::uint64_t rip;
  std::uint64_t cs;
  std::uint64_t rflags;
  std::uint64_t rsp;
  std::uint64_t ss;
};

} // namespace impl

typedef struct Mapping {
    uint64_t start;
    std::size_t length;
    StrongRef<Node> file;
    uint64_t offset;
} Mapping;

Mapping *mappings[1 << 12];
std::size_t mappings_size = 0;



uint64_t VA::check_canonical(uint64_t va) {
  int64_t sa = int64_t(va);
  ASSERT(((sa << 16) >> 16) == sa);
  return va;
}

/*
 * A simplified mmap implementation
 */

sync<void*> VMM::simplified_mmap(std::size_t length, StrongRef<Node> file,
                           uint64_t offset) {
  //MISSING();
  //VA address_start(0x00000001 << LOG_PAGE_SIZE);

  /*mappings[mappings_size]->start = 0x00000001 << LOG_PAGE_SIZE;  
  mappings[mappings_size]->length = length;
  mappings[mappings_size]->file = file;
  mappings[mappings_size]->offset = offset;*/

  mappings[mappings_size] = new Mapping{0x00000001 << LOG_PAGE_SIZE, length, file, offset};
  mappings_size += 1;

  VA va = VA(0x00000001 << LOG_PAGE_SIZE);
  VPN vpn = VPN(va);

  PPN ppn = physMem.alloc();

  VA frame_va(ppn);
  char *buffer = (char *) frame_va;

  if (file == StrongRef<Node>{}) {
    for (uint64_t i = 0; i < PAGE_SIZE; i++) {
        buffer[i] = 0;
    }
  }

  impl::map(vpn, ppn, false, false);

  co_return (void*)(0x00000001 << LOG_PAGE_SIZE);
}

sync<int> VMM::munmap(void *addr, std::size_t length) { 
    //MISSING(); 
    co_return 0;
}

extern "C" [[gnu::force_align_arg_pointer]] void
pageFaultHandler(uintptr_t cr2, impl::PageFaultTrapFrame *trap_frame) {
  using namespace impl;

  KPRINT("page fault cr2=?, pc=? error_code=?\n", cr2, trap_frame->rip,
         trap_frame->error_code);

  //MISSING(); 
  
}

void VMM::init_system() { IDT::trap(14, uintptr_t(pageFaultHandler_), 0); }

uint64_t* get_next_table(uint64_t &entry, bool user, bool write) {
    if (!(entry & 1)) {
        PPN new_ppn = physMem.alloc();
        VA new_va = VA(new_ppn);
        uint64_t *table = (uint64_t*) new_va.va();
        for (uint64_t i = 0; i < 512; i++) {
            table[i] = 0;
        }
        entry = (new_ppn.ppn() << LOG_FRAME_SIZE) | 0x1 | (write? 0x2 : 0) | (user ? 0x4 : 0);

        return table;
    }

    if (write) {
        entry = entry | 0x2;
    }

    if (user) {
        entry = entry | 0x4;
    }

    PA next_pa{entry & 0x000FFFFFFFFFF000ULL};

    VA next_va{next_pa};

    return (uint64_t *)next_va.va();
}

void impl::map(VPN vpn, PPN ppn, bool user, bool write) {

    uint64_t va = vpn.vpn() << 12;

    uint64_t pml4_i = (va >> 39) & 0x1FF;
    uint64_t pdpt_i = (va >> 30) & 0x1FF;
    uint64_t pd_i   = (va >> 21) & 0x1FF;
    uint64_t pt_i   = (va >> 12) & 0x1FF;

    uint64_t cr3 = get_cr3();

    PA pml4_pa = PA(cr3 & 0x000FFFFFFFFFF000ULL);
    VA pml4_va = VA(pml4_pa);
    uint64_t *pml4 = (uint64_t*)pml4_va.va();


    uint64_t *pdpt = get_next_table(pml4[pml4_i], user, write);


    uint64_t* pd = get_next_table(pdpt[pdpt_i], user, write);


    uint64_t *pt = get_next_table(pd[pd_i], user, write);

    pt[pt_i] = (ppn.ppn() << LOG_FRAME_SIZE) | 0x1 | (write ? 0x2 : 0) | (user ? 0x4 : 0);
    

}

