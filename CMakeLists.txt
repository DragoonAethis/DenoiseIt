cmake_minimum_required(VERSION 3.20)
project(DenoiseIt C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(RNNOISE REQUIRED rnnoise)
pkg_check_modules(SNDFILE REQUIRED sndfile)

set(CMAKE_C_STANDARD 11)
add_executable(denoiseit main.c)
target_link_libraries(denoiseit ${RNNOISE_LIBRARIES} ${SNDFILE_LIBRARIES})
target_include_directories(denoiseit PUBLIC ${RNNOISE_INCLUDE_DIRS} ${SNDFILE_INCLUDE_DIRS})
