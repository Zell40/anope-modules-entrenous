# m_youtube — drop this folder into Anope's modules/third/ (or keep it
# nested under the entrenous collection CMakeLists).
# Needs libcurl (headers + library). yyjson is Anope's vendored copy.
find_package(CURL REQUIRED)
build_module(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/m_youtube.cpp)

if(DEFINED CONF_DIR)
  install(FILES m_youtube.example.conf DESTINATION ${CONF_DIR})
endif()
