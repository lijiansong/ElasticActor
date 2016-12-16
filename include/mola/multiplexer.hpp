#ifndef _MOLA_MULTIPLEXER_H_
#define _MOLA_MULTIPLEXER_H_

/* epoll API is for linux platform only */
#if ( defined(linux) || defined(__linux__) ||\
      defined(__gnu_linux__) || defined(__linux) )
# define USE_EPOLL 1
#endif

#ifdef USE_EPOLL
# include <sys/epoll.h>
#else
# include <poll.h>
#endif // USE_EPOLL

#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "mola/debug.hpp"

namespace mola {

class EventHandler;

enum Operation {
  ERROR = -1,
  READ = 1,
  WRITE = 2,
  RDWR = 3,
};

class Multiplexer {
  using MultiplexerPtr = std::unique_ptr<Multiplexer>;

  static std::vector<MultiplexerPtr> backends;
  static int io_thread_num;
  static thread_local Multiplexer* poller;

public:
  static const int TIME_INFINITY = -1;

  static Multiplexer* instance(int i = -1);
  static void initialize(int thr_num = 1);

  static Multiplexer* backend() { return poller; }
  static void set_backend(Multiplexer* p) { poller = p; }

  Multiplexer(int id): m_id(id) {}

  virtual int poll(/*int timeout = TIME_INFINITY*/) = 0;
  virtual int add(Operation op, EventHandler *handler) = 0;
  virtual int del(Operation op, EventHandler *handler) = 0;

  int get_id() const { return m_id; }

private:
  int m_id;
};

#ifdef USE_EPOLL

class Epoller : public Multiplexer {
  friend class Multiplexer;
protected:
  Epoller(int num, int max = 512)
    : Multiplexer(num)
    , m_max(max), m_num(0) {
    m_epfd = ::epoll_create(m_max);
    MOLA_ASSERT(m_epfd > 0);
  }

  virtual ~Epoller() {}

public:
  int poll() override;

  int add(Operation op, EventHandler *handler) override; 
  int del(Operation op, EventHandler *handler) override; 

private:
  int epoll_ctl(Operation op, EventHandler *handler, int epop);

  int m_max;
  int m_epfd;
  std::atomic<int> m_num;

  std::mutex m_lock;
  std::condition_variable m_cond;
};

#else // USE_EPOLL

class Poller : public Multiplexer {
  friend class Multiplexer;

  static const int MAX_WAITING_TIME = 100; // ms
protected:
  Poller(int num, int max = 255)
    : Multiplexer(num)
    , m_cap(max), m_size(0)
    , m_fds(new struct pollfd [max])
    , m_evs(new EventHandler* [max]) { }
  
  virtual ~Poller() {
    delete [] m_fds;
    delete [] m_evs;
  }

public:
  int add(Operation op, EventHandler *handler) override ;
  int del(Operation op, EventHandler *handler) override ;
  int poll() override ;

private:
  int m_cap;
  int m_size;
  struct pollfd *m_fds;
  EventHandler **m_evs;
  
  std::mutex m_lock;
  std::condition_variable m_cond;
};

#endif // USE_EPOLL

}

#endif // _MOLA_MULTIPLEXER_H_
