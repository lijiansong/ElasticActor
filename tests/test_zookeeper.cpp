#include <cstdlib>
#include <iostream>
#include "mola/node_manager.hpp"
#include "mola/multiplexer.hpp"

#define ZK_HOSTS "ict122:2182,ict123:2182,ict125:2182="

using namespace mola;

const char * modules_name[4] = {
  "Mod_A", "Mod_B", "Mod_C", "Mod_D"
};

Module::ID mid[4];

int main() {
  Multiplexer::initialize();

  NodeManager::initialize(NodeManager::NM_ZOOKEEPER);
  auto mgr = NodeManager::instance();
  
  mgr->create_local_node("amalinux", 65344);

  if (mgr->connect(ZK_HOSTS) != 0) {
    std::cerr << "cannot connect to the node manager .." << std::endl;
    exit(-1);
  } 

  std::cout << "node manager connected " << std::endl; 

  std::cout << "beginning to lock the distributed lock .." << std::endl;

  mgr->lock(0);

  for (int i = 0; i < 4; ++i) {
    mid[i] = mgr->register_module(modules_name[i]);

    MOLA_ASSERT(mid[i] != Module::invalid_id);
  
    std::cout << "module " << modules_name[i] 
              << " ID " << mid[i] << std::endl;
  }

  mgr->unlock(0);

  std::cout << "release the distributed lock" << std::endl;

  if (mgr->join(nullptr, 0) != 0) {
    std::cerr << "cannot join to the node manager .." << std::endl;
    exit(-1);
  }

  std::cout << "joined, my node id is " 
            << mgr->local_node()->get_id() << std::endl;

/**
  int num = mgr->sync_all_nodes();
  std::cout << "sync with " << num << " nodes .." << std::endl;
*/
  auto node = mgr->find_node(mgr->local_node()->get_id());
  MOLA_ASSERT(node.get() == mgr->local_node().get());

  for (int i = 0; i < 4; ++i) {
    std::cout << "ID " << mid[i] 
              << " module " << mgr->get_module_name(mid[i])
              << std::endl;
  }

  auto nodes = mgr->query_node_with_module("Mod_A");
  MOLA_ASSERT(nodes.size() > 0);

  nodes = mgr->query_node_with_module("Mod_E");
  MOLA_ASSERT(nodes.size() == 0);

  mgr->leave();
  
  return 0;
}
