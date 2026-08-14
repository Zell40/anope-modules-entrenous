# EntreNous third-party Anope modules.
# Clone this repository into Anope's modules/third/ (for example
# modules/third/entrenous) then re-run ./Config && make && make install.
file(GLOB MODULE_DIRS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*")
list(SORT MODULE_DIRS)
foreach(MODULE_DIR ${MODULE_DIRS})
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_DIR}"
      AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${MODULE_DIR}/CMakeLists.txt")
    add_subdirectory(${MODULE_DIR})
  endif()
endforeach()
