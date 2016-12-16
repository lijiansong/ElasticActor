set(HIRT_LIB mola)
set(HIRT_IO_LIB "${HIRT_LIB}_io")
set(HIRT_CORE_LIB "${HIRT_LIB}_core")
set(HIRT_CLIENT_LIB "${HIRT_LIB}_client")

link_directories(${LibHIRT_SOURCE_DIR}/src)
link_directories(${ZOOKEEPER_C_API_PATH}/lib)

macro(add_libmola_test name libraries)
  add_executable(${name}.run
                 ${name}.cpp)
  set(extra_args ${ARGN})

  target_link_libraries(${name}.run
                        ${libraries} ${extra_args} zookeeper_mt pthread dl)
endmacro(add_libmola_test)

macro(add_client_test name libraries)
  set(extra_args ${ARGN})
  add_libmola_test(${name} ${libraries} ${extra_args})

  set_target_properties(${name}.run PROPERTIES COMPILE_FLAGS
                        "${CMAKE_CXX_FLAGS} -DCLIENT_CONTEXT")
endmacro(add_client_test)

add_libmola_test(test_connection ${HIRT_IO_LIB})
add_libmola_test(test_scheduler ${HIRT_CORE_LIB} ${HIRT_IO_LIB})
add_libmola_test(test_zookeeper ${HIRT_CORE_LIB} ${HIRT_IO_LIB})
add_libmola_test(test_zookeeper2 ${HIRT_CORE_LIB} ${HIRT_IO_LIB})
add_libmola_test(test_actor ${HIRT_CORE_LIB} ${HIRT_IO_LIB})

if(${BUILD_CLIENT_LIB})
  add_client_test(test_client ${HIRT_CLIENT_LIB} ${HIRT_IO_LIB})
  add_client_test(test_scatter-gather ${HIRT_CLIENT_LIB} ${HIRT_IO_LIB})
endif()
