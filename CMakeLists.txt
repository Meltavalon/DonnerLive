# SPDX-License-Identifier: AGPL-3.0-only
cmake_minimum_required(VERSION 3.24)
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
project(DonnerLive VERSION 1.0.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build from the Visual Studio x64 Native Tools command prompt.")
endif()

add_compile_options(/utf-8)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_HELPER_TOOLS ON CACHE BOOL "" FORCE)

# Keep dependency sources and their build products inside the ignored build directory.
include(FetchContent)
FetchContent_Declare(asio_sdk
    URL https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip
    URL_HASH SHA256=d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(asio_sdk)
set(DONNER_ASIO_SDK_ROOT "${asio_sdk_SOURCE_DIR}")
file(READ "${DONNER_ASIO_SDK_ROOT}/LICENSE.txt" asio_license)
if(NOT asio_license MATCHES "General Public License.*Version 3")
    message(FATAL_ERROR "The ASIO SDK must provide its GPLv3 licensing alternative.")
endif()

FetchContent_Declare(juce
    URL https://codeload.github.com/juce-framework/JUCE/zip/refs/tags/9.0.4
    URL_HASH SHA256=16ad4cfca70b73df6352e4e840ba5dc2e755d36b8ab2dd976ef3ecedcd18f6a9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(juce)
set(DONNER_JUCE_ROOT "${juce_SOURCE_DIR}")

if(NOT TARGET juce::juceaide)
    add_executable(juce::juceaide ALIAS juceaide)
endif()

add_library(DonnerLive.ASIO SHARED src/asio/driver.cpp src/asio/entry.cpp)
target_include_directories(DonnerLive.ASIO PRIVATE "${DONNER_ASIO_SDK_ROOT}/common")
target_link_libraries(DonnerLive.ASIO PRIVATE ole32 uuid avrt shell32)
target_link_options(DonnerLive.ASIO PRIVATE /EXPORT:DllGetClassObject /EXPORT:DllCanUnloadNow)

set(engine_sources src/engine/service.cpp src/engine/runtime.cpp src/engine/runtime_audio.cpp)
add_library(DonnerLive.Engine STATIC ${engine_sources})
target_include_directories(DonnerLive.Engine PUBLIC "${DONNER_ASIO_SDK_ROOT}/common")
target_link_libraries(DonnerLive.Engine PUBLIC ole32 uuid propsys shlwapi version avrt advapi32 shell32)

# The resource is generated after the ASIO target, so a clean build needs no prebuilt DLL.
set(asio_library "${CMAKE_BINARY_DIR}/bin/DonnerLive.ASIO.dll")
set_target_properties(DonnerLive.ASIO PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
juce_add_binary_data(DonnerLive.AsioResource NAMESPACE DonnerAsio HEADER_NAME DonnerAsioData.h
    SOURCES "${asio_library}")
add_dependencies(DonnerLive.AsioResource DonnerLive.ASIO)

juce_add_gui_app(DonnerLive.Panel PRODUCT_NAME "DonnerLive" COMPANY_NAME "DonnerLive" VERSION ${PROJECT_VERSION})
set(panel_sources src/panel/juce/main.cpp src/panel/juce/commands.cpp
    src/panel/juce/workbench.cpp src/panel/juce/workbench_rendering.cpp)
target_sources(DonnerLive.Panel PRIVATE ${panel_sources})
set_target_properties(DonnerLive.Panel PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_compile_definitions(DonnerLive.Panel PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(DonnerLive.Panel PRIVATE DonnerLive.Engine DonnerLive.AsioResource
    juce::juce_gui_extra juce::juce_recommended_config_flags)

foreach(target DonnerLive.ASIO DonnerLive.Engine DonnerLive.Panel)
    target_compile_definitions(${target} PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(${target} PRIVATE /W3 /wd4996)
endforeach()
# Cover MSVC installations whose localized /showIncludes output Ninja cannot decode.
file(GLOB_RECURSE project_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")
set_property(SOURCE src/asio/driver.cpp src/asio/entry.cpp ${engine_sources} ${panel_sources}
    APPEND PROPERTY OBJECT_DEPENDS ${project_headers})

install(TARGETS DonnerLive.Panel DonnerLive.ASIO
    RUNTIME DESTINATION . COMPONENT Runtime
    LIBRARY DESTINATION . COMPONENT Runtime
    ARCHIVE DESTINATION lib COMPONENT Development EXCLUDE_FROM_ALL)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION . COMPONENT Runtime)
install(FILES "${DONNER_JUCE_ROOT}/LICENSE.md" "${DONNER_JUCE_ROOT}/JUCE.spdx.json"
    DESTINATION licenses/JUCE COMPONENT Runtime)
install(FILES "${DONNER_ASIO_SDK_ROOT}/LICENSE.txt" LICENSE-GPL-3.0
    DESTINATION licenses/ASIO COMPONENT Runtime)
foreach(module juce_core juce_events juce_data_structures juce_graphics juce_gui_basics juce_gui_extra)
    file(GLOB_RECURSE module_notices RELATIVE "${DONNER_JUCE_ROOT}/modules" LIST_DIRECTORIES FALSE
        "${DONNER_JUCE_ROOT}/modules/${module}/LICENSE*"
        "${DONNER_JUCE_ROOT}/modules/${module}/COPYING*"
        "${DONNER_JUCE_ROOT}/modules/${module}/NOTICE*"
        "${DONNER_JUCE_ROOT}/modules/${module}/COPYRIGHT*"
        "${DONNER_JUCE_ROOT}/modules/${module}/PATENTS*"
        "${DONNER_JUCE_ROOT}/modules/${module}/README*"
        "${DONNER_JUCE_ROOT}/modules/${module}/JUCE_UPSTREAM.txt")
    foreach(notice IN LISTS module_notices)
        get_filename_component(notice_directory "${notice}" DIRECTORY)
        install(FILES "${DONNER_JUCE_ROOT}/modules/${notice}"
            DESTINATION "licenses/JUCE/modules/${notice_directory}" COMPONENT Runtime)
    endforeach()
endforeach()

# CPack supplies the installer; no project-specific installer script is required.
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_GENERATOR NSIS)
set(CPACK_PACKAGE_NAME DonnerLive)
set(CPACK_PACKAGE_VENDOR DonnerLive)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY DonnerLive)
set(CPACK_PACKAGE_FILE_NAME "DonnerLive-${PROJECT_VERSION}-Setup-x64")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/installer")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};Runtime;/")
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
set(CPACK_NSIS_EXECUTABLES_DIRECTORY .)
set(CPACK_PACKAGE_EXECUTABLES DonnerLive DonnerLive)
set(CPACK_NSIS_DISPLAY_NAME DonnerLive)
set(CPACK_NSIS_INSTALLED_ICON_NAME DonnerLive.exe)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
# ASIO uses the 64-bit registry; restore CPack's registry view afterwards.
set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
    SetRegView 64
    WriteRegStr HKLM "SOFTWARE\Classes\CLSID\{6223CC56-3DB6-48D1-8CC0-A3CFB72683BE}" "" "DonnerLive ASIO"
    WriteRegStr HKLM "SOFTWARE\Classes\CLSID\{6223CC56-3DB6-48D1-8CC0-A3CFB72683BE}\InprocServer32" "" "$INSTDIR\DonnerLive.ASIO.dll"
    WriteRegStr HKLM "SOFTWARE\Classes\CLSID\{6223CC56-3DB6-48D1-8CC0-A3CFB72683BE}\InprocServer32" "ThreadingModel" "Apartment"
    WriteRegStr HKLM "SOFTWARE\ASIO\DonnerLive ASIO" "CLSID" "{6223CC56-3DB6-48D1-8CC0-A3CFB72683BE}"
    WriteRegStr HKLM "SOFTWARE\ASIO\DonnerLive ASIO" "Description" "DonnerLive ASIO"
    SetRegView lastused
]=])
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
    SetRegView 64
    ReadRegStr $0 HKLM "SOFTWARE\Classes\CLSID\{6223CC56-3DB6-48D1-8CC0-A3CFB72683BE}\InprocServer32" ""
    StrCmp $0 "$INSTDIR\DonnerLive.ASIO.dll" 0 +3
    DeleteRegKey HKLM "SOFTWARE\ASIO\DonnerLive ASIO"
    DeleteRegKey HKLM "SOFTWARE\Classes\CLSID\{6223CC56-3DB6-48D1-8CC0-A3CFB72683BE}"
    SetRegView lastused
]=])
include(CPack)
