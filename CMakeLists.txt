cmake_minimum_required(VERSION 3.27)
project(spicy-kvm C)

find_package(PkgConfig)
pkg_check_modules(SAMPLERATE REQUIRED IMPORTED_TARGET samplerate)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
pkg_check_modules(SPICE_PROTOCOL REQUIRED IMPORTED_TARGET spice-protocol)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET systemd)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

add_compile_options(
  "-Wall"
  "-Wextra"
  "-Wno-unused-parameter"
  "-Wno-sign-compare"
)

add_subdirectory("${PROJECT_SOURCE_DIR}/lib/PureSpice" "${CMAKE_BINARY_DIR}/PureSpice")
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/LookingGlass" "${CMAKE_BINARY_DIR}/LookingGlass")

add_executable(spicy-kvm
  src/audio.c
  src/audiodev.c
  src/ddcci.c
  src/input.c
  src/main.c
)

target_link_libraries(spicy-kvm
  Threads::Threads
  PkgConfig::SAMPLERATE
  PkgConfig::PIPEWIRE
  PkgConfig::LIBEVDEV
  PkgConfig::SPICE_PROTOCOL
  purespice
  lg_common
  systemd
  m
)

install(TARGETS spicy-kvm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
