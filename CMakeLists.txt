cmake_minimum_required(VERSION 3.20)
project(CodiumBlocks VERSION 1.0.1 LANGUAGES CXX)

option(CODIUM_BLOCKS_ENABLE_CODEBLOCKS_SDK "Expose an optional Code::Blocks SDK include root to the native target" OFF)
set(CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT "" CACHE PATH "Code::Blocks SDK installation or source root")
option(CODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER "Build the isolated Code::Blocks SDK host adapter when a usable SDK is found" ON)
set(CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR "" CACHE PATH "Code::Blocks data directory containing resources.zip")
set(CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR "" CACHE PATH "Code::Blocks plugin directory containing matched native plugins")
set(CODIUM_BLOCKS_CODEBLOCKS_COMPILER_PLUGIN "" CACHE FILEPATH "Explicit Code::Blocks Compiler plugin path")
set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PLUGIN "" CACHE FILEPATH "Optional explicit Code::Blocks Debugger plugin path")
option(CODIUM_BLOCKS_ENABLE_CODEBLOCKS_DEBUGGERGDB_PROVIDER
    "Build the separately compiled private DebuggerGDB data provider" OFF)
set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_DIR "" CACHE PATH
    "Code::Blocks plugins/debuggergdb source directory matching the installed SDK")
set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION "" CACHE STRING
    "Exact Code::Blocks source revision used to build the DebuggerGDB provider")
set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_TAG "" CACHE STRING
    "Explicit ABI/build tag shared by the Code::Blocks SDK and DebuggerGDB provider")
set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MAJOR 2 CACHE STRING
    "Code::Blocks SDK major version used by the private provider")
set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MINOR 23 CACHE STRING
    "Code::Blocks SDK minor version used by the private provider")
set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_RELEASE 0 CACHE STRING
    "Code::Blocks SDK release version used by the private provider")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(codium_blocks_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

find_package(wxWidgets REQUIRED COMPONENTS core base richtext net xml xrc)
include(${wxWidgets_USE_FILE})
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    add_compile_definitions(CODIUM_BLOCKS_HAVE_OPENSSL=1)
    set(CODIUM_BLOCKS_CRYPTO_LIB OpenSSL::Crypto)
endif()

set(CODIUM_BLOCKS_CODEBLOCKS_ADAPTER_AVAILABLE OFF)
set(CODIUM_BLOCKS_CODEBLOCKS_LINK_TARGET "")
set(CODIUM_BLOCKS_CODEBLOCKS_INCLUDE_DIRS "")
set(CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR_DETECTED "")
set(CODIUM_BLOCKS_DEBUGGERGDB_PROVIDER_AVAILABLE OFF)
set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_INCLUDE_DIRS "")
set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_IDENTITY "unspecified")

if(CODIUM_BLOCKS_ENABLE_CODEBLOCKS_DEBUGGERGDB_PROVIDER)
    if(NOT CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_DIR AND CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT)
        set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_DIR
            "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/src/plugins/debuggergdb"
            CACHE PATH "Code::Blocks plugins/debuggergdb source directory matching the installed SDK" FORCE)
    endif()
    find_path(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_HEADER_DIR debuggergdb.h
        HINTS "${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_DIR}" NO_DEFAULT_PATH)
    if(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_HEADER_DIR AND
       CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION AND
       CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_TAG)
        set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_INCLUDE_DIRS
            "${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_HEADER_DIR}")
        if(CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT)
            list(APPEND CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_INCLUDE_DIRS
                "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/src/include"
                "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/src/sdk")
        endif()
        string(TOLOWER "${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-cxx${CMAKE_CXX_STANDARD}" CODIUM_BLOCKS_CODEBLOCKS_COMPILER_IDENTITY)
        set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_IDENTITY
            "${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION}|${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_TAG}|${CODIUM_BLOCKS_CODEBLOCKS_COMPILER_IDENTITY}")
        set(CODIUM_BLOCKS_DEBUGGERGDB_PROVIDER_AVAILABLE ON)
        message(STATUS "Code::Blocks private DebuggerGDB provider build enabled")
        message(STATUS "  DebuggerGDB source: ${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_DIR}")
        message(STATUS "  Source revision: ${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION}")
        message(STATUS "  ABI identity: ${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_IDENTITY}")
    else()
        message(WARNING "DebuggerGDB provider requested but private source, exact revision, or ABI tag is missing; provider disabled")
    endif()
endif()

if(CODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CODEBLOCKS QUIET codeblocks)
    endif()

    if(CODEBLOCKS_FOUND)
        find_library(CODIUM_BLOCKS_CODEBLOCKS_LIBRARY NAMES codeblocks
            HINTS ${CODEBLOCKS_LIBRARY_DIRS})
        if(CODIUM_BLOCKS_CODEBLOCKS_LIBRARY)
            set(CODIUM_BLOCKS_CODEBLOCKS_LINK_TARGET ${CODIUM_BLOCKS_CODEBLOCKS_LIBRARY})
        endif()
        foreach(codeblocks_include_dir IN LISTS CODEBLOCKS_INCLUDE_DIRS)
            if(EXISTS "${codeblocks_include_dir}")
                list(APPEND CODIUM_BLOCKS_CODEBLOCKS_INCLUDE_DIRS "${codeblocks_include_dir}")
            endif()
        endforeach()
        if(NOT CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR)
            execute_process(
                COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=datarootdir codeblocks
                OUTPUT_VARIABLE CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR_DETECTED
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR_DETECTED)
                set(CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR_DETECTED
                    "${CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR_DETECTED}/codeblocks")
            endif()
        endif()
        if(NOT CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR)
            execute_process(
                COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=plugindir codeblocks
                OUTPUT_VARIABLE CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT EXISTS "${CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR}")
                execute_process(
                    COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=libdir codeblocks
                    OUTPUT_VARIABLE codeblocks_libdir
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
                set(CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR
                    "${codeblocks_libdir}/codeblocks/plugins")
            endif()
        endif()
    elseif(CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT)
        find_path(CODIUM_BLOCKS_CODEBLOCKS_INCLUDE_DIR cbplugin.h
            HINTS
                "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/include/codeblocks"
                "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/include"
                "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/src/include"
            NO_DEFAULT_PATH)
        find_library(CODIUM_BLOCKS_CODEBLOCKS_LIBRARY NAMES codeblocks
            HINTS
                "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/lib"
                "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/lib64"
                "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/build/sdk"
            NO_DEFAULT_PATH)
        if(CODIUM_BLOCKS_CODEBLOCKS_INCLUDE_DIR AND CODIUM_BLOCKS_CODEBLOCKS_LIBRARY)
            set(CODIUM_BLOCKS_CODEBLOCKS_LINK_TARGET ${CODIUM_BLOCKS_CODEBLOCKS_LIBRARY})
            set(CODIUM_BLOCKS_CODEBLOCKS_INCLUDE_DIRS ${CODIUM_BLOCKS_CODEBLOCKS_INCLUDE_DIR})
        endif()
        if(NOT CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR)
            foreach(candidate
                    "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/share/codeblocks"
                    "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/data"
                    "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}")
                if(EXISTS "${candidate}/resources.zip")
                    set(CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR_DETECTED "${candidate}")
                    break()
                endif()
            endforeach()
        endif()
        if(NOT CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR)
            foreach(candidate
                    "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/lib/codeblocks/plugins"
                    "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/plugins"
                    "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/bin/plugins")
                if(EXISTS "${candidate}")
                    set(CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR "${candidate}")
                    break()
                endif()
            endforeach()
        endif()
    endif()

    if(NOT CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR AND CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR_DETECTED)
        set(CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR
            "${CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR_DETECTED}" CACHE PATH
            "Code::Blocks data directory containing resources.zip" FORCE)
    endif()
    if(CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR AND EXISTS "${CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR}/resources.zip")
        if(NOT CODIUM_BLOCKS_CODEBLOCKS_COMPILER_PLUGIN)
        foreach(plugin_candidate
                "${CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR}/libcompiler.so"
                "${CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR}/libcompiler.dylib"
                "${CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR}/compiler.dll"
                "${CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR}/compiler.exe"
                "${CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR}/../lib/codeblocks/plugins/libcompiler.so"
                "${CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR}/plugins/libcompiler.so")
            if(EXISTS "${plugin_candidate}")
                set(CODIUM_BLOCKS_CODEBLOCKS_COMPILER_PLUGIN "${plugin_candidate}")
                break()
            endif()
        endforeach()
        endif()
        if(NOT CODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PLUGIN)
        foreach(plugin_candidate
                "${CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR}/libdebugger.so"
                "${CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR}/libdebugger.dylib"
                "${CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR}/debugger.dll"
                "${CODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR}/debugger.so"
                "${CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR}/../lib/codeblocks/plugins/libdebugger.so"
                "${CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR}/plugins/libdebugger.so")
            if(EXISTS "${plugin_candidate}")
                set(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PLUGIN "${plugin_candidate}")
                break()
            endif()
        endforeach()
        endif()
    endif()

    if(CODIUM_BLOCKS_CODEBLOCKS_LINK_TARGET AND CODIUM_BLOCKS_CODEBLOCKS_INCLUDE_DIRS AND
       CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR AND EXISTS "${CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR}/resources.zip" AND
       CODIUM_BLOCKS_CODEBLOCKS_COMPILER_PLUGIN AND EXISTS "${CODIUM_BLOCKS_CODEBLOCKS_COMPILER_PLUGIN}")
        set(CODIUM_BLOCKS_CODEBLOCKS_ADAPTER_AVAILABLE ON)
        message(STATUS "Code::Blocks SDK adapter build enabled")
        message(STATUS "  SDK includes: ${CODIUM_BLOCKS_CODEBLOCKS_INCLUDE_DIRS}")
        message(STATUS "  SDK data: ${CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR}")
        message(STATUS "  Compiler plugin: ${CODIUM_BLOCKS_CODEBLOCKS_COMPILER_PLUGIN}")
        message(STATUS "  Debugger plugin: ${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PLUGIN}")
    else()
        message(STATUS "Code::Blocks SDK adapter unavailable; building the portable core only")
    endif()
endif()

if(CODIUM_BLOCKS_DEBUGGERGDB_PROVIDER_AVAILABLE AND (WIN32 OR APPLE))
    set(CODIUM_BLOCKS_DEBUGGERGDB_PROVIDER_AVAILABLE OFF)
    message(WARNING "DebuggerGDB provider is currently validated only on Linux; provider disabled on this platform")
endif()

if(CODIUM_BLOCKS_DEBUGGERGDB_PROVIDER_AVAILABLE AND
   (NOT CODIUM_BLOCKS_CODEBLOCKS_ADAPTER_AVAILABLE OR
    NOT CODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PLUGIN OR
    NOT EXISTS "${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PLUGIN}"))
    set(CODIUM_BLOCKS_DEBUGGERGDB_PROVIDER_AVAILABLE OFF)
    message(WARNING "DebuggerGDB provider disabled because the matched Code::Blocks Debugger plugin is unavailable")
endif()

add_executable(codium-blocks
    native/src/document.cpp
    native/src/main.cpp
    native/src/extension_host_client.cpp
    native/src/vsix_manager.cpp
    native/src/workspace.cpp
    native/src/project_config.cpp
    native/src/task_runner.cpp
    native/src/terminal_session.cpp
    native/src/terminal_screen.cpp
    native/src/terminal_profile.cpp
    native/src/dap_client.cpp
    native/src/extension_security.cpp
    native/src/extension_registry.cpp
    native/src/debug_model.cpp
    native/src/signature_verifier.cpp
    native/src/native_contributions.cpp
    native/src/compatibility_report.cpp
    native/src/problem_model.cpp
    native/src/codeblocks_bridge.cpp
    native/src/codeblocks_host.cpp
    native/src/codeblocks_adapter_client.cpp
)

# wxIMPLEMENT_APP provides WinMain on Windows. Mark only the GUI target as a
# Windows-subsystem executable; smoke tests intentionally remain console apps.
if(WIN32)
    set_target_properties(codium-blocks PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_include_directories(codium-blocks PRIVATE native/include)
target_compile_definitions(codium-blocks PRIVATE
    CODIUM_BLOCKS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CODIUM_BLOCKS_VERSION="${PROJECT_VERSION}"
)
codium_blocks_enable_warnings(codium-blocks)
target_link_libraries(codium-blocks PRIVATE ${wxWidgets_LIBRARIES} ${CODIUM_BLOCKS_CRYPTO_LIB})
if(CODIUM_BLOCKS_ENABLE_CODEBLOCKS_SDK AND CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT)
    target_include_directories(codium-blocks PRIVATE
        "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/include"
        "${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}/src/include"
    )
    target_compile_definitions(codium-blocks PRIVATE CODIUM_BLOCKS_CODEBLOCKS_SDK_ENABLED=1)
    message(STATUS "Optional Code::Blocks SDK include integration enabled: ${CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT}")
else()
    message(STATUS "Code::Blocks SDK include integration is optional and disabled")
endif()

if(CODIUM_BLOCKS_CODEBLOCKS_ADAPTER_AVAILABLE)
    add_executable(codium-blocks-codeblocks-adapter
        native/src/codeblocks_adapter_main.cpp
        native/src/codeblocks_host.cpp
        native/src/codeblocks_sdk_bootstrap.cpp
        native/src/codeblocks_debugger_headless.cpp
    )
    target_include_directories(codium-blocks-codeblocks-adapter PRIVATE
        native/include ${CODIUM_BLOCKS_CODEBLOCKS_INCLUDE_DIRS})
    codium_blocks_enable_warnings(codium-blocks-codeblocks-adapter)
    target_link_libraries(codium-blocks-codeblocks-adapter PRIVATE
        ${wxWidgets_LIBRARIES} ${CODIUM_BLOCKS_CODEBLOCKS_LINK_TARGET})
    if(CODIUM_BLOCKS_DEBUGGERGDB_PROVIDER_AVAILABLE)
        target_compile_definitions(codium-blocks-codeblocks-adapter PRIVATE
            CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION="${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION}"
            CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_IDENTITY="${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_IDENTITY}"
            CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MAJOR=${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MAJOR}
            CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MINOR=${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MINOR}
            CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_RELEASE=${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_RELEASE}
            CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_PRIVATE_PROVIDER_ENABLED=1)
    endif()
endif()

if(CODIUM_BLOCKS_DEBUGGERGDB_PROVIDER_AVAILABLE AND CODIUM_BLOCKS_CODEBLOCKS_ADAPTER_AVAILABLE AND
   UNIX AND NOT APPLE AND
   CODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PLUGIN AND EXISTS "${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PLUGIN}")
    add_library(codium-blocks-codeblocks-debuggergdb-provider SHARED
        native/src/codeblocks_debugger_provider.cpp)
    target_include_directories(codium-blocks-codeblocks-debuggergdb-provider PRIVATE
        native/include ${CODIUM_BLOCKS_CODEBLOCKS_INCLUDE_DIRS}
        ${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_INCLUDE_DIRS})
    target_compile_definitions(codium-blocks-codeblocks-debuggergdb-provider PRIVATE
        CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_PROVIDER_BUILD=1
        CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION="${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION}"
        CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_IDENTITY="${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_IDENTITY}"
        CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_PROVIDER_IDENTITY="codium-blocks-debuggergdb/${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION}"
        CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MAJOR=${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MAJOR}
        CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MINOR=${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MINOR}
        CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_RELEASE=${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_RELEASE})
    codium_blocks_enable_warnings(codium-blocks-codeblocks-debuggergdb-provider)
    target_link_libraries(codium-blocks-codeblocks-debuggergdb-provider PRIVATE
        ${wxWidgets_LIBRARIES} ${CODIUM_BLOCKS_CODEBLOCKS_LINK_TARGET}
        ${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PLUGIN})
    set_target_properties(codium-blocks-codeblocks-debuggergdb-provider PROPERTIES
        OUTPUT_NAME codium-blocks-codeblocks-debuggergdb-provider)
endif()

add_executable(codium-blocks-document-smoke
    native/src/document.cpp
    tests/document-smoke.cpp
)
target_include_directories(codium-blocks-document-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-document-smoke)
target_link_libraries(codium-blocks-document-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-vsix-smoke
    native/src/vsix_manager.cpp
    native/src/extension_security.cpp
    native/src/signature_verifier.cpp
    tests/vsix-smoke.cpp
)
target_include_directories(codium-blocks-vsix-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-vsix-smoke)
target_link_libraries(codium-blocks-vsix-smoke PRIVATE ${wxWidgets_LIBRARIES} ${CODIUM_BLOCKS_CRYPTO_LIB})

add_executable(codium-blocks-security-smoke
    native/src/extension_security.cpp
    native/src/extension_registry.cpp
    native/src/signature_verifier.cpp
    tests/security-smoke.cpp
)
target_include_directories(codium-blocks-security-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-security-smoke)
target_link_libraries(codium-blocks-security-smoke PRIVATE ${wxWidgets_LIBRARIES} ${CODIUM_BLOCKS_CRYPTO_LIB})

add_executable(codium-blocks-workspace-smoke
    native/src/workspace.cpp
    tests/workspace-smoke.cpp
)
target_include_directories(codium-blocks-workspace-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-workspace-smoke)
target_link_libraries(codium-blocks-workspace-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-task-smoke
    native/src/project_config.cpp
    native/src/task_runner.cpp
    tests/task-smoke.cpp
)
target_include_directories(codium-blocks-task-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-task-smoke)
target_link_libraries(codium-blocks-task-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-transport-smoke
    native/src/terminal_session.cpp
    native/src/terminal_screen.cpp
    native/src/dap_client.cpp
    tests/transport-smoke.cpp
)
target_include_directories(codium-blocks-transport-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-transport-smoke)
target_link_libraries(codium-blocks-transport-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-terminal-screen-smoke
    native/src/terminal_screen.cpp
    tests/terminal-screen-smoke.cpp
)
target_include_directories(codium-blocks-terminal-screen-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-terminal-screen-smoke)
target_link_libraries(codium-blocks-terminal-screen-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-terminal-profile-smoke
    native/src/terminal_profile.cpp
    tests/terminal-profile-smoke.cpp
)
target_include_directories(codium-blocks-terminal-profile-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-terminal-profile-smoke)
target_link_libraries(codium-blocks-terminal-profile-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-debug-model-smoke
    native/src/debug_model.cpp
    tests/debug-model-smoke.cpp
)
target_include_directories(codium-blocks-debug-model-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-debug-model-smoke)
target_link_libraries(codium-blocks-debug-model-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-native-contributions-smoke
    native/src/native_contributions.cpp
    tests/native-contributions-smoke.cpp
)
target_include_directories(codium-blocks-native-contributions-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-native-contributions-smoke)
target_link_libraries(codium-blocks-native-contributions-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-compatibility-smoke
    native/src/extension_security.cpp
    native/src/compatibility_report.cpp
    tests/compatibility-smoke.cpp
)
target_include_directories(codium-blocks-compatibility-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-compatibility-smoke)
target_link_libraries(codium-blocks-compatibility-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-problem-model-smoke
    native/src/problem_model.cpp
    tests/problem-model-smoke.cpp
)
target_include_directories(codium-blocks-problem-model-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-problem-model-smoke)
target_link_libraries(codium-blocks-problem-model-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-codeblocks-bridge-smoke
    native/src/codeblocks_bridge.cpp
    tests/codeblocks-bridge-smoke.cpp
)
target_include_directories(codium-blocks-codeblocks-bridge-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-codeblocks-bridge-smoke)
target_link_libraries(codium-blocks-codeblocks-bridge-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-codeblocks-host-smoke
    native/src/codeblocks_host.cpp
    tests/codeblocks-host-smoke.cpp
)
target_include_directories(codium-blocks-codeblocks-host-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-codeblocks-host-smoke)
target_link_libraries(codium-blocks-codeblocks-host-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-codeblocks-adapter-smoke
    native/src/codeblocks_host.cpp
    native/src/codeblocks_adapter_client.cpp
    tests/codeblocks-adapter-smoke.cpp
)
target_include_directories(codium-blocks-codeblocks-adapter-smoke PRIVATE native/include)
codium_blocks_enable_warnings(codium-blocks-codeblocks-adapter-smoke)
target_link_libraries(codium-blocks-codeblocks-adapter-smoke PRIVATE ${wxWidgets_LIBRARIES})

if(CODIUM_BLOCKS_CODEBLOCKS_ADAPTER_AVAILABLE AND UNIX AND NOT APPLE)
    add_executable(codium-blocks-codeblocks-real-adapter-smoke
        native/src/codeblocks_host.cpp
        native/src/codeblocks_adapter_client.cpp
        tests/codeblocks-real-adapter-smoke.cpp
    )
    target_include_directories(codium-blocks-codeblocks-real-adapter-smoke PRIVATE native/include)
    codium_blocks_enable_warnings(codium-blocks-codeblocks-real-adapter-smoke)
    target_link_libraries(codium-blocks-codeblocks-real-adapter-smoke PRIVATE ${wxWidgets_LIBRARIES})

    add_executable(codium-blocks-codeblocks-sdk-events-smoke
        native/src/codeblocks_host.cpp
        native/src/codeblocks_sdk_bootstrap.cpp
        native/src/codeblocks_debugger_headless.cpp
        tests/codeblocks-sdk-events-smoke.cpp
    )
    target_include_directories(codium-blocks-codeblocks-sdk-events-smoke PRIVATE
        native/include ${CODIUM_BLOCKS_CODEBLOCKS_INCLUDE_DIRS})
    codium_blocks_enable_warnings(codium-blocks-codeblocks-sdk-events-smoke)
    target_link_libraries(codium-blocks-codeblocks-sdk-events-smoke PRIVATE
        ${wxWidgets_LIBRARIES} ${CODIUM_BLOCKS_CODEBLOCKS_LINK_TARGET})
endif()

install(TARGETS codium-blocks RUNTIME DESTINATION bin)
if(TARGET codium-blocks-codeblocks-adapter)
    install(TARGETS codium-blocks-codeblocks-adapter RUNTIME DESTINATION bin)
endif()
if(TARGET codium-blocks-codeblocks-debuggergdb-provider)
    install(TARGETS codium-blocks-codeblocks-debuggergdb-provider
        RUNTIME DESTINATION bin LIBRARY DESTINATION bin)
endif()
install(DIRECTORY extension-host DESTINATION share/codium-blocks)
install(DIRECTORY extensions DESTINATION share/codium-blocks)
install(DIRECTORY assets/icons DESTINATION share/codium-blocks)
install(FILES README.md LICENSE COPYING DESTINATION share/doc/codium-blocks)
install(DIRECTORY docs protocol DESTINATION share/doc/codium-blocks)

include(CTest)
if(BUILD_TESTING)
    add_test(
        NAME extension-host-smoke
        COMMAND ${CMAKE_COMMAND} -E env
            CODIUM_BLOCKS_ROOT=${CMAKE_SOURCE_DIR}
            node ${CMAKE_SOURCE_DIR}/tests/host-smoke.mjs
    )
    add_test(NAME document-smoke COMMAND codium-blocks-document-smoke)
    add_test(NAME vsix-smoke COMMAND codium-blocks-vsix-smoke)
    add_test(NAME security-smoke COMMAND codium-blocks-security-smoke)
    add_test(NAME workspace-smoke COMMAND codium-blocks-workspace-smoke)
    add_test(NAME task-smoke COMMAND codium-blocks-task-smoke)
    add_test(NAME transport-smoke COMMAND codium-blocks-transport-smoke ${CMAKE_SOURCE_DIR})
    add_test(NAME terminal-screen-smoke COMMAND codium-blocks-terminal-screen-smoke)
    add_test(NAME terminal-profile-smoke COMMAND codium-blocks-terminal-profile-smoke)
    add_test(NAME debug-model-smoke COMMAND codium-blocks-debug-model-smoke)
    add_test(NAME native-contributions-smoke COMMAND codium-blocks-native-contributions-smoke)
    add_test(NAME compatibility-smoke COMMAND codium-blocks-compatibility-smoke)
    add_test(NAME problem-model-smoke COMMAND codium-blocks-problem-model-smoke)
    add_test(NAME codeblocks-bridge-smoke COMMAND codium-blocks-codeblocks-bridge-smoke)
    add_test(NAME codeblocks-host-smoke COMMAND codium-blocks-codeblocks-host-smoke)
    add_test(NAME codeblocks-adapter-smoke COMMAND codium-blocks-codeblocks-adapter-smoke ${CMAKE_SOURCE_DIR})

    if(TARGET codium-blocks-codeblocks-real-adapter-smoke)
        find_program(CODIUM_BLOCKS_XVFB_RUN xvfb-run)
        if(CODIUM_BLOCKS_XVFB_RUN AND CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR AND CODIUM_BLOCKS_CODEBLOCKS_COMPILER_PLUGIN)
            file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/codeblocks-adapter-home/config")
            set(CODIUM_BLOCKS_REAL_ADAPTER_ARGS
                $<TARGET_FILE:codium-blocks-codeblocks-adapter>
                ${CMAKE_SOURCE_DIR}/tests/fixtures/codium-blocks-adapter.cbp
                ${CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR}
                ${CODIUM_BLOCKS_CODEBLOCKS_COMPILER_PLUGIN})
            if(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PLUGIN)
                list(APPEND CODIUM_BLOCKS_REAL_ADAPTER_ARGS ${CODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PLUGIN})
            endif()
            if(TARGET codium-blocks-codeblocks-debuggergdb-provider)
                list(APPEND CODIUM_BLOCKS_REAL_ADAPTER_ARGS
                    $<TARGET_FILE:codium-blocks-codeblocks-debuggergdb-provider>)
            endif()
            add_test(
                NAME codeblocks-real-adapter-smoke
                COMMAND ${CMAKE_COMMAND} -E env
                    HOME=${CMAKE_CURRENT_BINARY_DIR}/codeblocks-adapter-home
                    XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/codeblocks-adapter-home/config
                    ${CODIUM_BLOCKS_XVFB_RUN} -a
                    $<TARGET_FILE:codium-blocks-codeblocks-real-adapter-smoke>
                    ${CODIUM_BLOCKS_REAL_ADAPTER_ARGS}
            )
            add_test(
                NAME codeblocks-sdk-events-smoke
                COMMAND ${CMAKE_COMMAND} -E env
                    HOME=${CMAKE_CURRENT_BINARY_DIR}/codeblocks-adapter-home
                    XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/codeblocks-adapter-home/config
                    ${CODIUM_BLOCKS_XVFB_RUN} -a
                    $<TARGET_FILE:codium-blocks-codeblocks-sdk-events-smoke>
                    ${CMAKE_SOURCE_DIR}/tests/fixtures/codium-blocks-adapter.cbp
                    ${CODIUM_BLOCKS_CODEBLOCKS_DATA_DIR}
                    ${CODIUM_BLOCKS_CODEBLOCKS_COMPILER_PLUGIN}
            )
        else()
            message(STATUS "Code::Blocks real integration test unavailable: xvfb-run or SDK runtime files missing")
        endif()
    endif()
endif()

message(STATUS "Codium::Blocks ${PROJECT_VERSION}: native wxWidgets core + optional Node.js extension host")
message(STATUS "Electron is not a dependency of this target")
