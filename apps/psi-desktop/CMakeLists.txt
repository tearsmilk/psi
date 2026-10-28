file(GLOB PSI_APP_SRC CONFIGURE_DEPENDS *.cpp *.h)
if(NOT PSI_WITH_OCCT)
  list(FILTER PSI_APP_SRC EXCLUDE REGEX "model_page\\.(cpp|h)$")   # рабочее пространство «Модель» только с OCCT
endif()
qt_add_executable(psi ${PSI_APP_SRC})
target_link_libraries(psi PRIVATE psi_ui)
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
