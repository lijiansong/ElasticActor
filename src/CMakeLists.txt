set(LIBRARY_NAME mola)
set(IO_LIBRARY_NAME "${LIBRARY_NAME}_io")
set(CORE_LIBRARY_NAME "${LIBRARY_NAME}_core")
set(OPT_LIBRARY_NAME "${LIBRARY_NAME}_opt")
set(CLIENT_LIBRARY_NAME "${LIBRARY_NAME}_client")

enable_language(ASM C CXX)

set(CORE_SRC_FILES scheduler.cpp
                   actor.cpp
                   actor_id.cpp
                   dispatcher.cpp
                   module.cpp
                   node_manager.cpp
                   zookeeper.cpp
                   gateway.cpp
                   message.cpp
                   network.cpp)

if(NOT ${USE_UCONTEXT_API})
  add_definitions(-DUSE_HIRT_FIBER_IMPL)
  set(CORE_SRC_FILES ${CORE_SRC_FILES} 
                     fiber.S)
endif()

set(IO_SRC_FILES multiplexer.cpp
                 event_handler.cpp)

set(OPT_SRC_FILES options.cpp)

set(CLIENT_SRC_FILES node_manager.cpp
                     zookeeper.cpp
                     gateway.cpp
                     client.cpp
                     message.cpp)

## set(CMAKE_INSTALL_SO_NO_EXE 0)

add_library(${IO_LIBRARY_NAME}
            SHARED
            ${IO_SRC_FILES})

target_link_libraries(${IO_LIBRARY_NAME})

add_library(${CORE_LIBRARY_NAME}
            SHARED
            ${CORE_SRC_FILES})

target_link_libraries(${CORE_LIBRARY_NAME})

add_library(${OPT_LIBRARY_NAME}
            SHARED
            ${OPT_SRC_FILES})

target_link_libraries(${OPT_LIBRARY_NAME})

if(${BUILD_CLIENT_LIB})
add_library(${CLIENT_LIBRARY_NAME}
            SHARED
            ${CLIENT_SRC_FILES})

target_link_libraries(${CLIENT_LIBRARY_NAME})

set_target_properties(${CLIENT_LIBRARY_NAME} PROPERTIES COMPILE_FLAGS
                      "${CMAKE_CXX_FLAGS} -DCLIENT_CONTEXT")

install(TARGETS
        ${CLIENT_LIBRARY_NAME}
        LIBRARY
        DESTINATION lib)

endif()

install(TARGETS
        ${IO_LIBRARY_NAME} ${CORE_LIBRARY_NAME} ${OPT_LIBRARY_NAME}
        LIBRARY 
        DESTINATION lib)
