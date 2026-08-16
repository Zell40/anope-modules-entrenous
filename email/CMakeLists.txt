# French Anope mail templates (plain text + HTML). Copied to conf/email/.
if(DEFINED CONF_DIR)
  install(FILES
    registration.txt
    registration.html
    password_reset.txt
    password_reset.html
    email_change.txt
    email_change.html
    memo.txt
    memo.html
    DESTINATION ${CONF_DIR}/email
  )
endif()
