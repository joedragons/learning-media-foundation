cmake_minimum_required(VERSION 3.22)
project(learning-media-foundation VERSION 0.3 LANGUAGES CXX)

# option(BUILD_SHARED_LIBS "https://cmake.org/cmake/help/latest/variable/BUILD_SHARED_LIBS.html" ON)
option(BUILD_TESTING "Build test program" OFF) # include(CTest)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INSTALL_UCRT_LIBRARIES true)
string(COMPARE EQUAL ${CMAKE_BUILD_TYPE} Debug CMAKE_INSTALL_DEBUG_LIBRARIES)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(winml_sdk_helpers)
include(winml_cppwinrt)

find_package(Threads REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

enable_testing()
message(STATUS "Using commit: ${GIT_COMMIT_ID}")
message(STATUS "Using system: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "Using compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Windows SDK")
message(STATUS "  ${sdk_version}") # expect CMAKE_SYSTEM_VERSION
message(STATUS "  ${sdk_folder}")
message(STATUS "  ${sdk_include_folder}")
message(STATUS "Visual Studio")
message(STATUS "  ${CMAKE_GENERATOR_INSTANCE}")
message(STATUS "  ${VSTEST_INCLUDE_DIR}")

list(APPEND hdrs
    test/mf_scheduler.hpp
    test/mf_transform.hpp
)

add_library(media0 SHARED
    ${hdrs}
    test/mf_scheduler.cpp
    test/mf_transform.cpp
    test/test_mf_scheduler.cpp
)

# <VCProjectVersion>17.0</VCProjectVersion>
# <ProjectSubType>NativeUnitTestProject</ProjectSubType>
set_target_properties(media0
PROPERTIES
    CXX_STANDARD 17
    WINDOWS_EXPORT_ALL_SYMBOLS True
    VS_GLOBAL_ProjectSubType NativeUnitTestProject
    VS_GLOBAL_VCProjectVersion 17.0
    PUBLIC_HEADER "${hdrs}"
)

target_include_directories(media0
PRIVATE
    ${VSTEST_INCLUDE_DIR}
)

target_compile_definitions(media0
PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX # NOGDI
)

target_compile_options(media0
PRIVATE
    /Zc:__cplusplus
    /W4
)

target_link_libraries(media0
PRIVATE
    WindowsApp mswsock ws2_32
    mf mfplat mfplay mfreadwrite mfuuid wmcodecdspuuid # Media Foundation SDK
    fmt::fmt-header-only spdlog::spdlog_header_only
)

target_link_options(media0
PRIVATE
    /subsystem:Windows
)

install(TARGETS media0
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES $<TARGET_PDB_FILE:media0> DESTINATION ${CMAKE_INSTALL_BINDIR} OPTIONAL)

set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "luncliff@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION "https://github.com/luncliff/media0")
set(CPACK_RESOURCE_FILE_README ${PROJECT_SOURCE_DIR}/readme.md)
set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE)
set(CPACK_GENERATOR "ZIP")
include(CPack)

