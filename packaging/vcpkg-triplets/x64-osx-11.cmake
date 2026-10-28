# Оверлей-триплет vcpkg для Пси: Intel x64, macOS 11.0 (как и у Qt / самого приложения), статические библиотеки —
# OCCT вшивается в psi, в .dmg не нужны libTK*.dylib, check_min_macos.py проверяет только наш бинарь и Qt.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 11.0)
set(VCPKG_BUILD_TYPE release)
