#include <sstream>

#include "mola/actor_id.hpp"
#include "mola/node_manager.hpp"

using namespace mola;

bool ActorID::is_local() const {
  if (node_id() == LOCAL) return true;
  auto nmgr = NodeManager::instance();
  return (node_id() == nmgr->local_node()->get_id());
}

std::string ActorID::to_string() const {
  std::stringstream ss;

  ss << "[[ node id : " << node_id() << ", "
     << " module id : " << module_id() << ", "
     << " interface id : " << intf_id() << ", "
     << " instance id : " << instance_id() << " ]]";
  return ss.str();
}
