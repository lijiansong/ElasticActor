#ifndef _MOLA_SPINLOCK_H_
#define _MOLA_SPINLOCK_H_

#include <atomic>
#include <deque>

namespace mola {
namespace utils {

class SpinLock {
public:
  SpinLock() : m_lock(ATOMIC_FLAG_INIT) {}

  void lock() {
    while (m_lock.test_and_set(std::memory_order_acquire));
  }

  bool try_lock(int times = 1) {
    for (int i = 0; i < times; ++i) { 
      if (!m_lock.test_and_set(std::memory_order_acquire))
        return true;
    }
    return false;
  }

  void unlock() {
    m_lock.clear(std::memory_order_release);
  }

private:
  std::atomic_flag m_lock;
};

class LockGuard {
  SpinLock *m_lock;
public:
  LockGuard(const LockGuard&) = delete;

  LockGuard(SpinLock& lock)
    : m_lock(&lock) { 
    m_lock->lock();
  }

  ~LockGuard() {
    if (m_lock != nullptr)
      m_lock->unlock();
  }

  void unlock() {
    if (m_lock != nullptr) {
      m_lock->unlock();
      m_lock = nullptr;
    }
  }

  void operator = (const LockGuard&) = delete;
};


class TryLockGuard {
  SpinLock *m_lock;

public:
  TryLockGuard(const TryLockGuard&) = delete;

  TryLockGuard(SpinLock& lock, int times = 1)
    : m_lock(&lock) {
    if (!m_lock->try_lock(times))
      m_lock = nullptr;
  }

  ~TryLockGuard() {
    if (m_lock != nullptr)
      m_lock->unlock();
  }

  bool is_locked() const {
    return (m_lock != nullptr);
  }

  void operator = (const TryLockGuard&) = delete;
};


template <typename T>
class ThreadSafeWrapper : public T {
public:
  void lock() {
    m_lock.lock();
  }

  void unlock() {
    m_lock.unlock();
  }

  SpinLock& get() { return m_lock; }

private:
  SpinLock m_lock;
};

class AsyncLock {
  typedef std::function<void()> callback_t;

protected:
  bool m_acquired;
  SpinLock m_lock;
  std::deque<callback_t> m_wait_que;

public:
  AsyncLock() : m_acquired(false) {}

  void acquire(callback_t callback) {
    LockGuard lk(m_lock);
    if (!m_acquired) {
      m_acquired = true;
      return callback();
    }

    auto lock = this;
    m_wait_que.emplace_back([=](){
      this->acquire(callback);
    });
  }

  void release() {
    LockGuard lk(m_lock);
    m_acquired = false;

    if (m_wait_que.size() > 0) {
      auto callback = m_wait_que.front();
      m_wait_que.pop_front();
      lk.unlock(); // TO AVOID DEADLOCK !!
      return callback();
    }
  }

};

}
}

#endif // _MOLA_SPINLOCK_H_
