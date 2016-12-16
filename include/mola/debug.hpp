#ifndef _MOLA_DEBUG_H_
#define _MOLA_DEBUG_H_

#include <execinfo.h>

#include <cassert>
#include <cstdlib>
#include <iostream>

#define BOLD "\033[1m"
#define RED "\033[31m"
#define GREEN "\033[32m"
#define END "\033[0m" << std::endl

#define BT_SIZE 100

#define MOLA_ASSERT(A) \
  do {\
    if (!(A)) {\
      std::cerr << BOLD << __func__ << ":" << __FILE__ << ":" << __LINE__\
                << ": Assertion \'" << #A << "\' failed" << END;\
      static __thread void *buffer[BT_SIZE];\
      int nframes = ::backtrace(buffer, BT_SIZE);\
      char **strings = ::backtrace_symbols(buffer, nframes);\
      if (strings != NULL) {\
        for (int i = 0; i < nframes; ++i)\
          std::cerr << "  " << strings[i] << std::endl;\
        free(strings);\
      }\
      ::abort();\
    }\
 } while (0)
//#define ENABLE_DEBUG_INFO
#ifndef ENABLE_DEBUG_INFO
# define MOLA_LOG_TRACE(...)
#else
# define MOLA_LOG_TRACE(...) {\
    std::cerr << GREEN << "[INFO] " << __func__ << ":" << __FILENAME__ << ":"\
              << __LINE__  << ": >>> " << __VA_ARGS__ << END; }
#endif // ENABLE_DEBUG_INFO

# define MOLA_ALTER_TRACE(...) {\
    std::cout << BOLD << "[ALTER] " << __func__ << ":" << __FILENAME__ << ":"\
              << __LINE__ << ": >>> " << __VA_ARGS__ << END; }

# define MOLA_ERROR_TRACE(...) {\
    std::cerr << RED << "[ERROR] " <<  __func__ << ":" << __FILENAME__ << ":"\
              << __LINE__  << ": >>> " << __VA_ARGS__ << END; }

# define MOLA_FATAL_TRACE(...) {\
    MOLA_ERROR_TRACE(__VA_ARGS__);\
    MOLA_ASSERT(0); }

#endif // _MOLA_DEBUG_H_
