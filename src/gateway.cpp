#include "mola/gateway.hpp"

#if defined(CLIENT_CONTEXT)
#include "mola/client.hpp"
#endif // defined(CLIENT_CONTEXT)

using namespace mola;

std::unique_ptr<GatewayServer> GatewayServer::gw;

void GatewayReceiver::deliver_message() {
  MessageSafePtr message(m_raw);
  m_raw = nullptr;

  auto target = message->get_target();
  
  MOLA_LOG_TRACE("deliver a new received message to target " 
              << std::hex << target.u64() << std::dec );

#if defined(CLIENT_CONTEXT)
  /* fetch the client connection ctx */
  auto gwc = dynamic_cast<client::ConnGatewayClient*>(get_client());
  MOLA_ASSERT(gwc != nullptr);

  auto conn = gwc->get_conn();
  MOLA_ASSERT(conn != nullptr);

  conn->deliver_message(message);

#else // defined(RUNTIME_CONTEXT)
  if (!target.is_local()) {
    MOLA_FATAL_TRACE("receive a message to node " 
                   << target.node_id() << " but my id is "
                   << NodeManager::instance()->local_node()->get_id() );
  }

  /* if in runtime context, just call Dispatcher to deliver it */
  WorkerGroup::instance()->enqueue(
    new AsyncTask( Multiplexer::backend()->get_id(),
                   [=]() mutable -> WorkItem::ExecStatus {
                      Dispatcher::dispatch(message);
                      return WorkItem::ExecStatus::EXIT;
                   }
  ) );
#endif
}
