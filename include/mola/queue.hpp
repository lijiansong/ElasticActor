/*
 Project MOLA Runtime System version 0.1

 Copyrigth (c) Institute of Computing Technology Chinese Academy of Sciences.

 All rights reserved.

 */

#ifndef _MOLA_QUEUE_H_
#define _MOLA_QUEUE_H_

#include <memory>
#include <atomic>
#include <limits>

#include "mola/spinlock.hpp"

namespace mola {
namespace utils {

template <class T> 
class ThreadSafeQueue {

private:
  typedef SpinLock lock_t;
  lock_t m_lock;

  struct node {
    T* value;
    std::unique_ptr<node> next;
    node(T* v, node *n) : value(v), next(n) {}
  };

  std::unique_ptr<node> m_head;
  node* m_tail;

  std::atomic<size_t> m_num;

public:
  ThreadSafeQueue() 
    : m_head(nullptr), m_tail(nullptr), m_num(0) {}
  ~ThreadSafeQueue() { /* TODO*/ }

  void enqueue(T* item) {
    std::unique_ptr<node> ptr(new node{item, nullptr});
    LockGuard lock(m_lock);
    
    if (m_num.load() == 0) {
      m_head = std::move(ptr);
      m_tail = m_head.get();
    } else  {
      m_tail->next = std::move(ptr);
      m_tail = m_tail->next.get();
    }

    m_num++;
  }

  T* dequeue() {
    if (m_num.load() == 0)
      return nullptr;
     
    LockGuard lock(m_lock);
    
    if (m_num.load() == 0)
      return nullptr;

    auto ptr = m_head->value;
    m_head = std::move(m_head->next);
    --m_num;
    if (m_num.load() == 0)
      m_tail = nullptr;
    return ptr;
  }

  bool empty() const { return m_num.load() == 0; }
  size_t size() const { return m_num.load(); }
};

/// Lock-free FIFO queue, from ConcurrentLinkedQueue in jdk6.0
template <typename T>
class ConcurrentLinkedQueue {
  static constexpr int HOPS = 1;
  
  class Node {
   public:
    Node(T* item) 
      : m_item(item), m_next(nullptr) {}
    ~Node() {}
    
    T* item() { return m_item.load(); }
    Node* next() { return m_next.load(); }

    void set_next(Node *next) { m_next.store(next); }
    void set_item(T* item) { m_item.store(item); }
    
    bool cas_item(T* o, T* n) {
      return m_item.compare_exchange_strong(o, n);
    }

    bool cas_next(Node* o, Node* n) {
      return m_next.compare_exchange_strong(o, n);
    }

   private:
    std::atomic<T*> m_item;
    std::atomic<Node*> m_next;
  };

  ConcurrentLinkedQueue(ConcurrentLinkedQueue&) = delete;
  ConcurrentLinkedQueue& operator = (ConcurrentLinkedQueue&) = delete;

public:
  ConcurrentLinkedQueue()
    : m_head(new Node(nullptr)) {
    m_tail = m_head.load();
  }
  ~ConcurrentLinkedQueue() { 
    /* --- TODO --- */ 
    delete m_head.load();
  }

  bool enqueue(T* item) {
    if (item == nullptr)
      return false;
    
    Node* node = new Node(item);

    for (;;) {
      Node* t = m_tail.load();
      Node* s = t->next();

      if (t == m_tail.load()) {
        if (s == nullptr) {
          if (t->cas_next(s, node)) {
            m_tail.compare_exchange_strong(t, node);
            return true;
          }
        } else {
          m_tail.compare_exchange_strong(t, s);
        }
      }
    }
    return false;
  }

  T* dequeue() {
    for (;;) {
      Node* h = m_head.load();
      Node* t = m_tail.load();
      Node* first = h->next();

      if (h == m_head.load()) {
        if (h == t) {
          if (first == nullptr) 
            return nullptr;
          else
            m_tail.compare_exchange_strong(t, first);
        } else if (m_head.compare_exchange_strong(h, first)) {
          T* item = first->item();
          if (item != nullptr) {
            first->set_item(nullptr);
            return item;
          }
        }
      }
    }
    return nullptr;
  }

  Node* first() {
    for (;;) {
      Node* h = m_head.load();
      Node* t = m_tail.load();
      Node* first = h->next();
      
      if (h == m_head.load()) {
        if (h == t) {
          if (first == nullptr)
            return nullptr;
          else
            m_tail.compare_exchange_strong(t, first);
        } else {
          if (first->item() != nullptr)
            return first;
          else
            m_head.compare_exchange_strong(h, first);
        }
      }
    }
  }

  bool empty() {
    return first() == nullptr;
  }

  int size() {
    int count = 0;
    for (Node* p = first(); p != nullptr; p = p->next()) {
      if (p->item() != nullptr) {
        if (++count == std::numeric_limits<int>::max())
          break;
      }
    }
    return count;
  }

private:
  std::atomic<Node*> m_head;
  std::atomic<Node*> m_tail;
};

} // namespace utils
} // namespace mola

#endif // _MOLA_QUEUE_H_
