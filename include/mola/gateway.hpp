#ifndef _MOLA_GATEWAY_H_
#define _MOLA_GATEWAY_H_

#include <arpa/inet.h>

#include "mola/connection.hpp"
#include "mola/node_manager.hpp"
#include "mola/dispatcher.hpp"

namespace mola {

class GatewayReceiver;

class GatewayClient : public TCPClient<GatewayReceiver> {
 public:
  GatewayClient(Node::ID nid = Node::invalid_id) 
    : m_nid(nid) {}

  GatewayClient(int sockfd,  BaseServer *server) 
    : TCPClient<GatewayReceiver>(sockfd, server)
    , m_nid(Node::invalid_id) {}

  virtual ~GatewayClient() {}
 
  void set_nid(Node::ID nid) { m_nid = nid; }
  Node::ID get_nid() const { return m_nid; }

 private:
  Node::ID m_nid;
};

class GatewayReceiver : public StreamManager {
  enum State {
    STATE_HANDSHAKE = 0,
    STATE_WAIT_HANDSHAKE,
    STATE_GET_HEAD,
    STATE_GET_BODY, 
  };

public:
  GatewayReceiver(BaseClient *gwc)
    : StreamManager(gwc)
    , m_state(gwc->passive() 
                ? STATE_WAIT_HANDSHAKE 
                : STATE_HANDSHAKE)
    , m_nid(Node::invalid_id)
    , m_raw(nullptr) { 

    auto gwc_ = dynamic_cast<GatewayClient*>(gwc);
    MOLA_ASSERT(gwc_ && "must be a `GatewayClient' here!");

    m_nid = gwc_->get_nid();
  }

  ~GatewayReceiver() {}

  void consume(StreamBuffer *buf) override {
    switch (m_state) {
    case STATE_WAIT_HANDSHAKE:
     {
       // TODO
       m_nid = ntohl(*(buf->cast_as<uint32_t*>()));
       
       MOLA_LOG_TRACE("receive a handshake info, node id is " << m_nid);

       auto node = NodeManager::instance()->find_node(m_nid);
       if (node.get() && node->get_client() == nullptr) {
         node->set_client(get_client()); // FIXME !!
       }
       m_state = STATE_GET_HEAD;
       break;
     }

    case STATE_HANDSHAKE:
      m_state = STATE_GET_HEAD;

    case STATE_GET_HEAD:
     {
       // TODO
       MOLA_LOG_TRACE("receive " << buf->size() << " bytes data ..");

       MOLA_ASSERT(buf->size() == sizeof(mola_message_metadata_t));

       mola_message_metadata_t &meta = m_raw->get_metadata();
       meta.data_type = TYPE_STREAM;

       if (meta.size > 0) {
         m_state = STATE_GET_BODY;
       } else {
         // TODO : deliver this message !!
         deliver_message();
       }

       MOLA_LOG_TRACE("got a message header, size is " << meta.size 
                    << " from " << std::hex 
                    << MOLA_MESSAGE_SOURCE(meta) << " to "
                    << MOLA_MESSAGE_TARGET(meta) << std::dec );
       break;
     }

    case STATE_GET_BODY:
     {
       // TODO
       size_t size = MOLA_MESSAGE_SIZE(m_raw->get_metadata());
       MOLA_ASSERT(size == buf->size());
       
       m_raw->set_stream_data(buf->data());

       MOLA_LOG_TRACE("got a message body,  size is " << size
                    << " buf is \"" << buf->to_string() << "\"");

       deliver_message();

       m_state = STATE_GET_HEAD;
       break;
     }

    }

    delete buf;
  }

  StreamBuffer* get_buffer() override {
    switch (m_state) {
    
    case STATE_WAIT_HANDSHAKE:
      {
       static Node::ID nid = Node::invalid_id;
       return new StreamBuffer(sizeof(nid),
                               reinterpret_cast<char*>(& nid));
      }
    case STATE_HANDSHAKE:
     {
      m_state = STATE_GET_HEAD;
     }
    case STATE_GET_HEAD:
     {
      m_raw = new MessageWrapper(nullptr, 0);

      return new StreamBuffer(sizeof(mola_message_metadata_t),
                              reinterpret_cast<char*>(&(m_raw->get_metadata())) );
     }
    case STATE_GET_BODY:
     {
      MOLA_ASSERT(m_raw != nullptr);
      size_t size = MOLA_MESSAGE_SIZE(m_raw->get_metadata());
      char * buffer = (char*)malloc(size);

      return new StreamBuffer(size, buffer);
     } 
    } // switch

    return nullptr;
  }

  void io_failure(Operation op) override {
    MOLA_ERROR_TRACE("peer down detected, node id is " << std::hex << m_nid << std::dec);
    // delete the node id from node_manager 
    // and update the per-thread cache info
    NodeManager::instance()->delete_node(m_nid);
  }

private:
  void deliver_message();
 
  State m_state;
  Node::ID m_nid;
  MessageWrapper *m_raw;
};


class GatewayServer : 
    public TCPServer<GatewayClient> {

  using supper = TCPServer<GatewayClient>;
  static std::unique_ptr<GatewayServer> gw;

public:
  typedef NodeManager::NodePtr NodePtr;
  
  /* start the listener, maybe called after creating the local node */
  static void initialize() {
    if (gw.get() != nullptr) {
      // TODO : report error !!
      return;
    }

    NodePtr node = NodeManager::instance()->local_node();
    MOLA_ASSERT(node != nullptr);
    gw.reset( new GatewayServer(node) );
    int ret = gw->start();
    if (ret < 0) {
      MOLA_FATAL_TRACE("cannot establish the tcp server!");
    }
  }

  static GatewayServer* gateway() { return gw.get(); }

  GatewayServer(std::string& host, uint16_t port)
    : supper(host, port) {}

  GatewayServer(NodePtr& node)
    : supper(node->get_ip(), node->get_port()) {}
  virtual ~GatewayServer() {}
};

}

#endif // _MOLA_GATEWAY_H_
