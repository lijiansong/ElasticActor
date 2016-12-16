/*
 Project MOLA Runtime System version 0.1

 Copyrigth (c) Institute of Computing Technology Chinese Academy of Sciences.

 All rights reserved.

 */

#ifndef _MOLA_WORKSTEAL_H_
#define _MOLA_WORKSTEAL_H_

#include <deque>
#include <mutex>
#include <condition_variable>

#include "mola/scheduler.hpp"
#include "mola/queue.hpp"

// modified by ZHAO Peng, the factor is somehow so big that work stealing seldom happens. 
// !! However, the factor is still improper, it should correlate with total messages number in all actors
//#define WAKEUP_FACTOR(N) (N << 3)
#define WAKEUP_FACTOR(N) (1)
// #define WAKEUP_FACTOR(N) ( (N+3)/4 << ((N+7)/8) )

namespace mola {

class WorkStealStrategy : public SchedStrategy {
public:
  WorkStealStrategy(uint32_t num, uint32_t helpers)
    : m_workers_num(num)
    , m_total_thr_num(num + helpers)
    , m_sleep_num(0)
    , m_total_jobs(0) 
    , m_factor(WAKEUP_FACTOR(num)) {}


public:
  WorkItem *dequeue(Worker *worker) override {
    MOLA_ASSERT(worker != nullptr);
    auto id = worker->get_id();

    auto task = dequeue(id);
    if (task != nullptr) 
      goto __exit;

    while (1) {
      if (m_total_jobs.load() > 0) {
        if ((task = global_dequeue(worker)) != nullptr)
          goto __exit;
        
        if (m_total_thr_num == 1) continue;

        for (int i = 0; i < m_total_thr_num-1; ++i) {
          auto victim = worker->random() % m_total_thr_num;
          while (victim == id)
            victim = worker->random() % m_total_thr_num;

          task = steal(id, victim);
          if (task != nullptr) goto __exit;
        }
      }

      auto busy = m_workers_num - m_sleep_num.load();
      if (m_total_jobs.load() > (m_factor * busy))
        continue;

      // TODO : sleep for a while before another try ..
      std::unique_lock<std::mutex> lock(m_mutex);
      if (++m_sleep_num < m_workers_num) {
        //std::cout<<"thread "<<id<<" sleeps."<<std::endl;
        m_condvar.wait(lock);
      } else if (m_total_jobs.load() == 0) {
        //std::cout<<"last thread "<<id<<" sleeps.\n"<<std::endl;
        m_condvar.wait_for(lock, std::chrono::milliseconds(10));
      }
      //std::cout<<"thread "<<id<<" is waked up."<<std::endl;
      m_sleep_num--;
    }

__exit:
    MOLA_ASSERT(task != nullptr);
    m_total_jobs--;
    return task; 
  }

protected:
  virtual void enqueue(int id, WorkItem *task) = 0;
  virtual WorkItem *dequeue(int id) = 0;
  virtual WorkItem *steal(int id, int victim_id) = 0;
  virtual WorkItem *global_dequeue(Worker *worker) = 0;

  void wakeup() {
    m_total_jobs++;
    /* If the current workload is more than the busy workers 
     * just try to notify a sleep one to do the tasks */
    auto busy = m_workers_num - m_sleep_num.load();
    if (m_workers_num > busy && 
        m_total_jobs.load() > (m_factor * busy)) {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_condvar.notify_one();
    }

  }

  uint32_t m_workers_num;
  uint32_t m_total_thr_num;

private:
  std::atomic<uint32_t> m_total_jobs;
  std::atomic<uint32_t> m_sleep_num;
  std::mutex m_mutex;
  std::condition_variable m_condvar;
  uint32_t m_factor;
};

// A simple work-stealing strategy using the spinlock queues..
template <class queue_type = utils::ConcurrentLinkedQueue<WorkItem> >
class SimpleStrategy : public WorkStealStrategy {
public:
  using queue_ptr = std::unique_ptr<queue_type>;

  SimpleStrategy(uint32_t num, uint32_t helpers) 
    : WorkStealStrategy(num, helpers) {
    for (uint32_t i = 0; i <= m_total_thr_num; ++i) {
      m_queues.emplace_back(new queue_type());
    }
  }

  void enqueue(AbsolateWorker *worker, WorkItem *job) override {
    if (worker != nullptr)
      enqueue(worker->get_id(), job);
    else
      enqueue(m_total_thr_num, job);
  }

  void global_enqueue(WorkItem *job) override {
    enqueue(m_total_thr_num, job);
  }

protected:

  void enqueue(int id, WorkItem *job) override {
    m_queues[id]->enqueue(job);
    wakeup();
  }

  WorkItem* dequeue(int id) override {
    return m_queues[id]->dequeue();
  }

  WorkItem* steal(int id, int victim) override {
    return dequeue(victim);
  }

  WorkItem* global_dequeue(Worker *worker) override {
    return dequeue(m_total_thr_num);
  }

private:
  std::vector<queue_ptr> m_queues; // task queues
};

#if defined(ENABLE_LOCKFREE_GLOBAL_QUEUE)
template<int N = 1024>
class LockFreeStrategy : public WorkStealStrategy {

  struct LockFreeQue {
    LockFreeQue() 
      : head(0), tail(0) {}

    std::atomic<uint32_t> head;
    std::atomic<uint32_t> tail;
    WorkItem *content[N];
  };

public:
  LockFreeStrategy(uint32_t num, uint32_t helpers) 
    : WorkStealStrategy(num, helpers) {
    m_queues = new LockFreeQue[num + helpers];
  }

  virtual ~LockFreeStrategy() {
    delete [] m_queues;
  }
  
  void global_enqueue(WorkItem *task) override {
    m_global_queue.enqueue(task);
    wakeup();
  }

  void enqueue(AbsolateWorker *worker, WorkItem *task) override {    
    if (worker != nullptr) {
      enqueue(worker->get_id(), task);
    } else {
      global_enqueue(task);
    }
  }

protected:
  void enqueue(int id, WorkItem *task) override {
    __enqueue(m_queues[id], task);
    wakeup();
  }

  WorkItem* dequeue(int id) override {
    auto task = __dequeue(m_queues[id]);
    return task;
  }

  WorkItem* steal(int me, int victim) override {
    return __steal(m_queues[me], m_queues[victim]);
  }
 
  WorkItem* global_dequeue(Worker* worker) override {
    return m_global_queue.dequeue();
  }

private:   
  void __global_enqueue(size_t size, WorkItem** tasks) {
    for (int i = 0; i < size; ++i) {
      m_global_queue.enqueue(tasks[i]);
    }
  }

  /* fetch an item from the private queue */
  WorkItem* __dequeue(LockFreeQue& q) {
    WorkItem *task = nullptr;
    uint32_t head, tail;

    while (1) {
      head = q.head.load();
      tail = q.tail.load(std::memory_order_relaxed);

      if (head == tail) return nullptr;

      task = q.content[head % N];
      if (q.head.compare_exchange_strong(head, head + 1))
        break;
    }

    return task;
  }

  bool __enqueue(LockFreeQue& q, WorkItem* task, uint32_t head, uint32_t tail) {
    WorkItem *temp[N / 2 + 1];
    uint32_t n = (tail - head) / 2;

    for (uint32_t i = 0; i < n; ++i) {
      temp[i] = q.content[(head + i) % N];
    }

    if (!q.head.compare_exchange_strong(head, head + n))
      return false;

    temp[n] = task;

    /* link the temp[] to gloabl queue */
    __global_enqueue(n + 1, temp);

    return true;
  }

  void __enqueue(LockFreeQue& q, WorkItem* task) {
    uint32_t tail, head;

    while (1) {
      head = q.head.load();
      tail = q.tail.load(std::memory_order_relaxed);

      if (tail - head < N) {
        q.content[tail % N] = task;
        q.tail.store(tail + 1);
        break;
      }

      // if the private queue if full,
      // try to move half tasks to the global queue.
      if (__enqueue(q, task, head, tail))
        break;
    }
  }

  uint32_t __grab(LockFreeQue& q, WorkItem **temp) {
    uint32_t tail, head, n;

    while (1) {
      head = q.head.load();
      tail = q.tail.load();
      n = tail - head;
      n = n - n / 2;
      if (n == 0) break;

      if (n > N /2) continue;

      for (uint32_t i = 0; i < n; ++i)
        temp[i] = q.content[(head + i) % N];
      
      if (q.head.compare_exchange_strong(head, head + n))
        break;
    }
    return n;
  }

  WorkItem* __steal(LockFreeQue& q, LockFreeQue& victim) {
    WorkItem *task;
    WorkItem *temp[N / 2];
    uint32_t tail, head, n;

    n = __grab(victim, temp);
    if (n == 0) return nullptr;

    task = temp[--n];
    if (n == 0) return task;

    head = q.head.load();
    tail = q.tail.load(std::memory_order_relaxed);

    MOLA_ASSERT(tail - head + n < N);

    for (uint32_t i = 0; i < n; i++, tail++) 
      q.content[tail % N] = temp[i];
    q.tail.store(tail);
   
    return task;
  }


  LockFreeQue* m_queues;
  utils::ConcurrentLinkedQueue<WorkItem> m_global_queue;
};

#else

template<int N = 1024>
class LockFreeStrategy : public WorkStealStrategy {

  struct LockFreeQue {
    LockFreeQue() 
      : head(0), tail(0) {}

    std::atomic<uint32_t> head;
    std::atomic<uint32_t> tail;
    WorkItem *content[N];
  };

public:
  LockFreeStrategy(uint32_t num, uint32_t helpers) 
    : WorkStealStrategy(num, helpers)
    , m_global_queue_num(0) {
    m_queues = new LockFreeQue[num + helpers];
  }

  virtual ~LockFreeStrategy() {
    delete [] m_queues;
  }
  
  void global_enqueue(WorkItem *task) override {
    {
      utils::LockGuard lock(m_lock);
      m_global_queue.emplace_back(task);
    }

    m_global_queue_num++;
    wakeup();
  }

  void enqueue(AbsolateWorker *worker, WorkItem *task) override {    
    if (worker != nullptr) {
      enqueue(worker->get_id(), task);
    } else {
      global_enqueue(task);
    }
  }

protected:
  void enqueue(int id, WorkItem *task) override {
    __enqueue(m_queues[id], task);
    wakeup();
  }

  WorkItem* dequeue(int id) override {
    auto task = __dequeue(m_queues[id]);
    return task;
  }

  WorkItem* steal(int me, int victim) override {
    return __steal(m_queues[me], m_queues[victim]);
  }
 
  WorkItem* global_dequeue(Worker* worker) override {
    if (m_global_queue_num.load() == 0)
      return nullptr;

    /* FIXME: not neccessary to fetch more than one task
     *        if the total ready tasks number is small */
    WorkItem *task;
    utils::LockGuard lock(m_lock);
    if (m_global_queue.size() == 0) 
      return nullptr;
    
    m_global_queue_num--;
    task = m_global_queue.front();
    m_global_queue.pop_front();
    return task;
  }

private:   
  void __global_enqueue(size_t size, WorkItem** tasks) {
    utils::LockGuard lock(m_lock);
    for (int i = 0; i < size; ++i) {
      m_global_queue.emplace_back(tasks[i]);
    }
    m_global_queue_num += size;
  }

  /* fetch an item from the private queue */
  WorkItem* __dequeue(LockFreeQue& q) {
    WorkItem *task = nullptr;
    uint32_t head, tail;

    while (1) {
      head = q.head.load();
      tail = q.tail.load(std::memory_order_relaxed);

      if (head == tail) return nullptr;

      task = q.content[head % N];
      if (q.head.compare_exchange_strong(head, head + 1))
        break;
    }

    return task;
  }

  bool __enqueue(LockFreeQue& q, WorkItem* task, uint32_t head, uint32_t tail) {
    WorkItem *temp[N / 2 + 1];
    uint32_t n = (tail - head) / 2;

    for (uint32_t i = 0; i < n; ++i) {
      temp[i] = q.content[(head + i) % N];
    }

    if (!q.head.compare_exchange_strong(head, head + n))
      return false;

    temp[n] = task;

    /* link the temp[] to gloabl queue */
    __global_enqueue(n + 1, temp);

    return true;
  }

  void __enqueue(LockFreeQue& q, WorkItem* task) {
    uint32_t tail, head;

    while (1) {
      head = q.head.load();
      tail = q.tail.load(std::memory_order_relaxed);

      if (tail - head < N) {
        q.content[tail % N] = task;
        q.tail.store(tail + 1);
        break;
      }

      // if the private queue if full,
      // try to move half tasks to the global queue.
      if (__enqueue(q, task, head, tail))
        break;
    }
  }

  uint32_t __grab(LockFreeQue& q, WorkItem **temp) {
    uint32_t tail, head, n;

    while (1) {
      head = q.head.load();
      tail = q.tail.load();
      n = tail - head;
      n = n - n / 2;
      if (n == 0) break;

      if (n > N /2) continue;

      for (uint32_t i = 0; i < n; ++i)
        temp[i] = q.content[(head + i) % N];
      
      if (q.head.compare_exchange_strong(head, head + n))
        break;
    }
    return n;
  }

  WorkItem* __steal(LockFreeQue& q, LockFreeQue& victim) {
    WorkItem *task;
    WorkItem *temp[N / 2];
    uint32_t tail, head, n;

    n = __grab(victim, temp);
    if (n == 0) return nullptr;

    task = temp[--n];
    if (n == 0) return task;

    head = q.head.load();
    tail = q.tail.load(std::memory_order_relaxed);

    MOLA_ASSERT(tail - head + n < N);

    for (uint32_t i = 0; i < n; i++, tail++) 
      q.content[tail % N] = temp[i];
    q.tail.store(tail);
   
    return task;
  }


  LockFreeQue* m_queues;
  std::deque<WorkItem*> m_global_queue;
  std::atomic<size_t> m_global_queue_num;
  utils::SpinLock m_lock;
};

#endif

} // namespace mola

#undef WAKEUP_FACTOR

#endif // _MOLA_WORKSTEAL_H_
