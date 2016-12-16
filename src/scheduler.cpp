#include "mola/scheduler.hpp"
#include "mola/work_steal.hpp"
#include "mola/actor.hpp"
#include "mola/multiplexer.hpp"
#include "mola/node_manager.hpp"

using namespace mola;

thread_local AbsolateWorker *AbsolateWorker::current_worker;
thread_local uint32_t AbsolateWorker::seed;

int AbsolateWorker::worker_num;
std::vector<AbsolateWorker::WorkerPtr> AbsolateWorker::workers;

Actor* AbsolateWorker::find_cached_actor(uint32_t key) {
  auto current = Actor::current();
  if (current != nullptr) 
    return current->find_cached_actor(key);
  return m_actor_cache[key];
}

void AbsolateWorker::cache_an_actor(uint32_t key, Actor* actor) {
  auto current = Actor::current();
  if (current != nullptr)
    return current->cache_an_actor(key, actor);
  m_actor_cache[key] = actor;
}

void AsyncIoWorker::run() {
  // init the thread-local var
  Multiplexer::set_backend(m_backend);

  for (;;) {
    m_backend->poll();
  }
}

void DelayMessageWorker::run() {
  m_pool->loop();
}

extern "C" {
  __thread void * __rt_gen_data;
}

void Worker::before_resume(void * ctx) {
  // TODO 
  __rt_gen_data = ctx;
}

std::unique_ptr<WorkerGroup> WorkerGroup::group;

void WorkerGroup::start_io_workers(unsigned io) {
  for (unsigned i = 0; i < io; ++i) {
    auto io_thr = new AsyncIoWorker(Multiplexer::instance(i));
    io_thr->start();
  }
}

WorkerGroup* WorkerGroup::initialize(unsigned num, unsigned io, SchedStrategy* ss) {
  if (group.get() == nullptr) {
    num = (num > 0) ? num : std::thread::hardware_concurrency();
    if (ss == nullptr) {
#if ENABLE_TIMER
      unsigned helpers = 2; // timer thread and delay message thread
#else
      unsigned helpers = 1; // the delay message thread only
#endif // ENABLE_TIMER
      group.reset(new WorkerGroup(num, 
#if 1
                    new LockFreeStrategy<4096>(num, io + helpers)
#else
                    new SimpleStrategy<>(num, io+helpers)
#endif
      ));
    } else {
      group.reset(new WorkerGroup(num, ss));
    }

    // NOTE: we can't start the workers until all modules are loaded!!
    group->init_workers();

    group->start_io_workers(io);
  }
  return group.get();
}

