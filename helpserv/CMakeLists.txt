# HelpServ — drop this folder into Anope's modules/third/ (or keep it
# nested under the entrenous collection CMakeLists).
# Client bots AideMoi / SignalMoi are created from the example conf.
build_module(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/helpserv.cpp)

if(DEFINED CONF_DIR)
  install(FILES helpserv.example.conf DESTINATION ${CONF_DIR})
endif()

if(HAVE_LOCALIZATION)
  set(HELPSERV_PO ${CMAKE_CURRENT_SOURCE_DIR}/language/helpserv.fr_FR.po)
  set(HELPSERV_MO ${CMAKE_CURRENT_BINARY_DIR}/helpserv.fr_FR.mo)
  add_custom_command(OUTPUT ${HELPSERV_MO}
    COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} -c ${HELPSERV_PO} -o ${HELPSERV_MO}
    MAIN_DEPENDENCY ${HELPSERV_PO}
  )
  add_custom_target(helpserv_language DEPENDS ${HELPSERV_MO})
  add_dependencies("helpserv${CMAKE_SHARED_LIBRARY_SUFFIX}" helpserv_language)
  install(DIRECTORY DESTINATION "${LOCALE_DIR}/fr_FR/LC_MESSAGES")
  install(FILES ${HELPSERV_MO} DESTINATION ${LOCALE_DIR}/fr_FR/LC_MESSAGES RENAME helpserv.mo)
endif()
