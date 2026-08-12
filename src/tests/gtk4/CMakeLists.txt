# GTK4 port behavior-verification suite (g_test-based, GTK4-only).
# The port's workbench, so it builds regardless of BUILD_TESTING (the
# cmocka unit tests in ../unittests stay behind that flag).  Run:
# ctest --test-dir build-gtk4 -R gtk4
# (or ./build-gtk4/bin/darktable-test-gtk4 directly; use
# `--verbose` / `-p` for test-by-test output).

include_directories(${DARKTABLE_BINDIR})

add_executable(darktable-test-gtk4
  test_gtk4.c
  test_harness.c
  test_menu.c
  test_container.c
  test_controller.c
  test_bauhaus.c
  test_iopheader.c
  test_libheader.c
  test_gesture.c
  test_panel.c
  test_view.c
)

set_target_properties(darktable-test-gtk4 PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(darktable-test-gtk4 lib_darktable)

if(WIN32)
  _detach_debuginfo(darktable-test-gtk4 bin)
else()
  set_target_properties(darktable-test-gtk4
                        PROPERTIES
                        INSTALL_RPATH ${RPATH_ORIGIN}/${REL_BIN_TO_LIBDIR}
                        RUNTIME_OUTPUT_DIRECTORY ${DARKTABLE_BINDIR})
endif(WIN32)

install(TARGETS darktable-test-gtk4 DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT DTApplication)

enable_testing()
add_test(NAME darktable-test-gtk4 COMMAND darktable-test-gtk4)
# The suite is headless-capable (widget construction + signal emission);
# on this machine GDK auto-connects to the user's Wayland session, which is
# where the display-dependent tests will run.  GC-style logging off so
# GTK's own teardown chatter does not fail tests; g_test already fails on
# g_critical.
set_tests_properties(darktable-test-gtk4 PROPERTIES
  ENVIRONMENT "G_MESSAGES_DEBUG=none"
  TIMEOUT 60)
