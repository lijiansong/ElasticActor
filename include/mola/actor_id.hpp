#ifndef _MOLA_ACTOR_ID_H_
#define _MOLA_ACTOR_ID_H_

#include <stdint.h>
#include <string>

namespace mola {

struct ActorID {
  
  static const uint32_t ANY = 0x0000ffff;
  static const uint32_t LOCAL = 0;
  static const uint32_t CLIENT = 0xffffffffUL;
  
  uint32_t node_id;
  uint32_t module_id:16;
  uint32_t intf_id:16;
  uint32_t inst_id;


  uint32_t module_id() const { return module_id; }
  uint32_t instance_id() const { return inst_id; }
  uint32_t node_id() const { return node_id; }
  uint32_t intf_id() const { return intf_id; }

  void set_module_id(uint32_t m_id) {
    module_id = m_id;
  }

  void set_inst_id(uint32_t i_id) {
    inst_id = i_id;
  }

  uint32_t hash_key() const {
    return (((uint32_t)module_id) << 16) | 
            inst_id;
  }

  ActorID(uint32_t nid, uint32_t mid, uint32_t iid, uint32_t ifid): node_id(nid), module_id(mid), inst_id(iid), intf_id(ifid){}
  ActorID& operator = (const ActorID& id) {
    node_id = id.node_id;
    inst_id = id.inst_id;
    module_id = id.module_id;
    intf_id = id.intf_id;
  }
/*
  ActorID& operator = (const ActorID& id) {
    m_union.u64 = id.m_union.u64;
    return *this;
  }

  bool operator == (const ActorID& id) {
    return m_union.u64 == id.m_union.u64;
  }

  bool is_valid() const { return m_union.u64 != 0; }
*/
  std::string to_string() const;

#if !defined(CLIENT_CONTEXT)
  bool is_local() const;
#endif
/*
  static inline const ActorID* convert_from_u64(const uint64_t *u64) {
    return reinterpret_cast<const ActorID*>(u64);
  }

  static inline ActorID* convert_from_u64(uint64_t *u64) {
    return reinterpret_cast<ActorID*>(u64);
  }
  */
};

struct ActorAddr {
  
  uint32_t node_id;
  uint32_t station_id;
  uint32_t offset;

  ActorAddr(const ActorAddr& id) 
    : station_id(id.station_id)
    , node_id(id.inst_id)
    , offset(id.offset) {}

  ActorAddr(uint32_t nid = 0, uint32_t sid = 0, uint32_t off) : station_id(sid), inst_id(iid), offset(off) {}


  ActorAddr& operator = (const ActorAddr& id) {
    station_id = id.station_id;
    node_id = id.node_id;
    offset = id.offset;
    return *this;
  }

  bool operator == (const ActorAddr& id) {
    return (station_id == id.station_id) && (node_id == id.node_id) &&(offset == id.offset);
  }

  std::string to_string() const;
  uint32_t node_id() {return node_id;}
#if !defined(CLIENT_CONTEXT)
  bool is_local() const;
#endif


}

#endif // _MOLA_ACTOR_ID_H_
