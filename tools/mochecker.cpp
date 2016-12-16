#include <sys/types.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <unistd.h>

#include <string>
#include <iostream>

#include "mola/debug.hpp"
#include "mola/node_manager.hpp"

using namespace mola;

static constexpr const char* NO_INIT_ERR = 
  "You may not implement the init callback defined in the module IDL!";

int main(int argc, char** argv) {
  // check the command line option
  if (argc != 2) {
    MOLA_ERROR_TRACE("Usage " << argv[0] << " <module-file>.so");
    exit(-1);
  }
  
  // check the input filename
  std::string input(argv[1]);
  
  auto pos = input.rfind(".");
  if (pos != (input.length() - 3)) {
    MOLA_ERROR_TRACE("The input file must be a *.so fille!");
    exit(-1);
  }

  // check if the input file exists
  struct stat sb;
  if (stat(input.c_str(), &sb) == -1) {
    MOLA_ERROR_TRACE("The input file '" << input << "' not found!");
    exit(-1);
  }

  // create a stub node manager, since the *.so's constructors may be called during open
  auto mgr = NodeManager::initialize(NodeManager::NM_SINGLETON, nullptr);

  std::string undef("undefined symbol: ");

  // try to load this file and resolve all symbols in it
  void *handle = dlopen(input.c_str(), RTLD_NOW);
  if (handle == nullptr) {
    std::string error( dlerror() ), symbol;
    auto pos = error.find(undef);
    
    if (pos != std::string::npos) {
      // find the undefined symbol name
      symbol = error.substr(pos + undef.length());
      pos = symbol.rfind("_wrapper");
      if (pos != std::string::npos) {
        symbol = symbol.substr(0, pos);
        error = undef + symbol;
      }
    }

    error = "Cannot load the module file, '" + error + "'";

    if ((pos = symbol.rfind("__vtable")) != std::string::npos) {
      error = error + "\n" + NO_INIT_ERR;
    }

    MOLA_ERROR_TRACE(error);
    exit(-1);
  }

  dlerror(); // clean all errors

  void *spec = dlsym(handle, "__module_spec");
  auto error = dlerror();
  if (error != nullptr) {
    MOLA_ERROR_TRACE("Cannot find the module spec, '" 
                      << error << "'\n" << NO_INIT_ERR);
    dlclose(handle);
    exit(-1);
  }

  // Ok, the module is correct!
  std::cout << "The module plugin file " << input << " is valid!"<< std::endl;

  dlclose(handle);
  return 0;
}

