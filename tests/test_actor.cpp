#include "mola/module.hpp"
#include "mola/node_manager.hpp"
#include "mola/scheduler.hpp"

using namespace mola;

const char *modules[] = {
  "./modules/libmodule_A.so",
  "./modules/libmodule_B.so",
  "./modules/libmodule_C.so",
};

int main() {
  /* init the scheduler */
  auto wg = WorkerGroup::initialize();

  /* init the single node manager */
  auto mgr = NodeManager::initialize(NodeManager::NM_SINGLETON);
  mgr->create_local_node();
  
  /* load the modules */
  std::vector<Module*> m;
  std::vector<std::string> argv;

  for (int i = 0; i < 3; ++i) {
    auto mod = Module::dynload_module(modules[i], argv);
    if (mod != nullptr)
      m.push_back(mod);
  }

  MOLA_ASSERT(m.size() == 3);

  /* all modules are loaded, we can start the schduler
   * threads now !!*/
  wg->start_all();

  for (;;)
    std::this_thread::yield();

  return 0;
}
