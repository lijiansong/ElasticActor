#include <map>

#include "mola/multiplexer.hpp"
#include "mola/event_handler.hpp"

using namespace mola;

std::vector<Multiplexer::MultiplexerPtr> Multiplexer::backends;

int Multiplexer::io_thread_num;

thread_local Multiplexer* Multiplexer::poller = nullptr;

void Multiplexer::initialize(int np) {
  MOLA_ASSERT(np > 0);

  io_thread_num = np;

  for (int i = 0; i < np; ++i) {
#ifdef USE_EPOLL
    auto poller = new Epoller(i);
#else
    auto poller = new Poller(i);
#endif // USE_EPOLL
    
    backends.emplace_back(poller);
  }
}

Multiplexer* Multiplexer::instance(int i) { 
  if (i == -1)
    i = ::random() % io_thread_num;
  
  MOLA_ASSERT(i >= 0 && i < io_thread_num);

  return backends[i].get(); 
}



#ifdef USE_EPOLL

int Epoller::add(Operation op, EventHandler* handler) {
  if (handler->add()) { 
    m_num++;
    m_cond.notify_one();
  }
  return epoll_ctl(op, handler, EPOLL_CTL_ADD);
}

int Epoller::del(Operation op, EventHandler* handler) {
  if (handler->del()) {
    m_num--;
    return epoll_ctl(op, handler, EPOLL_CTL_DEL);
  }
  return 0;
}

int Epoller::poll() {
  /* check if there is any waiting operations */
  while (m_num.load() == 0) {
    std::unique_lock<std::mutex> lk(m_lock);
    m_cond.wait(lk);
  }

  std::map<EventHandler*, uint32_t> activities;

  struct epoll_event events[m_max];
  int ready, mode = 0;

  for (;;) {
    int timeout = activities.size() > 0 ? 0 : TIME_INFINITY; 
    ready = ::epoll_wait(m_epfd, events, m_max, timeout);

    for (int i = 0; i < ready; ++i) {
      struct epoll_event &ev = events[i];

      EventHandler *handler = 
        reinterpret_cast<EventHandler*>(ev.data.ptr);
    
      if (ev.events & (EPOLLERR | EPOLLHUP)) {
        handler->handle_event(Operation::ERROR);
        continue;
      }

      activities[handler] |= ev.events;    
    } // for 
    
    decltype(activities) temp;
    for (auto active : activities) {
      // for write operations
      if (active.second & EPOLLOUT) {
        if (!active.first->handle_event(Operation::WRITE)) {
          active.second &= (~EPOLLOUT);
        }
      }
      // for read operations
      if (active.second & (EPOLLIN | EPOLLRDHUP)) {
        if (!active.first->handle_event(Operation::READ)) {
          active.second &= (~(EPOLLIN | EPOLLRDHUP));
        }
      }

      if (active.second != 0) {
        temp.emplace(active);
      } 
    }

    activities = std::move(temp);

  }

  return ready;
}

int Epoller::epoll_ctl(Operation op, EventHandler *handler, int epop) {
   struct epoll_event ev;
   ev.events = EPOLLET; /* using edge trigger */

   if (op & Operation::READ) ev.events |= (EPOLLIN | EPOLLRDHUP);
   if (op & Operation::WRITE) ev.events |= EPOLLOUT;

   ev.data.ptr = reinterpret_cast<void*>(handler);

   return ::epoll_ctl(m_epfd, epop, handler->sockfd(), &ev);
}

#else // USE_EPOLL


int Poller::add(Operation op, EventHandler *handler) {
  std::unique_lock<std::mutex> lk(m_lock);
  if (m_size == m_cap) {
    struct pollfd *new_fds = new struct pollfd [2 * m_cap];
    EventHandler **new_evs = new EventHandler* [2 * m_cap];
 
    memcpy(new_fds, m_fds, m_cap * sizeof(struct pollfd));
    memcpy(new_evs, m_evs, m_cap * sizeof(EventHandler*));

    delete [] m_fds;
    delete [] m_evs;

    m_fds = new_fds;
    m_evs = new_evs;

    m_cap *= 2;
  }

  m_evs[m_size] = handler;
  auto & pfd = m_fds[m_size];

  pfd.fd = handler->sockfd();
  pfd.revents = 0;

  if (op & Operation::READ) pfd.events |= POLLIN;
  if (op & Operation::WRITE) pfd.events |= POLLOUT;

  if (m_size++ == 0)
    m_cond.notify_one();

  return 0;
}

int Poller::del(Operation op, EventHandler *handler) {
  std::unique_lock<std::mutex> lk(m_lock);

  int i;
  for (i = 0; i < m_size; ++i) {
    if (m_evs[i] == handler) {
      if ( (op == Operation::READ && m_fds[i].events & POLLIN) ||
           (op == Operation::WRITE && m_fds[i].events & POLLOUT))
        break;
    }
  }

  if (i >= m_size) return -1;

  m_fds[i] = m_fds[m_size - 1];
  m_evs[i] = m_evs[m_size - 1];
  m_size--;

  return 0;
}

int Poller::poll() {
  int ready = 0;
  
  while (ready == 0) {
    std::unique_lock<std::mutex> lk(m_lock);
    while (m_size == 0) 
      m_cond.wait(lk);
    
    lk.unlock();

    ready = ::poll(m_fds, m_size, MAX_WAITING_TIME);
  }
  
  if (ready > 0) {
    std::unique_lock<std::mutex> lk(m_lock);
    std::vector<std::pair<EventHandler*, Operation>> activities;

    for (int i = 0; i < m_size; ++i) {
      auto ev = m_evs[i];
      auto & revents = m_fds[i].revents;

      if (revents & POLLERR) {
        activities.emplace_back(ev, Operation::ERROR);
        continue;
      } 

      if (revents & (POLLIN | POLLRDHUP)) {
        activities.emplace_back(ev, Operation::READ);
      }
      if (revents & POLLOUT) {
        activities.emplace_back(ev, Operation::WRITE);
      }
    }

    lk.unlock();

    // do the operations here for avoid deadlock.
    // since the we may call add/del during the `ev->handle_event()'
    for (auto & active : activities) {
      active.first->handle_event(active.second);
    }
  }

  return ready;
}

#endif // USE_EPOLL
