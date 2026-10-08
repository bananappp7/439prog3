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
#include "handle_queue.h"
#include <cstdint>

constexpr static uint64_t LOG_PAGE_SIZE = 12;
constexpr static uint64_t PAGE_SIZE = 1 << LOG_PAGE_SIZE;
constexpr static uint64_t STARTING_ADDRESS = 0x00000001;

SpinLock vmm_lock{};

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

int64_t unmap(VPN vpn);

uint64_t VA::check_canonical(uint64_t va) {
  int64_t sa = int64_t(va);
  ASSERT(((sa << 16) >> 16) == sa);
  return va;
}

void remove_mapping(uint64_t index) {
    delete mappings[index];
    for (uint64_t i = index; i + 1 < mappings_size; i ++) {
        mappings[i] = mappings[i + 1];
    }
    mappings_size -= 1;
    mappings[mappings_size] = nullptr;
}

void insert_mapping(uint64_t index, Mapping *mapping) {
    ASSERT(mappings_size <= (1 << 12));
    for (uint64_t i = mappings_size; i > index; i --) {
        mappings[i] = mappings[i - 1];
    }

    mappings[index] = mapping;
    mappings_size += 1;
}

/*
 * A simplified mmap implementation
 */

sync<void*> VMM::simplified_mmap(std::size_t length, StrongRef<Node> file,
                           uint64_t offset) {
  //MISSING();

  if (length == 0) {
    co_return reinterpret_cast<void*>(UINTPTR_MAX);
  }
  if (file == StrongRef<Node>{} && offset != 0) {
    co_return reinterpret_cast<void*>(UINTPTR_MAX);
  }
  if (offset & 0xFFF) {
    co_return reinterpret_cast<void*>(UINTPTR_MAX);
  }
  if (length >= 0x00007FFFFFFFF001) {
    co_return reinterpret_cast<void*>(UINTPTR_MAX);
  }
 
  vmm_lock.lock();

  uint64_t allocated_page_address = STARTING_ADDRESS;
  uint64_t allocate_page_length = length/PAGE_SIZE;
  uint64_t ind = mappings_size;
  if (length & 0xFFF) {
    allocate_page_length += 1;
  }

  //uint64_t mapped_length = (length + PAGE_SIZE - 1) / PAGE_SIZE;

  for (uint64_t i = 0; i < mappings_size; i++) {
    if (mappings[i] == nullptr) {
        break;
    }
    uint64_t map_start = mappings[i]->start/PAGE_SIZE;
    uint64_t map_pages = (mappings[i]->length + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t map_end = map_start + map_pages;

    if (allocated_page_address + allocate_page_length <= map_start) {
        ind = i;
        break;
    }
    allocated_page_address = map_end;
  }

  uint64_t page_count = (length/4096);
  if (length & 0xFFF) {
    page_count += 1;
  }

  uint64_t mapped_length = page_count * PAGE_SIZE;

  Mapping* to_add = new Mapping{allocated_page_address << LOG_PAGE_SIZE, mapped_length, file, offset};

  insert_mapping(ind, to_add);

  

  vmm_lock.unlock();

  

  /*for (uint64_t i = 0; i < page_count; i++) {



    VA va = VA((allocated_page_address +i)<< LOG_PAGE_SIZE);
    VPN vpn = VPN(va);

    PPN ppn = physMem.alloc();

    VA frame_va(ppn);
    char *buffer = (char *) frame_va;

    
    for (uint64_t j = 0; j < PAGE_SIZE; j++) {
        buffer[j] = 0;
    }
    if (file     != StrongRef<Node>{}) {
        uint64_t bytes_done = i * PAGE_SIZE;

        uint64_t bytes_left = length - bytes_done;

        uint64_t bytes_to_read = (bytes_left < PAGE_SIZE && 0)? bytes_left : PAGE_SIZE;

        uint64_t total_read = 0;

        while (total_read < bytes_to_read) {

            uint64_t n = co_await file->BlockIO::read(offset + bytes_done + total_read, bytes_to_read - total_read, buffer + total_read);
            if (n == 0) {
                break;
            }

            total_read += n;

        }
    }

    vmm_lock.lock();

    impl::map(vpn, ppn, false, true);

    vmm_lock.unlock();

  }*/
  


  co_return (void*)(allocated_page_address << LOG_PAGE_SIZE);
}





sync<int> VMM::munmap(void *addr, std::size_t length) { 
    //MISSING(); 

    uintptr_t unmap_start = reinterpret_cast<uintptr_t>(addr);
    uint64_t pages_to_unmap = length / 4096;
    if (length & 0xFFF) {
        pages_to_unmap += 1;
    }

    if (length == 0) {
        co_return -1;
    }
    if (unmap_start & 0xFFF) {
        co_return -1;
    }
    if (unmap_start >= 0x7FFFFFFFFFFF) {
        co_return -1;
    }
    uint64_t unmap_end = unmap_start + pages_to_unmap * PAGE_SIZE;
    
    vmm_lock.lock();
    uint64_t mapped_index = 0;
    bool found = false;
    for (uint64_t i = 0; i < (mappings_size); i++) {
        uint64_t map_start = mappings[i]->start;
        uint64_t map_pages = (mappings[i]->length + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t map_end = map_start + map_pages * PAGE_SIZE;
        if (map_start < unmap_end && unmap_start < map_end) {
            mapped_index = i;
            found = true;
            break;
        }
    }
    if (!found) {
        vmm_lock.unlock();
        co_return 0;
    }
    /*for (uint64_t i = (1 << 12) - 1; i > mapped_index; i--) {
        mappings[i] = mappings[i - 1];
    }*/
    /*for (uint64_t i = 0; i < pages_to_unmap; i++) {
        VA va = VA(unmap_start + i * PAGE_SIZE);
        VPN vpn = VPN(va);

        int64_t ippn = unmap(vpn);

        if (ippn != -1) {

            PPN ppn = PPN((uint64_t) ippn);
            physMem.PhysMem::free(ppn);

        }
    }*/
    for (uint64_t i = 0; i < mappings_size; i++) {
        Mapping* m = mappings[i];
        uint64_t map_start = m->start;
        uint64_t map_end = m->start + m->length;

        uint64_t overlap_start = (unmap_start > map_start) ? unmap_start : map_start;

        uint64_t overlap_end = (unmap_end < map_end) ? unmap_end : map_end;

        for (uint64_t va = overlap_start; va < overlap_end; va += PAGE_SIZE) {
            int64_t ippn = unmap(VPN(VA(va)));

            if (ippn != -1) {
                physMem.free(PPN((uint64_t)ippn));
            }
        }
    }
    uint64_t ind = mapped_index;
    while (ind < mappings_size) {
        Mapping* m = mappings[ind];

        uint64_t map_start = m->start;
        uint64_t map_pages = (m->length + PAGE_SIZE - 1) / PAGE_SIZE;
        uint64_t map_end = map_start + map_pages * PAGE_SIZE;

        if (map_start >= unmap_end)
            break;

        if (map_end <= unmap_start) {
            ind += 1;
            continue;
        }

        if (unmap_start <= map_start && unmap_end >= map_end) {
            remove_mapping(ind);
            continue;
        }

        if (unmap_start <= map_start && unmap_end < map_end) {
            m->start = unmap_end;
            m->length = map_end - unmap_end;
            m->offset = m->offset + unmap_end - map_start;
            ind += 1;
            continue;
        }

        if (unmap_start > map_start && unmap_end >= map_end) {
            m->length = unmap_start - map_start;
            ind += 1;
            continue;
        }

        if (unmap_start > map_start && unmap_end < map_end) {
            uint64_t old_start  = m->start;
            uint64_t old_end    = m->start + m->length;
            uint64_t old_offset = m->offset;

            m->length = unmap_start - map_start;

            Mapping *new_map = new Mapping{unmap_end, old_end - unmap_end, m->file, old_offset + unmap_end - old_start};
            insert_mapping(ind + 1, new_map);
            break;
        }
    }
    ASSERT(mapped_index >= 0);
    vmm_lock.unlock();
    co_return 0;
}

bool is_mapped(VPN vpn);

extern "C" [[gnu::force_align_arg_pointer]] void
pageFaultHandler(uintptr_t cr2, impl::PageFaultTrapFrame *trap_frame) {
  using namespace impl;

  /*KPRINT("page fault cr2=?, pc=? error_code=?\n", cr2, trap_frame->rip,
         trap_frame->error_code);*/

  uint64_t fault_address = cr2;
  uint64_t fault_page = fault_address & ~0xFFF;

  bool present = trap_frame->error_code & 1;

  if (!present) {
    for (uint64_t i = 0; i < mappings_size; i ++) {
        if (mappings[i]->start <= fault_address && mappings[i]->start + mappings[i]->length > fault_address) {
            VA va = VA(fault_page);
            VPN vpn = VPN(va);
            PPN ppn = physMem.alloc();

            VA frame_va(ppn);
            char *buffer = (char *) frame_va;

            
            for (uint64_t j = 0; j < PAGE_SIZE; j++) {
                buffer[j] = 0;
            }

            if (mappings[i]->file != StrongRef<Node>{}) {
                uint64_t page_offset = (fault_page - mappings[i]->start);
                uint64_t bytes_to_read = PAGE_SIZE;

                uint64_t total_read = 0;

                while (total_read < bytes_to_read) {

                    auto n = mappings[i]->file->BlockIO::read(page_offset + mappings[i]->offset + total_read, bytes_to_read - total_read, buffer + total_read);
                    

                    while (!n.promise->done) {
                        asm volatile("invlpg (%0)" :: "r"(tlb_polling.va) : "memory");
                        impl::tlb_poll();

                        
                        auto h = impl::ready_queue.remove();

                        if (h) {
                            h.resume();
                        } else {
                            asm volatile("pause");
                        }
                        //asm volatile("pause");
                    }
                    if (n.promise->value == 0) {
                        break;
                    }

                    total_read += (n.promise->value >= 0) ? n.promise->value : 0;

                }
            }
            vmm_lock.lock();

            if (is_mapped(vpn)) {
                vmm_lock.unlock();
                physMem.PhysMem::free(ppn);
                return;
            }

            impl::map(vpn, ppn, false, true);

            vmm_lock.unlock();
            return;
        }
    }
  }
  return;
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


uint64_t* get_exist_table(uint64_t &entry) {
    if (!(entry & 1)) {
        return nullptr;
    }


    PA next_pa{entry & 0x000FFFFFFFFFF000ULL};

    VA next_va{next_pa};

    return (uint64_t *)next_va.va();
}


int64_t unmap (VPN vpn) {
    uint64_t va = vpn.vpn() << 12;

    uint64_t pml4_i = (va >> 39) & 0x1FF;
    uint64_t pdpt_i = (va >> 30) & 0x1FF;
    uint64_t pd_i   = (va >> 21) & 0x1FF;
    uint64_t pt_i   = (va >> 12) & 0x1FF;

    uint64_t cr3 = get_cr3();

    PA pml4_pa = PA(cr3 & 0x000FFFFFFFFFF000ULL);
    VA pml4_va = VA(pml4_pa);
    uint64_t *pml4 = (uint64_t*)pml4_va.va();

    if (pml4 == nullptr) {
        return -1;
    }

    uint64_t *pdpt = get_exist_table(pml4[pml4_i]);

    if (pdpt == nullptr) {
        return -1;
    }

    uint64_t* pd = get_exist_table(pdpt[pdpt_i]);

    if (pd == nullptr) {
        return -1;
    }

    uint64_t *pt = get_exist_table(pd[pd_i]);

    if (pt == nullptr) {
        return -1;
    }
    if (!(pt[pt_i] & 1)) {
        return -1;
    }    

    int64_t to_return = ((pt[pt_i] & 0x000FFFFFFFFFF000ULL) >> 12);

    pt[pt_i] = 0;

    asm volatile("invlpg (%0)":: "r"(va): "memory");

    impl::tlb_polling.va = va;
    impl::tlb_polling.ack.set(Sys::core_count - 1);
    impl::tlb_polling.generation.add_fetch(1);
    //KPRINT("wait");
    while (impl::tlb_polling.ack.get() > 0) {
        asm volatile("pause");
    }

    //KPRINT("ack done");

    return to_return;
}

bool is_mapped(VPN vpn) {
    uint64_t va = vpn.vpn() << 12;

    uint64_t pml4_i = (va >> 39) & 0x1FF;
    uint64_t pdpt_i = (va >> 30) & 0x1FF;
    uint64_t pd_i   = (va >> 21) & 0x1FF;
    uint64_t pt_i   = (va >> 12) & 0x1FF;

    uint64_t cr3 = get_cr3();

    PA pml4_pa = PA(cr3 & 0x000FFFFFFFFFF000ULL);
    VA pml4_va = VA(pml4_pa);
    uint64_t *pml4 = (uint64_t*)pml4_va.va();

    if (!(pml4[pml4_i] & 1)) {
        return false;
    }

    uint64_t *pdpt = get_exist_table(pml4[pml4_i]);

    if (!(pdpt[pdpt_i] & 1)) {
        return false;
    }

     uint64_t* pd = get_exist_table(pdpt[pdpt_i]);

    if (!(pd[pd_i])) {
        return false;
    }

    uint64_t *pt = get_exist_table(pd[pd_i]);

    
    if (!(pt[pt_i] & 1)) {
        return false;
    }    
    return true;

}
