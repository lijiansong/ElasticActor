#ifndef _MOLA_NETWORK_H_
#define _MOLA_NETWORK_H_

#include <atomic>
#include <vector>

#include "c/mola_network.h"
#include "mola/actor_id.hpp"
#include "mola/connection.hpp"
#include "mola/scheduler.hpp"

namespace mola {

class Actor;

class BuiltinReceiver : public StreamManager {
  enum State {
    STATE_GET_LEN = 0,
    STATE_GET_BUF = 1,
  };

public:
  BuiltinReceiver(BaseClient *client);
  ~BuiltinReceiver() {}

  void consume(StreamBuffer *buf) override;
  StreamBuffer* get_buffer() override;

private:
  void deliver_message(size_t size, char *buf);

  ::mola_proto_t m_proto;
  State m_state;
  size_t m_len;
};


class BuiltinClient : public TCPClient<BuiltinReceiver> {
  using supper = TCPClient<BuiltinReceiver>;
public:
  BuiltinClient(int sockfd, BaseServer *server);

  BuiltinClient(const ActorID& target, ::mola_proto_t proto)
    : m_target(target), m_proto(proto), m_disable(false) {}

  virtual ~BuiltinClient() {}

  int start() override;

  void set_target(const ActorID& target) {
    m_target = target;
  }

  const ActorID& get_target() const { return m_target; }
  ActorID& get_target() { return m_target; }

  ::mola_proto_t get_proto() { return m_proto; }
  void set_proto(::mola_proto_t proto) { m_proto = proto; }

  bool is_disabled() const { return m_disable.load(); }
  void disable() { m_disable.store(true); }
  void enable() { m_disable.store(false); }

  Actor* get_instance();

private:
  ActorID m_target;
  ::mola_proto_t m_proto;
  std::atomic<bool> m_disable;
  std::vector<Actor*> m_instances;
};

class BuiltinServer : public TCPServer<BuiltinClient> {
  
  using supper = TCPServer<BuiltinClient>;

public:  
  BuiltinServer(const std::string& host, uint16_t port, 
                ActorID& target, ::mola_proto_t proto)
    : supper(host, port), m_target(target), m_proto(proto) {}

  virtual ~BuiltinServer() {}

  ActorID& get_target() { return m_target; }
  const ActorID& get_target() const { return m_target; }
 
  ::mola_proto_t get_proto() { return m_proto; }
  void set_proto(::mola_proto_t proto) { m_proto = proto; } 

private:
  ActorID m_target;
  ::mola_proto_t m_proto;
};

}

extern "C" {
  
typedef struct raw_package {
  int size;
  char *buffer;
  void *client;
} raw_package_t;

extern mola_message_vtab_t raw_package_vtab;

extern int SendToSocket(void *client, int size, char *buffer);
extern void BindSocketToInst(void *client, uint32_t intf);

extern void *ConnectToServer(const char *host, uint16_t port,
                             mola_proto_t proto, uint32_t intf);
extern void *StartServer(const char *host, uint16_t port,
                         mola_proto_t proto, uint32_t intf);

extern void CloseSocket(void *sock);

extern void EnableClient(void *sock);
extern void DisablClient(void *sock);

}


#endif //  _MOLA_NETWORK_H_
