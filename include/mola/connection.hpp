#ifndef _MOLA_CONNECTION_H_
#define _MOLA_CONNECTION_H_

#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <cstdio>
#include <string>
#include <vector>
#include <iostream>

#include "mola/event_handler.hpp"

namespace mola {

const int INVALID_SOCKFD = -1;

using namespace std;
class Connection {
public:
  static bool parseip(std::string& host, uint32_t& ip) {
    uint8_t addr[4] = {0, 0, 0, 0};
    char *p = const_cast<char*>( host.c_str() );

    int i;
    for (i = 0; i < 4 && *p; ++i) {
      uint32_t x = strtoul(p, &p, 0);
      if (x < 0 || x > 256) return false;
      if (*p != '.' && *p != 0) return false;
      if (*p == '.') p++;
      addr[i] = static_cast<uint8_t>(x);
    }

    MOLA_ASSERT(i == 4);
    // FIXME : little endian only!
    ip = *(uint32_t*)addr;
    return true;
  }

  static bool host_to_ip(std::string& host, uint32_t& ip) {
    struct hostent *he;

    if (parseip(host, ip)) return true;

    if (he = gethostbyname(host.c_str())) {
      ip = *(uint32_t*)(he->h_addr);
      return true;
    }

    return false;
  }

  static bool get_primary_ip(std::string& addr) {
    struct ifaddrs *if_addrs = nullptr;
    struct ifaddrs *ifa = nullptr;
    void *addr_ptr_ = nullptr;
    bool found = false;

    getifaddrs(& if_addrs);

    for (ifa = if_addrs; ifa != nullptr; ifa = ifa->ifa_next) {
      if (!ifa->ifa_addr) continue;
      if (ifa->ifa_addr->sa_family == AF_INET) {
        // check for IPv4 only ..
        addr_ptr_ = &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr;
        char addr_[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, addr_ptr_, addr_, INET_ADDRSTRLEN);
        if (!strncmp(addr_, "127.", 4))
          continue;
        addr = addr_;
        found = true;
        break;
      }
    }

    freeifaddrs(if_addrs);

    return found;
  }

  virtual int start() = 0;
  virtual void stop() = 0;

  virtual int sockfd() const = 0;

  inline void nonblock() {
    fcntl(sockfd(), F_SETFL, 
            fcntl(sockfd(), F_GETFL) | O_NONBLOCK);
  }

  virtual ~Connection() {}
protected:
  Connection() {}
};

class BaseServer : public Connection {
public:
  virtual void new_connection(int) = 0;
  /* TODO */
};

template <typename T>
class TCPServer : public BaseServer {
public:
  typedef T ClientType;
  typedef std::unique_ptr<ClientType> ClientPtr;

  explicit TCPServer(const std::string& host, uint16_t port)
    : m_host(host), m_port(port)
    , m_running(false), m_acceptor(nullptr) {}

  explicit TCPServer(const char* host, uint16_t port)
    : m_host(host), m_port(port)
    , m_running(false), m_acceptor(nullptr) {}

  virtual ~TCPServer() { stop(); }

  void new_connection(int client_fd) override {
    std::cout<<"accept a new client on sockfd "<< client_fd;

    ClientType *client = new ClientType(client_fd, this);
    client->start();
    m_clients.emplace_back(client);
  }

  void set_host(std::string& host) { m_host = host; }
  std::string get_host() const { return m_host; }

  void set_port(uint16_t port) { m_port = port; }
  uint16_t get_port() const { return m_port; }

  bool is_running() const { return m_running; }

  AcceptHandler* get_acceptor() const { return m_acceptor.get(); }
  void set_acceptor(AcceptHandler* acceptor) {
  /* Those will be auto called during dtor ..
   *
    if (m_acceptor.get()) {
      m_acceptor->stop();
    }
    */
    m_acceptor.reset(acceptor);
    m_acceptor->start(this);
  }

  int sockfd() const override { return m_sockfd; }

  void stop() override {
    if (m_running) {
      MOLA_LOG_TRACE("stoping the server on sockfd " << sockfd() );
      m_acceptor.reset(nullptr); // m_acceptor->stop();
      for (int i = 0; i < m_clients.size(); ++i) {
        m_clients[i].reset(nullptr); // m_clients[i]->stop();
      }
      ::close(sockfd());
      m_running = false;
    }
  }
  
  int start() override {
    struct sockaddr_in sa;
    socklen_t sn;
    uint32_t ip;
    int n;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;

    if (m_host.length() > 0 && m_host != "*") {
      if (host_to_ip(m_host, ip) < 0) return -1;
      memmove(&sa.sin_addr, &ip, 4);
    }

    sa.sin_port = htons(m_port);

    if ((m_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      perror("server creates socket ");
      return -1;
    }

    if (::getsockopt(m_sockfd, SOL_SOCKET, SO_TYPE, &n, &sn) >= 0) {
      n = 1;
      ::setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof(n));
    }

    if (::bind(m_sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
      ::perror("binding ");
      ::close(m_sockfd);
      return -1;
    }

    ::listen(m_sockfd, m_port);

    nonblock();
  
    AcceptHandler *acceptor = 
      new AcceptHandler(Multiplexer::instance(), m_sockfd);

    set_acceptor(acceptor);

    m_running = true;

    MOLA_LOG_TRACE("start listening the TCP address " 
                  << m_host << ":" << m_port << "...");
    return m_sockfd;
  }

private:

  int m_sockfd;
  std::string m_host;
  uint16_t m_port;
  bool m_running;
  std::unique_ptr<AcceptHandler> m_acceptor;
  std::vector<ClientPtr> m_clients;
};

class BaseClient : public Connection {
public:
  virtual ~BaseClient() {}

  virtual int connect(uint32_t, uint16_t) = 0;
  virtual int connect(std::string&, uint16_t) = 0;
  virtual int connect(const char*, uint16_t) = 0;
  virtual int async_write(std::vector<StreamBuffer*>&) = 0;
  virtual int async_write(StreamBuffer* ) = 0;
  virtual int async_write(const char*, size_t) = 0;
  virtual bool passive() const = 0;
};

template <typename MgrType>
class TCPClient : public BaseClient {
public:
  TCPClient(int sockfd, BaseServer *server)
    : m_sockfd(sockfd), m_server(server), m_running(false) {}
  TCPClient()
    : m_sockfd(INVALID_SOCKFD), m_server(nullptr), m_running(false) {}
  virtual ~TCPClient() { stop(); }

  int connect(uint32_t ip, uint16_t port) override {
    int n;
    struct sockaddr_in sa;
    socklen_t sn;

    m_sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_sockfd < 0) {
      ::perror("client creates socket ");
      return -1;
    }

    memset(&sa, 0, sizeof(sa));
    memmove(&sa.sin_addr, &ip, 4);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    if (::connect(m_sockfd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
      ::perror("client tries to connect ");
      ::close(m_sockfd);
      return -1;
    }

    start();
    return m_sockfd;
  }

  int connect(std::string& host, uint16_t port) override {
    MOLA_ASSERT(!passive());

    uint32_t ip;
    if (host_to_ip(host, ip))
      return connect(ip, port);
    return -1;
  }

  int connect(const char* host, uint16_t port) override {
    std::string s(host);
    return connect(s, port);
  }

  virtual int start() {
    nonblock();
    
    m_handler.reset(
        new StreamHandler(Multiplexer::instance(), m_sockfd) );
    
    m_handler->start(new MgrType(this));

    m_running = true;

    return 0;
  }

  void stop() override {
    if (m_running) {
      m_handler.reset(nullptr); // m_handler->stop();
      ::close(m_sockfd);
      m_running = false;
    }
  }

  EventHandler* get_handler() const { return m_handler.get(); }

  int sockfd() const override { return m_sockfd; }

  BaseServer *get_server() const { return m_server; }

  bool passive() const override { return m_server != nullptr; }

  int async_write(std::vector<StreamBuffer*>& wbuf) override {
    m_handler->async_write(wbuf);
  }

  int async_write(StreamBuffer *wbuf) override {
    m_handler->async_write(wbuf);
  }

  int async_write(const char *buf, size_t size) override {
    if (size == 0) return 0;

    char *nbuf = (char*)malloc(size);
    memcpy(nbuf, buf, size);
    return async_write(new StreamBuffer(size, nbuf, true));
  }

private:
  std::unique_ptr<StreamHandler> m_handler;
  int m_sockfd;
  bool m_running;
  BaseServer *m_server;
};

}
#endif // _MOLA_CONNECTION_H_
