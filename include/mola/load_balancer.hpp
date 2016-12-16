#ifndef _MOLA_LOADBALANCER_H_
#define _MOLA_LOADBALANCER_H_

#include <random>
#include <memory>

#include "mola/scheduler.hpp"
#include "mola/node_manager.hpp"

namespace mola {

class LoadBalanceStrategy {
public:
  using task_t = 
    std::function<int(NodeManager::NodePtr&, MessageSafePtr&)>;

  virtual NodeManager::NodePtr select_one_node(NodeManager::NodeSet&) = 0;
  virtual NodeManager::NodePtr select_one_node(NodeManager::NodeSet&, size_t) = 0;
  virtual int scatter_to_nodes(NodeManager::NodeSet& candidate,
                MessageGroup& group, task_t task) = 0;
};

// ---- A simple random select strategy ---- //
class RandomStrategy : public LoadBalanceStrategy {
public:
  RandomStrategy() { /* TODO */ }
  ~RandomStrategy() { /* TODO*/ }

  NodeManager::NodePtr select_one_node(
                         NodeManager::NodeSet& candidates, size_t size) override {
    return candidates[ random(size) ];
  }

  NodeManager::NodePtr select_one_node(
                        NodeManager::NodeSet& candidates) override {
    return candidates[ random(candidates.size()) ];
  }

  int scatter_to_nodes(NodeManager::NodeSet& candidates,
                       MessageGroup& group, task_t task) override {
    int rc = 0;
    auto size = candidates.size();
    auto ng = group.size();
    
    MOLA_ASSERT(size > 0);

    if (size < ng) {
      uint32_t r = AbsolateWorker::random();
      for (int i = 0; i < ng; ++i, ++r) {
        if (task(candidates[r % size], group[i]) == 0) rc++;
      }
      return rc;
    }
    
    for (int i = 0; i < ng; ++i) {
      auto node = select_one_node(candidates, size);
      if (task(node, group[i]) == 0) rc++;
    }
    return rc; // total success
  }

private:
  uint32_t random(uint32_t max, uint32_t min) {
    MOLA_ASSERT(max > min);
    return random(max - min) + min;
  }
  
  uint32_t random(uint32_t max) {
    MOLA_ASSERT(max > 0);
    return AbsolateWorker::random() % max; 
  }
};


// ---- TODO : more strategies ---- //
//
}
#endif // _MOLA_LOADBALANCER_H_
