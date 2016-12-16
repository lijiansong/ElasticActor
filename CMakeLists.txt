project(LibMOLA)

cmake_minimum_required(VERSION 2.8)

include_directories(${LibMOLA_SOURCE_DIR}/include)
include_directories(${LibMOLA_SOURCE_DIR}/include/mola)

## enable the C++11 features
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread -D__FILENAME__='\"$(subst ${CMAKE_SOURCE_DIR}/,,$(abspath $<))\"'")

## for perf to obtain the stack call graph
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG_")

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH} ${CMAKE_ROOT}/Modules")
message(STATUS "${CMAKE_MODULE_PATH}")

## test and determine the endian type
include(CPack)
include(TestBigEndian)

TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
if(NOT ${IS_BIG_ENDIAN})
  add_definitions(-D__LITTLE_ENDIAN__)
endif()


set(MAX_ERRORS "5" CACHE string "Max error messages to show")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmax-errors=${MAX_ERRORS}")

option(BUILD_CLIENT_LIB "Enable to build the client api library" on) 

option(BUILD_TESTS "Enable to build the tests programs" off)

option(DEBUG_INFO "Enable to show the console debug info" off)

if(${DEBUG_INFO})
  add_definitions(-DENABLE_DEBUG_INFO)
endif()

option(ENABLE_LOCKFREE_GQ "Enable to use lock-free global queue (experimental)" off)

if (${ENABLE_LOCKFREE_GQ})
  add_definitions(-DENABLE_LOCKFREE_GLOBAL_QUEUE)
endif()

set(ZOOKEEPER_C_API_PATH "/usr/local" CACHE path "The install path of Zookeeper C API lib")

## set the ZooKeeper C API library path
if(EXISTS "${ZOOKEEPER_C_API_PATH}/include/zookeeper/zookeeper.h")
  message(STATUS "Find zookeeperk c api path as ${ZOOKEEPER_C_API_PATH}")
else()
  message(FATAL_ERROR "Zookeeper c api not found")
endif()

## check the arch of current building system
execute_process( COMMAND uname -m COMMAND tr -d '\n' OUTPUT_VARIABLE PLAT_ARCH)

message(STATUS "Platfrom architecture : ${PLAT_ARCH}")

##if(NOT ${PLAT_ARCH} MATCHES "x86_64")
##  add_definitions(-DUSE_UCONTEXT=1)
##endif()

#option(USE_UCONTEXT_API "Enable ucontext based fiber mechnism" off)
#if(${USE_UCONTEXT_API})
#  add_definitions(-DUSE_UCONTEXT=1)
#endif()

option(USE_TCMALLOC "Using google's tc-malloc for memory management" off)
if(${USE_TCMALLOC})
  find_library(TCMALLOC_LIBRARY libtcmalloc.so HINTS /usr/local/lib/ /opt/tc-malloc)
  if(TCMALLOC_LIBRARY)
    message(STATUS "found libtcmalloc as ${TCMALLOC_LIBRARY}")
  else()
    message(FATAL_ERROR "libtcmalloc not found")
  endif()
endif()

include_directories(${ZOOKEEPER_C_API_PATH}/include)

add_subdirectory(src)
add_subdirectory(tools)

install(DIRECTORY 
        "include/mola"
        DESTINATION 
        "include"
        FILES_MATCHING
        PATTERN
        "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
  add_subdirectory(modules)
endif(BUILD_TESTS)

