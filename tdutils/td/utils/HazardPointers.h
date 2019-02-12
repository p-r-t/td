//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"

#include <array>
#include <atomic>

namespace td {

template <class T, int MaxPointersN = 1>
class HazardPointers {
 public:
  explicit HazardPointers(size_t threads_n) : threads_(threads_n) {
    for (auto &data : threads_) {
      for (auto &ptr : data.hazard) {
        std::atomic_init(&ptr, static_cast<T *>(nullptr));
      }
    }
  }
  HazardPointers(const HazardPointers &other) = delete;
  HazardPointers &operator=(const HazardPointers &other) = delete;
  HazardPointers(HazardPointers &&other) = delete;
  HazardPointers &operator=(HazardPointers &&other) = delete;

  class Holder {
   public:
    T *protect(std::atomic<T *> &to_protect) {
      return do_protect(hazard_ptr_, to_protect);
    }
    Holder(const Holder &other) = delete;
    Holder &operator=(const Holder &other) = delete;
    Holder(Holder &&other) = default;  // TODO
    Holder &operator=(Holder &&other) = delete;
    ~Holder() {
      clear();
    }
    void clear() {
      hazard_ptr_.store(nullptr, std::memory_order_release);
    }

   private:
    friend class HazardPointers;
    explicit Holder(std::atomic<T *> &ptr) : hazard_ptr_(ptr) {
    }
    std::atomic<T *> &hazard_ptr_;
  };

  Holder get_holder(size_t thread_id, size_t pos) {
    return Holder(get_hazard_ptr(thread_id, pos));
  }

  void retire(size_t thread_id, T *ptr = nullptr) {
    CHECK(thread_id < threads_.size());
    auto &data = threads_[thread_id];
    if (ptr) {
      data.to_delete.push_back(unique_ptr<T>(ptr));
    }
    for (auto it = data.to_delete.begin(); it != data.to_delete.end();) {
      if (!is_protected(it->get())) {
        it->reset();
        it = data.to_delete.erase(it);
      } else {
        ++it;
      }
    }
  }

  // old inteface
  T *protect(size_t thread_id, size_t pos, std::atomic<T *> &ptr) {
    return do_protect(get_hazard_ptr(thread_id, pos), ptr);
  }
  void clear(size_t thread_id, size_t pos) {
    do_clear(get_hazard_ptr(thread_id, pos));
  }

  size_t to_delete_size_unsafe() const {
    size_t res = 0;
    for (auto &thread : threads_) {
      res += thread.to_delete.size();
    }
    return res;
  }

 private:
  struct ThreadData {
    std::array<std::atomic<T *>, MaxPointersN> hazard;
    char pad[TD_CONCURRENCY_PAD - sizeof(std::array<std::atomic<T *>, MaxPointersN>)];

    // stupid gc
    std::vector<unique_ptr<T>> to_delete;
    char pad2[TD_CONCURRENCY_PAD - sizeof(std::vector<unique_ptr<T>>)];
  };
  std::vector<ThreadData> threads_;
  char pad2[TD_CONCURRENCY_PAD - sizeof(std::vector<ThreadData>)];

  static T *do_protect(std::atomic<T *> &hazard_ptr, std::atomic<T *> &to_protect) {
    T *saved = nullptr;
    T *to_save;
    while ((to_save = to_protect.load()) != saved) {
      hazard_ptr.store(to_save);
      saved = to_save;
    }
    return saved;
  }

  static void do_clear(std::atomic<T *> &hazard_ptr) {
    hazard_ptr.store(nullptr, std::memory_order_release);
  }

  bool is_protected(T *ptr) {
    for (auto &thread : threads_) {
      for (auto &hazard_ptr : thread.hazard) {
        if (hazard_ptr.load() == ptr) {
          return true;
        }
      }
    }
    return false;
  }

  std::atomic<T *> &get_hazard_ptr(size_t thread_id, size_t pos) {
    CHECK(thread_id < threads_.size());
    return threads_[thread_id].hazard[pos];
  }
};

}  // namespace td
