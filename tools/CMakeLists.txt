set(HIRT_LIB mola)
set(HIRT_IO_LIB "${HIRT_LIB}_io")
set(HIRT_CORE_LIB "${HIRT_LIB}_core")
set(HIRT_OPT_LIB "${HIRT_LIB}_opt")
set(HIRT_CLIENT_LIB "${HIRT_LIB}_client")

link_directories(${LibHIRT_SOURCE_DIR}/src)
link_directories(${ZOOKEEPER_C_API_PATH}/lib)

macro(add_libmola_tool name libraries)
  add_executable(${name}
                 ${name}.cpp)
  set(extra_args ${ARGN})

  target_link_libraries(${name}
                        ${libraries} ${extra_args} zookeeper_mt pthread dl)

  install_targets(/bin ${name})
endmacro(add_libmola_tool)

add_libmola_tool(mochecker ${HIRT_CORE_LIB} ${HIRT_IO_LIB} ${HIRT_OPT_LIB})

if(USE_TCMALLOC)
  get_filename_component(TCMALLOC_LIBRARY_PATH ${TCMALLOC_LIBRARY} PATH)
  link_directories(${TCMALLOC_LIBRARY_PATH})
  add_libmola_tool(moloader tcmalloc ${HIRT_CORE_LIB} ${HIRT_IO_LIB} ${HIRT_OPT_LIB})
else()
  add_libmola_tool(moloader ${HIRT_CORE_LIB} ${HIRT_IO_LIB} ${HIRT_OPT_LIB})
endif()
