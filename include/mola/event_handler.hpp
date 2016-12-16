#ifndef _MOLA_EVENT_HAHNDLER_H_
#define _MOLA_EVENT_HAHNDLER_H_

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <cstring>
#include <vector>
#include <sstream>
#include <memory>
#include <mutex>
#include <functional>

#include "mola/multiplexer.hpp"
#include "mola/mailbox.hpp"
#include "mola/spinlock.hpp"

namespace mola {

class StreamBuffer {
  typedef std::function<void(void)> dtor_t;

  size_t m_size;
  char* m_data;
  bool m_release;
  std::unique_ptr<StreamBuffer> m_link;
  dtor_t m_dtor;

public:
  StreamBuffer(size_t size, 
               char *data = nullptr, 
               bool release = false,
               dtor_t dtor = nullptr)
    : m_size(size), m_data(data)
    , m_release(release), m_dtor(dtor)
    , m_link(nullptr), next(nullptr) {
    if (m_data == nullptr) {
      m_data = (char*)malloc(m_size);
      m_release = true;
    }
  }
  
  ~StreamBuffer() { 
    if (m_release) free(m_data); 
    if (m_dtor != nullptr) m_dtor();
  }

  std::string to_string() const {
    std::stringstream ss;
    for (int i = 0; i < m_size; ++i) {
      ss << std::hex << (int)(m_data[i]) << std::dec;
    }
    return ss.str();
  }

  size_t size() const { return m_size; }
  char* data() const { return m_data; }

  template <typename T>
  T cast_as() {
    return reinterpret_cast<T>(m_data);
  }

  StreamBuffer* get_link() { return m_link.release(); }
  void set_link(StreamBuffer *link) { m_link.reset(link); }

  /* for mailbox to access */
  StreamBuffer *next;
};

class BaseClient;

class StreamManager {
  static const size_t default_size = 512;

protected:
  BaseClient *m_client;
  size_t m_size;
public:
  StreamManager(BaseClient *client = nullptr, size_t size = default_size)
    : m_client(client), m_size(size) {}

  void set_client(BaseClient *client) {
    m_client = client;
  }

  BaseClient* get_client() const { return m_client; }

  virtual void io_failure(Operation op) { 
    /* TODO */
    MOLA_ERROR_TRACE(" error when doing IO op " << op );
  }
  virtual void consume(StreamBuffer *buf) { /* TODO */ }
  virtual StreamBuffer* get_buffer() { 
    return new StreamBuffer(m_size);
  }
};

class EventHandler {
public:
  EventHandler(Multiplexer* backend, int sockfd) 
    : m_backend(backend)
    , m_sockfd(sockfd)
#ifdef USE_EPOLL
    , m_ref(0) 
#endif
  {}

  virtual ~EventHandler() {}

  virtual bool handle_event(Operation op) = 0;

  int sockfd() const { return m_sockfd; }

  Multiplexer* backend() const {
    return m_backend;
  }

#ifdef USE_EPOLL
  bool add() { return m_ref++ == 0; }
  bool del() { return --m_ref == 0; }

private:
  std::atomic<int> m_ref;
#endif

protected:
  Multiplexer* m_backend;
  int m_sockfd;

};

class BaseServer;

class AcceptHandler : public EventHandler {
public:
  AcceptHandler(Multiplexer* backend, int sockfd)
    : EventHandler(backend, sockfd) {}
  virtual ~AcceptHandler() { stop(); }

  void start(BaseServer* mgr) {
    m_manager = mgr;
    backend()->add(Operation::READ, this);
  }

  bool handle_event(Operation op) override;

private:
  void stop() {
    backend()->del(Operation::READ, this);
  }

  bool try_accept(int &client_sockfd) {
    struct sockaddr addr;
    memset(&addr, 0, sizeof(addr));
    socklen_t addrlen = sizeof(addr);
    client_sockfd = ::accept(m_sockfd, &addr, &addrlen);
    return (client_sockfd > 0);
  }

  BaseServer *m_manager;
};


class StreamHandler : public EventHandler {
  typedef utils::Mailbox<StreamBuffer> Mailbox;
  typedef std::unique_ptr<StreamManager> StreamManagerPtr;
  typedef std::unique_ptr<StreamBuffer> StreamBufferPtr;

public:
  StreamHandler(Multiplexer* backend, int sockfd)
    : EventHandler(backend, sockfd)
    , m_collected(0), m_rd_buf(nullptr)
    , m_written(0), m_wr_buf(nullptr) { }

  virtual ~StreamHandler() { stop(); }

  void start(StreamManager* reader/*, StreamManager* writer = nullptr*/) {
    m_reader.reset(reader);
    m_writer.reset(/*writer ? writer : */ new StreamManager);

    m_rd_buf.reset(m_reader->get_buffer());

    backend()->add(Operation::RDWR, this);
  }

  bool handle_event(Operation op) override {
    switch (op) {
    case ERROR:
      {
        // TODO
        m_reader->io_failure(Operation::READ);
        m_writer->io_failure(Operation::WRITE);

        backend()->del(Operation::RDWR, this);
        break;
      }
    case READ:
      {
        ssize_t nrd = 0; // read bytes
        if (!read_some(nrd, m_sockfd, 
                       m_rd_buf->data() + m_collected,
                       m_rd_buf->size() - m_collected) || nrd == 0) {
          m_reader->io_failure(op);
          backend()->del(op, this);
        } else if (nrd > 0) {
          MOLA_LOG_TRACE("read " << nrd << " bytes from socket " << m_sockfd);

          m_collected += nrd;
          if (m_collected == m_rd_buf->size()) {
            m_reader->consume(m_rd_buf.release());
            // maybe there 're more data in read buffer ,
            // so try again !!
            read_loop(); 
          }
          return true;
        }

        break;
      }
    case WRITE:
      {
        ssize_t nwr; // written bytes
#if 1
        utils::TryLockGuard lock(m_lock, 1);

        if (!lock.is_locked()) return true;
#else
        utils::LockGuard lock(m_lock);
#endif
__wr_loop:
        nwr = 0;
        
        while (m_wr_buf.get() == nullptr) {
          // try to fetch from my mailbox
          // since the mailbox should be read
          // by only one thread, we must lock
          // this critical section !!
          m_wr_buf.reset(m_mbox.dequeue());

          if (m_wr_buf.get() == nullptr) {
            if (m_mbox.is_blocked() || m_mbox.try_block()) {
              // the mailbox is empty, let the next async send
              // operation to enable it again!!
              return false;
            }
            // FIXME : if mailbox is not blocked, 
            //         should we loop again or just quit ??
          }
        }

        if (!write_some(nwr, m_sockfd,
                        m_wr_buf->data() + m_written,
                        m_wr_buf->size() - m_written)) {
          m_writer->io_failure(op);
          backend()->del(op, this);
          
          // close the mailbox, so later async send operation
          // will cause an error !!
          m_mbox.close();
          return false;
        }
        
        MOLA_LOG_TRACE("write " << nwr << " bytes to socket " << m_sockfd
                      << " buf \"" << m_wr_buf->to_string() << "\"");

        if (nwr > 0) {
          m_written += nwr;
          if (m_written == m_wr_buf->size()) {
            // reset for next write operation !!
            // try to write more data ..
            write_loop(); 
            goto __wr_loop;
          }
        }

      }
    }
    /* TODO : report the unhandled cases !! */
    return false;
  }

  int async_write(StreamBuffer * write_buf) {
    auto ret = m_mbox.enqueue(write_buf);
    if (ret == utils::MAILBOX_FAILURE) {
      delete write_buf;
      return -1;
    }

    if (ret == utils::MAILBOX_UNBLOCKED) {
#if 1
      while (handle_event(Operation::WRITE));
#else
      handle_event(Operation::WRITE);
#endif
    }
    return 0;
  }

  int async_write(std::vector<StreamBuffer*>& write_buf_list) {
    auto size = write_buf_list.size();
    /* wirte operation must be a transaction */
    for (int i = 0; i < size - 1; ++i) {
      write_buf_list[i]->set_link(write_buf_list[i+1]);
    }

    return async_write(write_buf_list[0]);
  }
  
private:

  void stop() { 
    /* TODO */ 
    backend()->del(Operation::RDWR, this);
    
    /* FIXME : how to handle the case when handler is deleted but 
     *         still used by the epoll thread ?? */
    utils::LockGuard guard(m_lock);
    while (auto e = m_mbox.dequeue()) {
      delete e; // cannot deliver those messages ..
    }
  }

  bool read_some(ssize_t& nrd, int sock, char * buf, size_t size) {
    nrd = ::read(sock, buf, size);
    if (nrd < 0 && errno != EAGAIN) {
      ::perror(" during read_some ..");
      MOLA_ERROR_TRACE(" sock " << sock << " buf " << buf << " size " << size);
      return false; // some error may happen
    }
    return true;
  }

  bool write_some(ssize_t& nwr, int sock, char * buf, size_t size) {
    nwr = ::write(sock, buf, size);
    if (nwr < 0 && errno != EAGAIN) {
      ::perror(" during write_some ..");
      MOLA_ERROR_TRACE(" sock " << sock << " buf " << buf << " size " << size);
      return false;
    }
    return true;
  }

  void read_loop() {
    m_collected = 0;
    m_rd_buf.reset(m_reader->get_buffer());
  }

  void write_loop() {
    m_written = 0;  
    m_wr_buf.reset(m_wr_buf->get_link());
  }

  StreamManagerPtr m_reader;
  StreamManagerPtr m_writer;

  size_t m_collected;
  StreamBufferPtr m_rd_buf;

  Mailbox m_mbox;
  utils::SpinLock m_lock;
  size_t m_written;
  StreamBufferPtr m_wr_buf;
};

}

#endif // _MOLA_EVENT_HAHNDLER_H_
