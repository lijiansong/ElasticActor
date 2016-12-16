#include "mola/event_handler.hpp"
#include "mola/connection.hpp"

using namespace mola;

bool AcceptHandler::handle_event(Operation op) {
  if (m_manager && op == Operation::READ) {
    int client_sockfd;
    while (try_accept(client_sockfd)) {
      m_manager->new_connection(client_sockfd);
    }
  }

  if (op == Operation::ERROR) {
    MOLA_ERROR_TRACE("accept handler got an error, sockfd is " << sockfd());
    backend()->del(Operation::READ, this);
  }
  return false;
}
