#include "mola/client.hpp"

using namespace mola;
using namespace mola::client;

#define ZK_HOSTS "ict122:2182,ict123:2182,ict125:2182="

static mola_message_vtab_t vtab = { .size = sizeof(int), };

int main() {
  ClientConn::initialize();

  std::unique_ptr<ClientConn> conn(
        new ClientConn(NodeManager::NM_ZOOKEEPER) );

  conn->connect(ZK_HOSTS);
  
  MessageSafePtr res, req;
  
  req = ::mola_alloc_message(& vtab, NULL);
  int *p = (int*)ConvertRawToType_(req.get(), &vtab, 1);

  *p = 4;

  conn->send_message("GatherTest", 0, req);
  conn->recv_message("*", res);

  MOLA_ASSERT(! res.is_nil());

  std::cout << "received message is " 
            << *( (int*)ConvertRawToType_(res.get(), &vtab, 1) ) << std::endl;

  conn->disconnect();

  return 0;
}
