include(Prebuilt)

include(LibFindMacros)
# GTK4 port: colord-gtk4 is the GTK4 build of colord-gtk (same API/header).
# The GTK3 colord-gtk must never be linked into a GTK4 process — GTK4 aborts
# at init when GTK2/3 symbols are present ("GTK 2/3 symbols detected").
libfind_pkg_check_modules(ColordGTK colord-gtk4)
foreach(i ${ColordGTK_LIBRARIES})
  find_library(_colordgtk_LIBRARY NAMES ${i} HINTS ${ColordGTK_LIBRARY_DIRS})
  LIST(APPEND ColordGTK_LIBRARY ${_colordgtk_LIBRARY})
  unset(_colordgtk_LIBRARY CACHE)
endforeach(i)
set(ColordGTK_LIBRARIES ${ColordGTK_LIBRARY})
unset(ColordGTK_LIBRARY CACHE)

if(ColordGTK_FOUND)
  set(ColordGTK ON CACHE BOOL "Build with libcolord-gtk support.")
endif(ColordGTK_FOUND)
