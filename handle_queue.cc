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

#include "handle_queue.h"
#include "print.h"

namespace impl {

HandleQueue<SpinLock> ready_queue{};
Atomic<uint64_t> started = 0;
Atomic<uint64_t> finished = 0;




Polling tlb_polling{};

void tlb_poll(uint64_t & core_gen) {
    if (core_gen < tlb_polling.generation.get()) {
        core_gen = tlb_polling.generation.get();
        asm volatile("invlpg (%0)":: "r"(tlb_polling.va): "memory");
        tlb_polling.ack.sub_fetch(1);
    }
}

[[noreturn]]
void event_loop(bool shutdown_when_done) {

  uint64_t core_generation = 0;
  while ((started == 0) || (finished != started)) {
    tlb_poll(core_generation);
    auto handle = impl::ready_queue.remove();
    tlb_poll(core_generation);
    if (handle) {
      handle.resume();
    } else {
      asm volatile("pause");
    }
  }
  if (shutdown_when_done) {
    KPRINT("event_loop: all done started=? finished=?, shutting down\n",
           Dec(started.get()), Dec(finished.get()));
    shutdown(true);
  }
  while (true) {
    asm volatile("hlt");
  }
}
} // namespace impl
