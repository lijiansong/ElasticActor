#ifndef _MOLA_ACTOR_ID_H_
#define _MOLA_ACTOR_ID_H_

#include <stdint.h>
#include <string>

namespace mola {

struct StationID {
  
  static const uint32_t ANY = 0x0000ffff;
  static const uint32_t LOCAL = 0;
  static const uint32_t CLIENT = 0xffffffffUL;
  
  union anno {
    uint64_t u64;
#ifdef __LITTLE_ENDIAN__
    struct {
      uint32_t node_id:16;
      uint32_t module_id:16;
      uint32_t inst_id;
    } sub_4x16;
    struct {
      uint32_t id;
      uint32_t type;
    } sub_2x32;
#else
    struct {
      uint32_t inst_id;
      uint32_t module_id:16;
      uint32_t node_id:16;
    } sub_4x16;
    struct {
      uint32_t type;
      uint32_t id;
    } sub_2x32;
#endif // __LITTLE_ENDIAN__

    anno(uint64_t _u64) : u64(_u64) {}
#ifdef __LITTLE_ENDIAN__
    anno(uint32_t _f0, uint32_t _f1)
      : sub_2x32{_f0, _f1} {}
    anno(uint16_t _f0, uint16_t _f1, uint32_t _f2)
      : sub_4x16{_f0, _f1, _f2} {}
#else
    anno(uint32_t _f0, uint32_t _f1)
      : sub_2x32{_f1, _f0} {}
    anno(uint16_t _f0, uint16_t _f2, uint32_t _f3)
      : sub_4x16{_f3, _f2, _f0} {}
#endif // __LITTLE_ENDIAN__
  } m_union;

  uint32_t node_id() const {
    return ( (m_union.sub_2x32.type == CLIENT) 
                ? m_union.sub_2x32.id
                : m_union.sub_4x16.node_id ); 
  }
  uint32_t module_id() const { return m_union.sub_4x16.module_id; }
  uint32_t instance_id() const { return m_union.sub_4x16.inst_id; }

  void set_node_id(uint32_t node_id) {
    if (m_union.sub_2x32.type == CLIENT)
      m_union.sub_2x32.id = node_id;
    else
      m_union.sub_4x16.node_id = node_id;
  }

  void set_module_id(uint32_t module_id) {
    m_union.sub_4x16.module_id = module_id;
  }

  void set_inst_id(uint32_t inst_id) {
    m_union.sub_4x16.inst_id = inst_id;
  }

  uint32_t hash_key() const {
    return (((uint32_t)m_union.sub_4x16.module_id) << 16) | 
            m_union.sub_4x16.inst_id;
  }

  uint64_t u64() const { return m_union.u64; }
  void set(uint64_t u64) { m_union.u64 = u64; }

  StationID(const StationID& id) 
    : m_union(id.m_union.u64) {}

  StationID(uint64_t id = 0) : m_union(id) {}

  StationID(uint32_t module_id, uint32_t inst_id)
    : m_union(LOCAL, module_id, inst_id) {}

  StationID(uint32_t node_id, uint32_t module_id,
          , uint32_t inst_id) 
    : m_union(node_id, module_id, inst_id) {}

  StationID& operator = (const StationID& id) {
    m_union.u64 = id.m_union.u64;
    return *this;
  }

  bool operator == (const StationID& id) {
    return m_union.u64 == id.m_union.u64;
  }

  bool is_valid() const { return m_union.u64 != 0; }

  std::string to_string() const;

#if !defined(CLIENT_CONTEXT)
  bool is_local() const;
#endif

  static inline const StationID* convert_from_u64(const uint64_t *u64) {
    return reinterpret_cast<const StationID*>(u64);
  }

  static inline StationID* convert_from_u64(uint64_t *u64) {
    return reinterpret_cast<StationID*>(u64);
  }
};

struct ActorAddr {
  
  uint64_t station_id;
  uint32_t inst_id;

  ActorAddr(const ActorAddr& id) 
    : station_id(id.station_id)
    , inst_id(id.inst_id){}

  ActorAddr(uint64_t sid = 0, uint32_t iid = 0) : station_id(sid), inst_id(iid) {}


  ActorAddr& operator = (const ActorAddr& id) {
    station_id = id.station_id;
    inst_id = id.inst_id;
    return *this;
  }

  bool operator == (const ActorAddr& id) {
    return (station_id == id.station_id) && (inst_id == inst_id);
  }

  bool is_valid() const { return m_union.u64 != 0; }

  std::string to_string() const;

#if !defined(CLIENT_CONTEXT)
  bool is_local() const;
#endif

  static inline const ActorAddr* convert_from_u64(const uint64_t *u64) {
    return reinterpret_cast<const ActorAddr*>(u64);
  }

  static inline ActorAddr* convert_from_u64(uint64_t *u64) {
    return reinterpret_cast<ActorAddr*>(u64);
  }
};

using ActorID = uint32_t;

}

#endif // _MOLA_ACTOR_ID_H_
