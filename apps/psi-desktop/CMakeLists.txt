file(GLOB PSI_APP_SRC CONFIGURE_DEPENDS *.cpp *.h)
list(REMOVE_ITEM PSI_APP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
# main window, docks and pages as a library so the UI tests can drive the real window
qt_add_library(psi_app STATIC ${PSI_APP_SRC})
target_link_libraries(psi_app PUBLIC psi_ui)
target_include_directories(psi_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

qt_add_executable(psi main.cpp)
target_link_libraries(psi PRIVATE psi_app)
set_target_properties(psi PROPERTIES
  OUTPUT_NAME "psi"
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_BUNDLE_NAME "Пси"
  MACOSX_BUNDLE_GUI_IDENTIFIER "ink.psi.cad"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in")
if(APPLE AND EXISTS "${CMAKE_SOURCE_DIR}/build/Psi.icns")
  set_target_properties(psi PROPERTIES MACOSX_BUNDLE_ICON_FILE "Psi.icns")
  target_sources(psi PRIVATE "${CMAKE_SOURCE_DIR}/build/Psi.icns")
  set_source_files_properties("${CMAKE_SOURCE_DIR}/build/Psi.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
