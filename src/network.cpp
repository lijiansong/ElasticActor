#include "mola/message.hpp"
#include "mola/network.hpp"
#include "mola/dispatcher.hpp"
#include "mola/actor.hpp"

using namespace mola;

extern "C" {
  static void raw_package_release(raw_package_t *raw);
}

BuiltinReceiver::BuiltinReceiver(BaseClient *client)
  : StreamManager(client), m_state(STATE_GET_LEN) {
  auto client_ = dynamic_cast<BuiltinClient*>(client);
  MOLA_ASSERT(client_ != nullptr);

  m_proto = client_->get_proto();
}

void BuiltinReceiver::deliver_message(size_t len, char *buf) {
  auto client = dynamic_cast<BuiltinClient*>(get_client());

  auto instance = client->get_instance();
  MOLA_ASSERT(instance != nullptr);

  auto msg = mola_alloc_message(& raw_package_vtab,
                                (mola_message_dtor_cb_t) raw_package_release);
  raw_package_t *raw = reinterpret_cast<raw_package_t*>(msg->user_data);

  raw->size = len;
  raw->buffer = buf;
  raw->client = client;

  MessageSafePtr rt_msg(msg);
  rt_msg->set_target(client->get_target());

  instance->deliver_message(rt_msg);
}

void BuiltinReceiver::consume(StreamBuffer *buf) {
  MOLA_ASSERT(m_len == buf->size());
  
  switch (m_state) {
  case STATE_GET_LEN:
   {
    /* get the BODY 's size */
    m_len = m_proto->body_size(m_proto->ctx, buf->data(), m_len);

    if (m_len > 0)
      m_state = STATE_GET_BUF;
    break;
   }
  case STATE_GET_BUF:
   {
    auto client = dynamic_cast<BuiltinClient*>(get_client());
    
    if (client->is_disabled()) {
      free(buf->data());
    } else {
      auto buffer = buf->data();
      auto size = buf->size();
      auto self = this;

      auto backend = Multiplexer::backend();
      MOLA_ASSERT(backend != nullptr);
    
      WorkerGroup::instance()->enqueue(
        new AsyncTask( backend->get_id(), [=]() -> WorkItem::ExecStatus {
          self->deliver_message(size, buffer);
            return WorkItem::ExecStatus::EXIT;
        }
      ));
    }

    m_state = STATE_GET_LEN;
    break;
   }
  }

  delete buf;
}

StreamBuffer* BuiltinReceiver::get_buffer() {
  switch (m_state) {
  
  case STATE_GET_LEN:
   {
    m_len = m_proto->header_size(m_proto->ctx);
    MOLA_ASSERT(m_len > 0);
    char *buffer = (char*)calloc(1, m_len + 1); // FIXME!!

    return new StreamBuffer(m_len, buffer, true);
   }
  case STATE_GET_BUF:
   {
    MOLA_ASSERT(m_len > 0);
    char *buffer = (char*)malloc(m_len + 1); // FIXME!!

    return new StreamBuffer(m_len, buffer);
   }
  }

  return nullptr;
}



BuiltinClient::BuiltinClient(int sockfd, BaseServer *server) 
  : supper(sockfd, server), m_disable(false) {
  BuiltinServer* server_ = dynamic_cast<BuiltinServer*>(server);
  MOLA_ASSERT(server_ != nullptr);

  m_target = server_->get_target();
  m_proto = server_->get_proto();
}

int BuiltinClient::start() {
  auto module = Module::find_module(m_target.module_id());
  MOLA_ASSERT(module != nullptr);

  if (m_proto->bind) {
    // Bind this client to an specific instance
    m_instances.emplace_back( 
      module->select_or_create_instance(m_target.instance_id()) );
    MOLA_ASSERT(m_instances[0] != nullptr);
  } else {
    m_instances = module->get_all_instances();
    MOLA_ASSERT(m_instances.size() > 0);
  }

  if (passive()) {
    auto msg = mola_alloc_message(& raw_package_vtab,
                                  (mola_message_dtor_cb_t)raw_package_release);
    raw_package_t *raw = reinterpret_cast<raw_package_t*>(msg->user_data);

    raw->size = 0;
    raw->buffer = nullptr;
    raw->client = this;

    auto instance = get_instance();
    MOLA_ASSERT(instance != nullptr);

    MessageSafePtr rt_msg(msg);
    rt_msg->set_target(m_target);

    instance->deliver_message(rt_msg);
  }

  supper::start();
}

Actor* BuiltinClient::get_instance() {
  int id = AbsolateWorker::random() % m_instances.size();
  return m_instances[id];
}



extern "C" {

// --- default proto definition ---
static size_t _simple_get_header_size(void * /* unused */) {
  return sizeof(size_t);
}

static size_t _simple_get_body_size(void * /* unused */, 
                                    const char* header, size_t size) {
  assert(size == sizeof(size_t));
  return *(reinterpret_cast<const size_t*>(header));
}

static size_t _simple_make_buffer(void * /* unused */,
                                  size_t bsize, const char *bbuf,
                                  char ** sbuf) {
  size_t size = sizeof(size_t) + bsize;
  *sbuf = new char[size];

  ::memcpy(*sbuf, &bsize, sizeof(size_t));
  ::memcpy(*sbuf + sizeof(size_t), bbuf, bsize);
  
  return size;
}

static void _simple_release_buffer(void * /* unused */, char *buffer) {
  delete buffer;
}

struct mola_proto _mola_default_proto = {
  .header_size = _simple_get_header_size,
  .body_size = _simple_get_body_size,
  .make_buffer =  _simple_make_buffer,
  .release_buffer = _simple_release_buffer,
  .ctx = NULL,
};
// ------------------------------

static void raw_package_release(raw_package_t *raw) {
  if (raw->size > 0 && raw->buffer != NULL) {
    free(raw->buffer);
    raw->size = 0;
    raw->buffer = NULL;
  }
}

mola_message_vtab_t raw_package_vtab = {
  .size = sizeof(raw_package_t),

  .init = NULL,
  .pack_size = NULL,
  .pack = NULL,
  .unpack = NULL,
  .free_unpacked = NULL,
  // .dtor = (mola_message_dtor_cb_t)raw_package_release,
  .copy = NULL
};

int SendToSocket(void *client_, int size, char *buffer) {
  auto client = reinterpret_cast<mola::BuiltinClient*>(client_);
  auto proto = client->get_proto();
  
  char *pbuf = buffer;
  size_t psize = size;
  
  if (proto->make_buffer)
    psize = proto->make_buffer(proto->ctx, size, buffer, &pbuf);
  
  auto sb = new mola::StreamBuffer(psize, pbuf, false,
                                   [=]() {
                                     if (proto->release_buffer)
                                       proto->release_buffer(proto->ctx, pbuf);
                                   } );

  client->async_write(sb);
  return 0;
}

void BindSocketToInst(void * client_, uint32_t intf) {
  auto client = reinterpret_cast<mola::BuiltinClient*>(client_);
  auto current = mola::Actor::current();

  client->set_target(current->get_id());
  client->get_target().set_intf_id(intf);
}


void* ConnectToServer(const char *host, uint16_t port, 
                      mola_proto_t proto, uint32_t intf) {
  mola::ActorID target = mola::Actor::current()->get_id();
  target.set_intf_id(intf);

  if (proto == NULL) proto = &_mola_default_proto;
  mola::BuiltinClient *client = new mola::BuiltinClient(target, proto);
  
  if (client->connect(host, port) <= 0)
    return NULL;
  return client;
}

void* StartServer(const char *host, uint16_t port, 
                  mola_proto_t proto, uint32_t intf) {
  mola::ActorID target = mola::Actor::current()->get_id();
  target.set_intf_id(intf);
  target.set_inst_id(mola::ActorID::ANY);

  if (proto == NULL) proto = &_mola_default_proto;

  mola::BuiltinServer *server = 
    new mola::BuiltinServer(host, port, target, proto);

  if (server->start() <= 0)
    return NULL;
  return server;
}

void CloseSocket(void *sock) {
  mola::Connection* conn = 
    reinterpret_cast<mola::Connection*>(sock);
  conn->stop();
}

void EnableClient(void *sock) {
  auto client = reinterpret_cast<mola::BuiltinClient*>(sock);
  if (client != nullptr)
    client->enable();
}

void DisableClient(void *sock) {
  auto client = reinterpret_cast<mola::BuiltinClient*>(sock);
  if (client != nullptr)
    client->disable();
}

}
