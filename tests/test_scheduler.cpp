#include <iostream>
#include <thread>

#include "mola/scheduler.hpp"

using namespace mola;

class TestTask : public WorkItem {
  int cnt;
public:
  TestTask(uint64_t tid) 
    : WorkItem(ActorID(tid)), cnt(0) {}
  virtual ~TestTask() {}

  ExecStatus execute(Worker *worker) override {
    if (cnt++ < 4) {
      std::cout << std::hex << get_id().u64() << std::flush;
      return RESUMABLE;
    }
    return EXIT;
  }
};


int main() {
  WorkerGroup::initialize();

  auto wg = WorkerGroup::instance();
  wg->start_all();

  for (int i = 0; i < 16; ++i) {
    wg->enqueue(new TestTask(i));
  }

  for (;;)
    std::this_thread::yield();

  return 0;
}
