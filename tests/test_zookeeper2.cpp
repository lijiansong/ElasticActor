#include <unistd.h>
#include <signal.h>

#include <cstdlib>
#include <iostream>
#include <atomic>

#include "mola/node_manager.hpp"
#include "mola/multiplexer.hpp"
#include "mola/dispatcher.hpp"

#define ZK_HOSTS "ict122:2182,ict123:2182,ict125:2182="

using namespace mola;

const char *modules[] = {
  "./modules/libmodule_A.so",
  "./modules/libmodule_B.so",
  "./modules/libmodule_C.so",
};

std::atomic<bool> g_loop(true);

extern "C" void sigint_handler(int sig) {
  g_loop.store(false);
}

int start(bool is_parent) {
  uint16_t port = is_parent ? 65344 : 65333;

  Multiplexer::initialize();

  Dispatcher::initialize();

  NodeManager::initialize(NodeManager::NM_ZOOKEEPER);
  auto mgr = NodeManager::instance();
  
  mgr->create_local_node("amalinux", port);

  if (mgr->connect(ZK_HOSTS) != 0) {
    std::cerr << "cannot connect to the node manager .." << std::endl;
    exit(-1);
  } 

  std::cout << "node manager connected " << std::endl; 

  /* init the scheduler */
  auto wg = WorkerGroup::initialize();

  std::cout << "beginning to lock the distributed lock .." << std::endl;

  mgr->lock(0);

  std::vector<std::string> dummy;

  if (is_parent) {
    auto mod = Module::dynload_module(modules[0], dummy);
    MOLA_ASSERT(mod != nullptr);
  } else {
    auto mod = Module::dynload_module(modules[1], dummy);
    MOLA_ASSERT(mod != nullptr);

    mod = Module::dynload_module(modules[2], dummy);
    MOLA_ASSERT(mod != nullptr);
  }

  mgr->unlock(0);

  std::cout << "release the distributed lock" << std::endl;

  if (mgr->join(nullptr, 0) != 0) {
    std::cerr << "cannot join to the node manager .." << std::endl;
    exit(-1);
  }

  std::cout << "joined, my node id is " 
            << mgr->local_node()->get_id() << std::endl;

  
  /* all modules are loaded, we can start the schduler
   * threads now !!*/
  wg->start_all();

  ::signal(SIGINT, sigint_handler);

  for (;g_loop.load();) 
    std::this_thread::yield();

  mgr->leave();
  
  return 0;
}

int main(int argc, char **argv) {
  MOLA_ASSERT(argc > 1);

  int p = std::stoi(argv[1]);

  signal(SIGCHLD, SIG_IGN);
  return start(p != 0);
}
