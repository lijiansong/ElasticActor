/*
 Project MOLA Runtime System version 0.1

 Copyrigth (c) Institute of Computing Technology Chinese Academy of Sciences.

 All rights reserved.

 */
 
#ifndef _MOLA_SCHDULER_H_
#define _MOLA_SCHDULER_H_

#include <cstdlib>
#include <atomic>
#include <thread>
#include <chrono>
#include <queue>
#include <map>
#include <mutex>
#include <memory>
#include <condition_variable>

#include "mola/debug.hpp"
#include "mola/actor_id.hpp"

namespace mola {

class WorkItem;
class Actor;
class Node;
class Worker;

class AbsolateWorker {
  static constexpr uint32_t ALL_KEYS = 0xFFFFFFFFUL;

  static constexpr uint32_t RNGMOD = ((1ULL << 32) - 5);
  static constexpr uint32_t RNGMUL = 69070UL;

  using WorkerPtr = std::unique_ptr<AbsolateWorker>;

  static int worker_num;
  static std::vector<WorkerPtr> workers;

  static thread_local uint32_t seed;

  using NodePtr = std::shared_ptr<Node>;
  using NodeSet = std::vector<NodePtr>;

  template <class T>
  struct Cache {
    Cache() : valid(true) {}

    void invalid() { valid.store(false); }

    T get(uint32_t key) {
      T value;
      
      if (valid.load()) {
        auto i = cache.find(key);
        if (i != cache.end())
          return i->second;
      } else {
        cache.clear();
        valid.store(true);
      }

      return value;
    }

    void insert(uint32_t key, T& value) {
      cache.emplace(key, value);
    }

    void remove(uint32_t key) {
      auto i = cache.find(key);
      if (i != cache.end()) {
        cache.erase(i);
      }
    }

    std::map<uint32_t, T> cache;
    std::atomic<bool> valid;
  };

public:
  static thread_local AbsolateWorker *current_worker;
  static void set_current_worker(AbsolateWorker *worker) {
    current_worker = worker;
  }

  template <class T = AbsolateWorker>
  static T* get_current_worker() {
    return dynamic_cast<T*>(current_worker);
  }

  template <class T = Worker>
  static T* get_worker(int id = -1) {
    if (id < 0)
      return get_current_worker<Worker>();
    
    MOLA_ASSERT(id < worker_num);
    return dynamic_cast<T*>(workers[id].get());
  }

  static const std::vector<WorkerPtr>& get_workers() {
    return workers;
  }
  
  // call this before using the random per-thread!!
  static void srand(uint32_t s) {
    s %= RNGMOD; s += (s == 0);
    seed = s;
  }

  static uint32_t random() {
    seed = (uint32_t)((RNGMUL * (uint64_t)seed) % RNGMOD);
    return seed;
  }

  static int get_worker_num() {
    return worker_num;
  }

  int get_id() const { return m_id; }

  // NOTE: It seems better to put the cache into per-actor!!
  Actor* find_cached_actor(uint32_t key); 
  void cache_an_actor(uint32_t key, Actor* actor);

  NodePtr find_cached_node(uint32_t key) {
    return m_node_cache.get(key);
  }

  void cache_a_node(uint32_t key, NodePtr& node) {
    m_node_cache.insert(key, node);
  }

  void invalid_cached_node(uint32_t key = ALL_KEYS) {
    if (key == ALL_KEYS)
      m_node_cache.invalid();
    else
      m_node_cache.remove(key);
  }

  NodeSet find_cached_ns(uint32_t key) {
    return m_ns_cache.get(key);
  }

  void cache_ns(uint32_t key, NodeSet& ns) {
    m_ns_cache.insert(key, ns);
  }

  void invalid_cached_ns(uint32_t key = ALL_KEYS) {
    if (key == ALL_KEYS)
      m_ns_cache.invalid();
    else
      m_ns_cache.remove(key);
  }

  void start() {
    auto self = this;
    m_this_thread = std::thread{ [self] {
      AbsolateWorker::srand(self->m_id);
      self->set_current_worker(self);
      self->run();
    }};
  }

  void stop() {
    m_this_thread.join();
  }

  virtual ~AbsolateWorker() {
   stop();
  }

protected:
  AbsolateWorker() : m_id(worker_num++) {
    workers.emplace_back(this);
  }

  virtual void run() = 0;

  int m_id;
  std::thread m_this_thread;
  
  std::map<uint32_t, Actor*> m_actor_cache;
  Cache<NodePtr> m_node_cache;
  Cache<NodeSet> m_ns_cache;
};

class WorkItem {
public:
  enum ExecStatus {
    RESUMABLE = 0, // yield
    BLOCKED, // waiting for invoking
    WAIT,    // waiting for responses
    EXIT,    // exit
    SUSPEND, // suspended
    SWITCH, //  to switch to child context
  };

  virtual ~WorkItem() {}

  const ActorID& get_id() const { return m_id; }
  ActorID& get_id() { return m_id; }

  virtual ExecStatus execute(Worker* worker) = 0;

protected:
  WorkItem(ActorID id) 
    : m_id(id) { /* TODO */ }

private:
  ActorID m_id;
};

class AsyncTask : public WorkItem {
  using task_t = std::function<ExecStatus()>;
public:
  AsyncTask(int id, task_t task) 
    : WorkItem(-1), m_id(id), m_task(task) {}
  virtual ~AsyncTask() {}

  virtual ExecStatus execute(Worker* worker) {
    return m_task();
  }

  int get_id() const { return m_id; }

protected: 
  task_t m_task;  
  int m_id;
};

class WorkerGroup;

class SchedStrategy {
public:
  virtual void enqueue(AbsolateWorker *worker, WorkItem *job) = 0;
  virtual void global_enqueue(WorkItem *job) = 0;
  virtual WorkItem* dequeue(Worker *worker) = 0;

};

class Worker : public AbsolateWorker {
public:
  Worker(SchedStrategy *strategy) 
    : m_strategy(strategy), m_current(nullptr) { 
  }

  void enqueue(WorkItem *job) {
    MOLA_ASSERT(job != nullptr);
    m_strategy->enqueue(this, job);
  }

  void before_resume(void *ctx);

  WorkItem *get_current() { return m_current; }

private:
  void run() override {
    MOLA_LOG_TRACE("worker with ID " << m_id);

    /* schedule loop */
    for (;;) {
      m_current = m_strategy->dequeue(this);
      MOLA_ASSERT(m_current != nullptr);
      // MOLA_LOG_TRACE("resume actor instance " << m_current->get_id().u64());
      //std::cout<<"the scheduled actor is "<<std::hex<<m_current->get_id().u64()<<std::endl;
      auto ret = m_current->execute(this);
      
      if (ret == WorkItem::RESUMABLE) {
        // TODO: like a yield operation, let others go.
        //       so it is better to push it into the gloabl queue.
        m_strategy->enqueue(this, m_current);
      }
      
      if (ret == WorkItem::EXIT) {
        delete m_current;
        m_current = nullptr;
      }

      // TODO : Add handlers for other cases!!
    }
  }

  SchedStrategy* m_strategy;
  WorkItem* m_current;
};


class Timer : public AbsolateWorker {
  using task_t = std::function<void()>;
  using item_t = std::pair<std::chrono::steady_clock::time_point, task_t>;
  using duration_t = std::chrono::steady_clock::duration;

  class Compare {
  public:
    bool operator () (const item_t& i0, const item_t& i1) const {
      return i0.first < i1.first;
    }
  };

protected:
  void run() override {
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;) {
      if (m_queue.empty())
        m_condvar.wait(lock);

      auto& item = m_queue.top();
      if (item.first <= std::chrono::steady_clock::now() ||
          m_condvar.wait_until(lock, item.first) == std::cv_status::timeout) {
        item.second(); // do the callback if time out
        m_queue.pop(); // delete the task
      } 
      
      // top of the m_queue may be changed, we should fetch it again!!
      continue;
    }
  }

public:

  void add(int64_t timeout, task_t task) {
    return add(std::chrono::milliseconds(timeout), task);
  }
  
  void add(std::chrono::milliseconds timeout, task_t task) {
    if (timeout.count() == 0) {
      task();
      return;
    }

    std::unique_lock<std::mutex> lock(m_mutex);

    auto when = std::chrono::steady_clock::now() + 
                std::chrono::duration_cast<duration_t>(timeout);

    m_queue.emplace(when, task);
    if (m_queue.top().first == when) {
      m_condvar.notify_one();
    }
  }

  std::mutex m_mutex;
  std::condition_variable m_condvar;
  std::priority_queue<item_t, std::vector<item_t>, Compare> m_queue;
};

class Multiplexer;
class AsyncIoWorker : public AbsolateWorker {
public:
  AsyncIoWorker(Multiplexer *backend) 
    : m_backend(backend) { 
    MOLA_ASSERT(m_backend != nullptr);
  }

  virtual ~AsyncIoWorker() { }

  void run() override;
  
private:
  Multiplexer* m_backend; 
};

class DelayMessagePool;
class DelayMessageWorker : public AbsolateWorker {
public:
  DelayMessageWorker(DelayMessagePool* pool)
    : m_pool(pool) {
    MOLA_ASSERT(m_pool != nullptr);
  }
  virtual ~DelayMessageWorker() {}

  void run() override;

private:
  DelayMessagePool* m_pool;  
};

class WorkerGroup {
  
public:
  static WorkerGroup* instance() {
    return group.get();
  }

  template <typename T>
  static void add_timer(T timeout, std::function<void()> task) {
    group->m_timer->add(timeout, task);
  }

  static WorkerGroup* initialize(unsigned num = 0, unsigned io = 0,
                                 SchedStrategy* ss = nullptr);

  virtual ~WorkerGroup() {
    for (int i = 0; i < m_workers_num; ++i) {
      m_pool[i]->stop();
    }
  }

  unsigned get_workers_num() const {
    return m_workers_num;
  }

  SchedStrategy* get_strategy() { return m_strategy.get(); }

  void enqueue(WorkItem *job) {
    auto worker = AbsolateWorker::get_current_worker();
    return m_strategy->enqueue(worker, job);
  }

  void enqueue(AbsolateWorker *worker, WorkItem *job) {
    return m_strategy->enqueue(worker, job);
  }

  void before_resume(void *data_ptr);

  void start_all() {
    if (m_started) return ;
    for (unsigned i = 0; i < m_workers_num; ++i) {
      m_pool[i]->start(); // start the worker thread
    }
#ifdef ENABLE_TIMER
    m_timer = new Timer;
    m_timer->start(); // start the timer thread
#endif // ENABLE_TIMER
  }

private:

  static std::unique_ptr<WorkerGroup> group;

  WorkerGroup(unsigned num, SchedStrategy* strategy)
    : m_started(false)
    , m_workers_num(num)
    , m_strategy(strategy) {}
  WorkerGroup(SchedStrategy* strategy)
    : m_started(false)
    , m_workers_num(std::thread::hardware_concurrency())
    , m_strategy(strategy) {}


  void init_workers() {
    for (unsigned i = 0; i < m_workers_num; ++i) {
      m_pool.push_back(new Worker(m_strategy.get()));
    }
  }

  void start_io_workers(unsigned io);

  bool m_started;
  unsigned m_workers_num;
  Timer* m_timer;
  std::vector<Worker*> m_pool;
  std::unique_ptr<SchedStrategy> m_strategy;
};

}; // @namespace mola

#endif // _MOLA_SCHDULER_H_
