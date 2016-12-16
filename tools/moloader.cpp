#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <cstring>

#include "mola/debug.hpp"
#include "mola/options.hpp"
#include "mola/multiplexer.hpp"
#include "mola/module.hpp"
#include "mola/node_manager.hpp"
#include "mola/dispatcher.hpp"

using namespace mola;
using namespace mola::tools;

static Option nametable[] = {
  /* the hostname or ip address for the gateway server */
  { "hostname", STR },

  /* the port for the gateway server */
  { "port", NUM },

  /* where to load the modules' *.so */
  { "modules-load-path", STR },

  /* which modules to load, if more than one module,
   * a `,' should be provided between two modules .*/
  { "modules", STR },
  
  /* the namespace of this project */
  { "namespace", STR },

  /* the zookeeper manager nodes' address,
   * we using zookeeper for service-discovering. */
  { "zk-address", STR },

  /* reset the zookeeper server before connected */
  { "zk-reset", FLAG },

  /* the number of scheduler threads */
  { "worker-thread-num", NUM },

  /* the number of async io poller threads */
  { "async-io-thread-num", NUM },

  /* working path */
  { "working-path", STR },

  /* run as a daemon service */
  { "daemon", FLAG },

  /* log file name */
  { "log", STR },

  /* PID file name */
  { "pid-file", STR },

  /* must end with this !! */
  { nullptr, 0 }
};

static const char * default_tbl[] = {
  "hostname=localhost", /* default hostname */
  "port=65344", /* default port number */
  "modules-load-path=.", /* default modules loading path */
  "modules=", /* no default load module */
  "namespace=MOLA", /* default namespace is MOLA */
  "zk-address=localhost:2346=", /* default zookeeper address */
  "nozk-reset", /* not reset the zk server default */
  "worker-thread-num=0", /* zero means to using all hardware cores/threads */
  "async-io-thread-num=1", /* default using one thread to do polling */
  "working-path=.", /* default working dir is where you run moloader */
  "nodaemon",    /* not run as daemon default */
  "log=", /* stdout/stderr default */
  "pid-file=moloader.PID.",  /* default PID file prefix name */
  NULL,
};

class DynamicLoader {

  static int str2vec(const std::string& str, 
                     std::vector<std::string> &vec, 
                     char delimiter = ':') {
    std::size_t cur_pos = 0, pos;
    while ((pos = str.find(delimiter, cur_pos)) != std::string::npos) {
      vec.push_back(str.substr(cur_pos, pos-cur_pos));
      cur_pos = pos + 1; // escape the ','
      if (cur_pos == str.length()) break;
    }

    if (cur_pos != str.length())
      vec.push_back(str.substr(cur_pos));
    return vec.size();
  }

public:
  DynamicLoader(const char* path)  {
    str2vec(path, m_path);
  }

  virtual ~DynamicLoader() {}

  int load_all(const char *list) {
    int num_loaded = 0;
    std::vector<std::string> vec;
    if (0 == str2vec(list, vec)) return 0;

    /* need to lock the distribute lock during
     * loading & register the modules */
    auto nmgr = NodeManager::instance();
    nmgr->lock(0);

    for (auto mo : vec) {
      if (load_one(mo)) {
        ++num_loaded;
      } else {
        MOLA_ERROR_TRACE("cannot load the module \"" << mo << "\"!");
        break;
      }
    }
    
    nmgr->unlock(0);

    return num_loaded;
  }

private:
  bool load_one(const std::string& mo) {
    struct stat sb;

    std::vector<std::string> argv;
    str2vec(mo, argv, ',');

    std::string basename = argv[0];
    argv.erase(argv.begin());

    for (auto p : m_path) {
      std::string filename = p + "/" + basename;
      if (stat(filename.c_str(), &sb) == -1)
        continue; // file not found !!

      if (Module::dynload_module(filename.c_str(), argv))
        return true;
    }

    return false;
  }

  std::vector<std::string> m_path; 
};

/* a simple module loader and initiator for testing */
int main(int argc, char **argv) {
  Options opts (nametable, default_tbl);
  opts.process_options(argc, argv);

  /* redirect the stdout/stderr if log file is specific */
  auto log = opts.option("log")->str;
  std::ofstream out;
  
  if (log != nullptr && strlen(log) > 0) {
    out.open(log);
    std::cout.rdbuf(out.rdbuf());
    std::cerr.rdbuf(out.rdbuf());
  }

  /* need to chdir before running */
  auto dir = opts.option("working-path");
  if (strcmp(dir->str,  ".")) {
    if (chdir(dir->str) != 0) {
      MOLA_ERROR_TRACE("cannot change current directory to " << dir->str << "!");
    }
  }

  /* need to run as a daemon service */
  if (opts.option("daemon")->flag) {
    MOLA_ASSERT( 0 == daemon(/*nochdir=*/1, /*noclose=*/1) );

    /* create the PID file to record its current PID */
    auto pid = ::getpid();
    std::stringstream ss;

    ss << opts.option("pid-file")->str 
       << static_cast<uint64_t>(pid);

    std::ofstream PID(ss.str());
    PID << static_cast<uint64_t>(pid) << std::endl;
    PID.close();
  }

  /* init the async IO poller backend */
  auto opt_aio = opts.option("async-io-thread-num");
  MOLA_ASSERT(opt_aio->num > 0);
  Multiplexer::initialize(opt_aio->num);

  /* init the scheduler */
  auto opt_num_thr = opts.option("worker-thread-num");
  auto wg = WorkerGroup::initialize(opt_num_thr->num, opt_aio->num);

  /* init the default load-balance algorithm */
  Dispatcher::initialize();

  /* init the Zookeeper based node manager */
  auto mgr = 
    NodeManager::initialize(NodeManager::NM_ZOOKEEPER, 
                            opts.option("namespace")->str);

  auto opt_host = opts.option("hostname");
  auto opt_port = opts.option("port");
  
  MOLA_ASSERT(opt_host->str != nullptr && opt_port->num > 0);
  
  /* start the local gateway node tcp server */
  mgr->create_local_node(opt_host->str, opt_port->num);

  /* try to connect to the zookeeper based service discover server */
  auto opt_zkhost = opts.option("zk-address");
  MOLA_ASSERT(opt_zkhost->str != nullptr);
  auto opt_zkreset = opts.option("zk-reset");

  if (mgr->connect(opt_zkhost->str, opt_zkreset->flag) != 0) {
    MOLA_FATAL_TRACE("cannot connect the node manager "
                   << opt_zkhost->str);
    exit(-1);
  }

  /* try to load the module .so files */
  auto opt_mo = opts.option("modules");
  MOLA_ASSERT(opt_mo->str != nullptr && strlen(opt_mo->str) > 0);
  
  auto opt_mo_path = opts.option("modules-load-path");
  DynamicLoader dl(opt_mo_path->str);

  int num = dl.load_all(opt_mo->str);
  if (num <= 0) {
    MOLA_FATAL_TRACE("cannot find the `*.so' files, loader will stop");
    exit(-1);
  }

  /* try to join my node to the zk server */
  if (mgr->join(nullptr, 0) != 0) {
    MOLA_FATAL_TRACE("cannot join to zookeeper server !");
    exit(-1);
  }

  /* all modules are loaded, we can start the scheduler safely now !!*/
  wg->start_all();

  /* ignore the SIGPIPE, since it will cause the crash when peer down !!*/
  signal(SIGPIPE, SIG_IGN);

  /* flush the log file here !! */
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (out.is_open()) out.flush();
  }
  
  /* make my node leave from the zookeeper server .. */
  mgr->leave();

  return 0;
}
