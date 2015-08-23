# Parse soversion version
string(REGEX MATCHALL "[0-9]+" VERSION_COMPONENTS ${INIHCK_VERSION})
list(GET VERSION_COMPONENTS 0 SOVERSION)

include_directories(${inihck_SOURCE_DIR}/include ${inihck_SOURCE_DIR}/lib/chck/src)

if (INIHCK_BUILD_STATIC)
   add_library(inihck inihck.c)
else ()
   add_definitions(-DINIHCK_BUILD_SHARED)
   add_library(inihck SHARED inihck.c)
endif ()

set_target_properties(inihck PROPERTIES VERSION ${INIHCK_VERSION} SOVERSION ${SOVERSION})
target_link_libraries(inihck chck_inihck)

install(TARGETS inihck DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY "${inihck_SOURCE_DIR}/include/inihck" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if (INIHCK_BUILD_TESTS)
   add_executable(ini_test test.c)
   target_link_libraries(ini_test inihck)
   add_test_ex(ini_test)

   add_executable(fuzz_test test.c)
   target_link_libraries(fuzz_test inihck)
   set_target_properties(fuzz_test PROPERTIES COMPILE_DEFINITIONS FUZZ=1)
   add_custom_target(fuzz DEPENDS fuzz_test
      COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/fuzz.bash"
      )

   file(COPY test.ini DESTINATION .)
endif ()

set(INIHCK_LIBRARIES inihck CACHE STRING "Libraries of inihck" FORCE)
set(INIHCK_INCLUDE_DIRS "${inihck_SOURCE_DIR}/include" CACHE STRING "Include directories of inihck" FORCE)
mark_as_advanced(INIHCK_LIBRARIES INIHCK_INCLUDE_DIRS)
