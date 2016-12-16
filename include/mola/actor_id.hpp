#ifndef _MOLA_ACTOR_ID_H_
#define _MOLA_ACTOR_ID_H_

#include <stdint.h>
#include <string>

namespace mola {

struct ActorID {
  
  static const uint32_t ANY = 0x0000ffff;
  static const uint32_t LOCAL = 0;
  static const uint32_t CLIENT = 0xffffffffUL;
  
  union anno {
    uint64_t u64;
#ifdef __LITTLE_ENDIAN__
    struct {
      uint32_t node_id:16;
      uint32_t module_id:16;
      uint32_t intf_id:16;
      uint32_t inst_id:16;
    } sub_4x16;
    struct {
      uint32_t id;
      uint32_t type;
    } sub_2x32;
#else
    struct {
      uint32_t inst_id:16;
      uint32_t intf_id:16;
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
    anno(uint16_t _f0, uint16_t _f1, uint16_t _f2, uint16_t _f3)
      : sub_4x16{_f0, _f1, _f2, _f3} {}
#else
    anno(uint32_t _f0, uint32_t _f1)
      : sub_2x32{_f1, _f0} {}
    anno(uint16_t _f0, uint16_t _f1, uint16_t _f2, uint16_t _f3)
      : sub_4x16{_f3, _f2, _f1, _f0} {}
#endif // __LITTLE_ENDIAN__
  } m_union;

  uint32_t node_id() const {
    return ( (m_union.sub_2x32.type == CLIENT) 
                ? m_union.sub_2x32.id
                : m_union.sub_4x16.node_id ); 
  }
  uint32_t module_id() const { return m_union.sub_4x16.module_id; }
  uint32_t intf_id() const { return m_union.sub_4x16.intf_id; }
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

  void set_intf_id(uint32_t intf_id) {
    m_union.sub_4x16.intf_id = intf_id;
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

  ActorID(const ActorID& id) 
    : m_union(id.m_union.u64) {}

  ActorID(uint64_t id = 0) : m_union(id) {}

  ActorID(uint32_t module_id, uint32_t intf_id, uint32_t inst_id)
    : m_union(LOCAL, module_id, intf_id, inst_id) {}

  ActorID(uint32_t node_id, uint32_t module_id,
          uint32_t intf_id, uint32_t inst_id) 
    : m_union(node_id, module_id, intf_id, inst_id) {}

  ActorID& operator = (const ActorID& id) {
    m_union.u64 = id.m_union.u64;
    return *this;
  }

  bool operator == (const ActorID& id) {
    return m_union.u64 == id.m_union.u64;
  }

  bool is_valid() const { return m_union.u64 != 0; }

  std::string to_string() const;

#if !defined(CLIENT_CONTEXT)
  bool is_local() const;
#endif

  static inline const ActorID* convert_from_u64(const uint64_t *u64) {
    return reinterpret_cast<const ActorID*>(u64);
  }

  static inline ActorID* convert_from_u64(uint64_t *u64) {
    return reinterpret_cast<ActorID*>(u64);
  }
};

}

#endif // _MOLA_ACTOR_ID_H_
