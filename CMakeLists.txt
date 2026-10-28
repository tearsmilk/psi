cmake_minimum_required(VERSION 3.21)
project(PSI VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(PSI_BUILD_TESTS "Build the test suite" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets PrintSupport Svg)
find_package(ZLIB REQUIRED)

# ---- Open CASCADE (stage 02 «Модель»): on by default when OCCT is found, off otherwise («Собрано без OCCT»)
find_package(OpenCASCADE QUIET)
if(OpenCASCADE_FOUND)
  set(PSI_OCCT_DEFAULT ON)
else()
  set(PSI_OCCT_DEFAULT OFF)
endif()
option(PSI_WITH_OCCT "Build the 3D modules on Open CASCADE (stage 02+)" ${PSI_OCCT_DEFAULT})
if(PSI_WITH_OCCT AND NOT OpenCASCADE_FOUND)
  find_package(OpenCASCADE REQUIRED)
endif()
if(PSI_WITH_OCCT)
  message(STATUS "PSI: Open CASCADE ${OpenCASCADE_VERSION} found in ${OpenCASCADE_INSTALL_PREFIX}")
else()
  message(STATUS "PSI: building without Open CASCADE (workspace «Модель» disabled)")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT PSI_IPO OUTPUT PSI_IPO_MSG)
  if(PSI_IPO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
endif()

add_compile_options(-Wall -Wextra)
add_compile_definitions(PSI_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)

# ---- psi_core: document model, geometry, units, commands, sheets, .psi and DXF (Qt Core only)
file(GLOB_RECURSE PSI_CORE_SRC CONFIGURE_DEPENDS
     core/*.cpp core/*.h modules/drawing/*.cpp modules/drawing/*.h
     modules/sketcher/sketch.cpp modules/sketcher/sketch.h modules/interior/building.cpp modules/interior/building.h
     io/native/*.cpp io/native/*.h io/dxf/*.cpp io/dxf/*.h)
add_library(psi_core STATIC ${PSI_CORE_SRC})
target_include_directories(psi_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(psi_core PUBLIC Qt6::Core ZLIB::ZLIB)

# ---- psi_model: parametric 3D on OCCT (kernel facade, feature tree, STEP / STL). Only kernel.cpp / step.cpp / stl.cpp see OCCT headers.
if(PSI_WITH_OCCT)
  file(GLOB_RECURSE PSI_MODEL_SRC CONFIGURE_DEPENDS modules/model/*.cpp modules/model/*.h io/step/*.cpp io/step/*.h io/stl/*.cpp io/stl/*.h)
  add_library(psi_model STATIC ${PSI_MODEL_SRC})
  target_link_libraries(psi_model PUBLIC psi_core)
  target_compile_definitions(psi_model PUBLIC PSI_WITH_OCCT=1)
  # only the toolkits we use; names differ between OCCT 7.6/7.7 (TKSTEP*, TKSTL) and 7.8+ (TKDESTEP, TKDESTL)
  set(PSI_OCCT_TK TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool TKShHealing TKFillet TKOffset TKMesh TKXSBase)
  if(TARGET TKDESTEP)
    list(APPEND PSI_OCCT_TK TKDESTEP TKDESTL TKDE)
  else()
    list(APPEND PSI_OCCT_TK TKSTEP TKSTEPBase TKSTEPAttr TKSTEP209 TKSTL)
  endif()
  target_link_libraries(psi_model PRIVATE ${PSI_OCCT_TK})
  target_include_directories(psi_model SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
  # OCCT headers are third-party: silence their warnings only in the files that include them
  set_source_files_properties(modules/model/kernel.cpp io/step/step.cpp io/stl/stl.cpp PROPERTIES COMPILE_OPTIONS "-Wno-deprecated-declarations;-Wno-unused-parameter")
endif()

# ---- psi_ui: rendering, draft tools, PDF, theme and widgets
file(GLOB_RECURSE PSI_UI_SRC CONFIGURE_DEPENDS
     rendering/*.cpp rendering/*.h modules/draft/*.cpp modules/draft/*.h
     modules/sketcher/sketch_view.cpp modules/sketcher/sketch_view.h modules/interior/interior_view.cpp modules/interior/interior_view.h
     io/pdf/*.cpp io/pdf/*.h ui/*.cpp ui/*.h)
if(NOT PSI_WITH_OCCT)
  list(FILTER PSI_UI_SRC EXCLUDE REGEX "rendering/view3d\\.(cpp|h)$")   # 3D-вид нужен только с ядром
endif()
add_library(psi_ui STATIC ${PSI_UI_SRC})
target_link_libraries(psi_ui PUBLIC psi_core Qt6::Gui Qt6::Widgets Qt6::PrintSupport Qt6::Svg)
if(PSI_WITH_OCCT)
  target_link_libraries(psi_ui PUBLIC psi_model)
endif()

add_subdirectory(apps/psi-desktop)

if(PSI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
