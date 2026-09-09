cmake_minimum_required(VERSION 3.20)
project(CodiumBlocks VERSION 1.0.1 LANGUAGES CXX)

option(CODIUM_BLOCKS_ENABLE_CODEBLOCKS_SDK "Expose an optional Code::Blocks SDK include root to the native target" OFF)
set(CODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT "" CACHE PATH "Code::Blocks SDK installation or source root")

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

find_package(wxWidgets REQUIRED COMPONENTS core base richtext net xml)
include(${wxWidgets_USE_FILE})
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    add_compile_definitions(CODIUM_BLOCKS_HAVE_OPENSSL=1)
    set(CODIUM_BLOCKS_CRYPTO_LIB OpenSSL::Crypto)
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

install(TARGETS codium-blocks RUNTIME DESTINATION bin)
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
endif()

message(STATUS "Codium::Blocks ${PROJECT_VERSION}: native wxWidgets core + optional Node.js extension host")
message(STATUS "Electron is not a dependency of this target")
