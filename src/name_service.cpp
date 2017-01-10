#include <iostream>
#include "mola/name_service.hpp"
#include "mola/node_manager.hpp"
#include "mola/actor.hpp"
#include "mola/strong_actor.hpp"

using namespace mola;

ActorAddr* NameService::get_actor_addr(MODULE_ID_T mid, ActorID id) {
  ActorAddr* addr = nullptr;
  // look up local table first if the address record is on local node
  // search local location table cache first
  auto module = Module::find_module(mid);
  if (module != nullptr) {
    addr = module->search_location_cache(id);
  }
  // look up actor address in the location table
  if (addr == nullptr) {
    addr = lookup_location_table(module, mid, id);
  }
  return addr;
}

ActorAddr* NameService::lookup_location_table(Module*, module, MODULE_ID_T mid, ActorID id) {
  //  find which node the location info is located
  return module->search_location(id);
  //  if local, fetch the info
  //  else, generate a location request message and send it
}
