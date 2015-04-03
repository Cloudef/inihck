include_directories(${inihck_SOURCE_DIR}/lib/chck/src)
add_library(inihck inihck.c)
target_link_libraries(inihck chck_inihck)
install(TARGETS inihck DESTINATION lib)
install(FILES inihck.h DESTINATION include/inihck)

add_executable(ini_test test.c)
target_link_libraries(ini_test inihck)
add_test_ex(ini_test)

add_executable(fuzz_test test.c)
target_link_libraries(fuzz_test inihck)
set_target_properties(fuzz_test PROPERTIES COMPILE_DEFINITIONS FUZZ=1)
add_custom_target(fuzz DEPENDS fuzz_test
   COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/fuzz.bash"
   )

FILE(COPY test.ini DESTINATION .)
