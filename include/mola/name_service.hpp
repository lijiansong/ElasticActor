#ifndef _MOLA_NAME_SERVICE_H_
#define _MOLA_NAME_SERVICE_H_

#include "mola/actor.hpp"

namespace mola {

class NameService {
public:
  static ActorAddr* get_actor_addr(MODULE_ID_T mid, INST_ID_T inst);
  static ActorAddr* lookup_location_table(Module* module, MODULE_ID_T mid, ActorID id);
};

}

#endif
