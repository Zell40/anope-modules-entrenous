# AideServ — drop this folder into Anope's modules/third/ (or keep it
# nested under the entrenous collection CMakeLists).
build_module(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/aideserv.cpp)

if(DEFINED CONF_DIR)
  install(FILES aideserv.example.conf DESTINATION ${CONF_DIR})
endif()

if(HAVE_LOCALIZATION)
  set(AIDESERV_PO ${CMAKE_CURRENT_SOURCE_DIR}/language/aideserv.fr_FR.po)
  set(AIDESERV_MO ${CMAKE_CURRENT_BINARY_DIR}/aideserv.fr_FR.mo)
  add_custom_command(OUTPUT ${AIDESERV_MO}
    COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} -c ${AIDESERV_PO} -o ${AIDESERV_MO}
    MAIN_DEPENDENCY ${AIDESERV_PO}
  )
  add_custom_target(aideserv_language DEPENDS ${AIDESERV_MO})
  add_dependencies("aideserv${CMAKE_SHARED_LIBRARY_SUFFIX}" aideserv_language)
  install(DIRECTORY DESTINATION "${LOCALE_DIR}/fr_FR/LC_MESSAGES")
  install(FILES ${AIDESERV_MO} DESTINATION ${LOCALE_DIR}/fr_FR/LC_MESSAGES RENAME aideserv.mo)
endif()
