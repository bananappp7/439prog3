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

#pragma once

#include "debug.h"
#include "spin_lock.h"
#include "machine.h"
#include <coroutine>
#include <cstdint>

namespace impl {
class Segment {
public:
  static constexpr uint32_t LIMIT = 128;
  std::coroutine_handle<void> data[LIMIT];
  uint32_t head = 0;
  uint32_t count = 0;
  Segment *next = nullptr;

  [[gnu::noinline]]
  bool is_full() {
    return count == LIMIT;
  }

  [[gnu::noinline]]
  void add(const std::coroutine_handle<void> h) {
    ASSERT(count < LIMIT);
    data[(head + count) % LIMIT] = h;
    count += 1;
  }

  [[gnu::noinline]]
  inline std::coroutine_handle<void> remove() {
    if (count == 0) {
      return nullptr;
    }
    std::coroutine_handle<void> out = data[head];
    // data[head] = nullptr;
    head = (head + 1) % LIMIT;
    count -= 1;
    return out;
  }
};
} // namespace impl

template <typename LockType> class HandleQueue {
public:
  impl::Segment *head = nullptr;
  impl::Segment *tail = nullptr;
  uint32_t count = 0;
  LockType lock{};

  [[gnu::noinline]]
  void check() {
    if (head == nullptr) {
      ASSERT(tail == nullptr);
    }
    if (tail == nullptr) {
      ASSERT(head == nullptr);
    }
  }

public:
  HandleQueue() { check(); }

  // Racy by design: a best-effort hint for fast-path optimizations, not a
  // linearizable check.
  bool is_empty_hint() { return count == 0; }

  [[gnu::noinline]] [[nodiscard]]
  /* optimized for the "good path", (1) we can get the lock, (2) the queue is
     not full */
  bool try_add(std::coroutine_handle<void> h) {
    if (!lock.tryLock()) [[unlikely]] {
      return false;
    }
    check();
    if (tail == nullptr || tail->is_full()) [[unlikely]] {
      lock.unlock();
      auto emptySegment = new impl::Segment();
      if (!lock.tryLock()) {
        delete emptySegment;
        return false;
      }
      check();

      if (tail == nullptr) {
        head = emptySegment;
      } else {
        tail->next = emptySegment;
      }
      tail = emptySegment;
    }
    tail->add(h);
    count += 1;
    check();
    lock.unlock();
    return true;
  }

  void add(std::coroutine_handle<void> h) {
    while (!try_add(h)) {
      // spin
    }
  }

  [[gnu::noinline]]
  void add(HandleQueue<NoLock> &from) {
    from.check();
    if (from.head == nullptr) {
      return;
    }
    lock.lock();
    check();
    if (head == nullptr) {
      head = from.head;
      tail = from.tail;
      check();
    } else {
      ASSERT(tail != nullptr);
      tail->next = from.head;
      tail = from.tail;
      check();
    }
    count += from.count;
    check();
    lock.unlock();
    from.head = nullptr;
    from.tail = nullptr;
    from.count = 0;
    from.check();
  }

  [[gnu::noinline]]
  std::coroutine_handle<void> remove() {
    while (true) {
      lock.lock();
      check();
      auto h = head;
      if (h == nullptr) {
        check();
        lock.unlock();
        return nullptr;
      }

      auto out = head->remove();
      if (out) {
        count -= 1;
        check();
        lock.unlock();
        return out;
      }

      head = h->next;
      if (head == nullptr) {
        tail = nullptr;
      }
      check();
      lock.unlock();
      delete h;
    }
  }

  [[gnu::noinline]]
  HandleQueue<NoLock> remove_all() {
    lock.lock();
    check();
    HandleQueue<NoLock> out;
    out.head = head;
    out.tail = tail;
    out.count = count;
    head = nullptr;
    tail = nullptr;
    count = 0;
    check();
    lock.unlock();
    return out;
  }
};

namespace impl {
extern HandleQueue<SpinLock> ready_queue;
[[noreturn]] void event_loop(bool shutdown_when_done);
extern Atomic<uint64_t> started;
extern Atomic<uint64_t> finished;

struct Polling {
    uint64_t va;
    Atomic<uint64_t> generation;
    Atomic<uint64_t> ack;
};
extern Polling tlb_polling;
void tlb_poll();
uint64_t get_last_generation();
void set_last_generation(uint64_t last_gen);
} // namespace impl
