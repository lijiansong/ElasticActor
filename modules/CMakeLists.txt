include_directories(${LibHIRT_SOURCE_DIR}/include/c)

macro(add_module_file name)
  add_library(${name} MODULE ${name}.c) 
endmacro(add_module_file)

add_module_file("module_A")
add_module_file("module_B")
add_module_file("module_C")

add_module_file("scatter")
add_module_file("gather")
