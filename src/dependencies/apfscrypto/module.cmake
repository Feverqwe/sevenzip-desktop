# Only DES is imported; AES, HMAC and PBKDF2 retain their existing SDK owners.
add_library(sevenzip_des_core STATIC
    ${CMAKE_CURRENT_LIST_DIR}/Des.cpp ${CMAKE_CURRENT_LIST_DIR}/TripleDes.cpp)
set_target_properties(sevenzip_des_core PROPERTIES AUTOMOC OFF AUTOUIC OFF
    POSITION_INDEPENDENT_CODE ON)
