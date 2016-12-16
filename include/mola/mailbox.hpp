/*
 Project MOLA Runtime System version 0.1

 Copyrigth (c) Institute of Computing Technology Chinese Academy of Sciences.

 All rights reserved.

*/

#ifndef _MOLA_MAILBOX_H_
#define _MOLA_MAILBOX_H_

#include <atomic>
#include <memory>

#include <stdint.h>

#include "mola/debug.hpp"

namespace mola {

namespace utils {

enum EnqueueStatus {
  MAILBOX_SUCCESS,
  MAILBOX_UNBLOCKED,
  MAILBOX_FAILURE,
};

template <class T>
struct SinglePriority {
  constexpr int operator () (const T*) { return 0; }
};

template <class T, int NP = 1, class GetPriority = SinglePriority<T> >
class Mailbox {
protected:

  enum {
    MAILBOX_DUMMY_EMPTY = 1,
    MAILBOX_DUMMY_BLOCKED = 2,
    MAILBOX_DUMMY_CLOSED = 3,
  };

  static T* dummy_empty() {
    return reinterpret_cast<T*>(MAILBOX_DUMMY_EMPTY);
  }

  static T* dummy_blocked() {
    return reinterpret_cast<T*>(MAILBOX_DUMMY_BLOCKED);
  }

  static T* dummy_closed() {
    return reinterpret_cast<T*>(MAILBOX_DUMMY_CLOSED);
  }

  bool is_dummy(T *e) {
    auto i = reinterpret_cast<intptr_t>(e);
    return i == MAILBOX_DUMMY_EMPTY ||
           i == MAILBOX_DUMMY_BLOCKED ||
           i == MAILBOX_DUMMY_CLOSED;
  }

public:

  Mailbox() : m_num_inque(0) {
    static_assert(NP > 0, "must have at least one priority!");
    for (auto &h : m_headers) h = nullptr;
    m_stack = dummy_blocked();
  }

  virtual ~Mailbox() {}

  EnqueueStatus enqueue(T* item) {
    MOLA_ASSERT(item != nullptr);
    
    for (;;) {
      T *e = m_stack.load();
      if (dummy_closed() == e)
        return MAILBOX_FAILURE;

      item->next = is_dummy(e) ? nullptr : e;
      if (m_stack.compare_exchange_strong(e, item)) {
        return (e == dummy_blocked()) ? MAILBOX_UNBLOCKED
                                      : MAILBOX_SUCCESS;
      }
    }
    // never reach here !!
  }

  T* dequeue() {
  /*
    if (m_header != nullptr) {
      auto e = m_header;
      m_header = e->next;
      return e;
    }*/
__begin:    
    if (m_num_inque > 0) {
      for (; m_lastp < NP; ++m_lastp) {
        auto& header = m_headers[m_lastp];
        if (header != nullptr) {
          auto e = header;
          header = e->next;
          --m_num_inque;
          return e;
        }
      }
      MOLA_ASSERT(0 && "private queues are empty but m_num_inque is not zero!!");
    }


    auto e = m_stack.load();
    /* MOLA_ASSERT(e != dummy_blocked()); */
    if (e == dummy_blocked()) return nullptr;

    while (e != dummy_empty()) {
      if (m_stack.compare_exchange_strong(e, dummy_empty())) {
        // copy the items from the shared stack
        // into the private queue.
/**
        while (e->next != nullptr) {
          auto next = e->next;
          e->next = m_header;
          m_header = e;
          e = next;
        }
        return e;*/
        
        while (e != nullptr) {
          auto& header = m_headers[m_get_priority(e)]; 
          auto next = e->next;
          e->next = header;
          header = e;
          e = next;

          ++m_num_inque;
        }
        
        m_lastp = 0;
        goto __begin;
      }
      // try to fetch again!!
      e = m_stack.load();
      if (e == dummy_blocked()) return nullptr;
    }
    return nullptr;
  }

  bool is_blocked() {
    return m_stack.load() == dummy_blocked();
  }

  bool try_block() {
    if (m_num_inque > 0) return false;
    
    auto e = m_stack.load();
    /* MOLA_ASSERT(e != dummy_blocked()); */

    while (e == dummy_empty()) {
      if (m_stack.compare_exchange_strong(e, dummy_blocked()))
        return true;
      e = m_stack.load();
    }
    return false;
  }

  void close() {
    auto e = m_stack.load();
    while (e != dummy_closed()) {
      if (m_stack.compare_exchange_strong(e, dummy_closed()))
        break;
      e = m_stack.load();
    }

    /* free all link nodes in private list */
/**
    while (m_header != nullptr) {
      auto cur = m_header;
      m_header = cur->next;
      delete cur;
    }*/

    if (m_num_inque > 0) {
      for (; m_lastp < NP; ++m_lastp) {
        auto& header = m_headers[m_lastp];
        if (header != nullptr) {
          auto cur = header;
          header = cur->next;
          delete cur;
        }
      }
    }
    
    /* free all link nodes in shared list */
    if (!is_dummy(e)) {
      while (e->next != nullptr) {
        auto cur = e;
        e = cur->next;
        delete e;
      }
    }
  }

private:
  std::atomic<T*> m_stack;
  
  T* m_headers[NP]; // only accessed by the owner's thread !!
  uint32_t m_num_inque; // messages number in private queues
  uint32_t m_lastp; // save the last priority level fetched
  GetPriority m_get_priority; // get the priority per-message 
};

} // namespace utils
} // namespace mola

#endif // _MOLA_MAILBOX_H_ 
