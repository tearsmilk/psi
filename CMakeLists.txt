cmake_minimum_required(VERSION 3.21)
project(PSI VERSION 0.1.0 LANGUAGES CXX)

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
option(PSI_WITH_OCCT "Build the 3D modules on Open CASCADE (stage 02+)" OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets PrintSupport Svg)
find_package(ZLIB REQUIRED)

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
     io/native/*.cpp io/native/*.h io/dxf/*.cpp io/dxf/*.h)
add_library(psi_core STATIC ${PSI_CORE_SRC})
target_include_directories(psi_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(psi_core PUBLIC Qt6::Core ZLIB::ZLIB)

# ---- psi_ui: rendering, draft tools, PDF, theme and widgets
file(GLOB_RECURSE PSI_UI_SRC CONFIGURE_DEPENDS
     rendering/*.cpp rendering/*.h modules/draft/*.cpp modules/draft/*.h
     io/pdf/*.cpp io/pdf/*.h ui/*.cpp ui/*.h)
add_library(psi_ui STATIC ${PSI_UI_SRC})
target_link_libraries(psi_ui PUBLIC psi_core Qt6::Gui Qt6::Widgets Qt6::PrintSupport Qt6::Svg)

add_subdirectory(apps/psi-desktop)

if(PSI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
