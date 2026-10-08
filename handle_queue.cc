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

void tlb_poll() {
  uint64_t last_gen = tlb_polling.generation.get();
  if (get_last_generation() < last_gen) {
    uint64_t va = tlb_polling.va;
    asm volatile("invlpg (%0)":: "r"(va): "memory");
    set_last_generation(last_gen);
    tlb_polling.ack.sub_fetch(1);
  }
}

void set_last_generation(uint64_t gen) {
  wrgsbase(gen);
}

uint64_t get_last_generation() {
  return rdgsbase();
}

[[noreturn]]
void event_loop(bool shutdown_when_done) {

  while ((started == 0) || (finished != started)) {
    tlb_poll();
    auto handle = impl::ready_queue.remove();
    tlb_poll();
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
