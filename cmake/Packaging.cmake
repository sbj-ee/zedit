# CPack packaging: a .deb on Linux, a .dmg (via the DragNDrop generator,
# wrapping a real .app bundle) on macOS. Everything zedit depends on is
# built as a static library (see FetchDeps.cmake's BUILD_SHARED_LIBS OFF),
# so the packaged binary only pulls in ordinary system libraries -- no
# bundled .so/.dylib files to manage.

set(CPACK_PACKAGE_NAME "zedit")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Stephen B. Johnson")
set(CPACK_PACKAGE_CONTACT "Stephen B. Johnson <49662809+sbj-ee@users.noreply.github.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/sbj-ee/zedit")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "A modal text editor with gedit-style chrome and neovim-style editing")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# Tagging our own install() rules with a COMPONENT and restricting CPack to
# just that component keeps the package to what we actually intend to ship.
# Without this, CPack sweeps in *every* install() rule anywhere in the
# build tree -- including ones a FetchContent'd dependency's own
# CMakeLists.txt registers for itself (tree-sitter installs its own
# headers/static-lib/pkgconfig file, none of which end users installing
# zedit via apt should get dropped into their /usr/include or /usr/lib).
set(CPACK_DEB_COMPONENT_INSTALL YES)
set(CPACK_COMPONENTS_ALL zedit)
# Component-based installs otherwise suffix both the package name and
# file name with the component ("zedit-zedit", "zedit-0.1.0-Linux-
# zedit.deb") -- overridden back to the plain form since there's only
# ever the one component.
set(CPACK_DEBIAN_ZEDIT_PACKAGE_NAME "zedit")
set(CPACK_DEBIAN_ZEDIT_FILE_NAME "zedit-${PROJECT_VERSION}-Linux-amd64.deb")

if(UNIX AND NOT APPLE)
  set(CPACK_GENERATOR "DEB")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")
  set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
  set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
  # Auto-detects the real shared-library dependencies (libc, libstdc++,
  # libGL, libX11 and/or libwayland-client via GLFW, ...) by inspecting
  # the built binary with dpkg-shlibdeps, rather than hand-listing them
  # and risking it drifting out of sync with what's actually linked.
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

  # Installed layout matches the README's manual "optional install"
  # instructions (see assets/linux/zedit.desktop) -- packaging just
  # automates what those steps do by hand.
  install(TARGETS zedit RUNTIME DESTINATION bin COMPONENT zedit)
  install(FILES ${CMAKE_SOURCE_DIR}/assets/linux/zedit.desktop
          DESTINATION share/applications
          COMPONENT zedit)
  install(FILES ${CMAKE_SOURCE_DIR}/assets/logo/zedit-128.png
          DESTINATION share/icons/hicolor/128x128/apps
          RENAME zedit.png
          COMPONENT zedit)
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(CPACK_DMG_VOLUME_NAME "zedit ${PROJECT_VERSION}")
  set(CPACK_BUNDLE_NAME "zedit")
  set(CPACK_BUNDLE_ICON "${CMAKE_SOURCE_DIR}/assets/logo/zedit.icns")
  # zedit's own executable target (frontend_imgui/CMakeLists.txt) sets
  # MACOSX_BUNDLE TRUE and the icon resource when APPLE, so this just
  # installs that already-assembled .app bundle into the .dmg's root.
  install(TARGETS zedit BUNDLE DESTINATION . COMPONENT zedit)
endif()

include(CPack)
