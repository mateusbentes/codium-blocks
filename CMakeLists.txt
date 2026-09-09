cmake_minimum_required(VERSION 3.20)
project(CodiumBlocks VERSION 0.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

add_executable(codium-blocks
    native/src/document.cpp
    native/src/main.cpp
    native/src/extension_host_client.cpp
    native/src/vsix_manager.cpp
    native/src/workspace.cpp
    native/src/project_config.cpp
    native/src/task_runner.cpp
    native/src/terminal_session.cpp
    native/src/dap_client.cpp
)

target_include_directories(codium-blocks PRIVATE native/include)
target_compile_definitions(codium-blocks PRIVATE
    CODIUM_BLOCKS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CODIUM_BLOCKS_VERSION="${PROJECT_VERSION}"
)
target_compile_options(codium-blocks PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(codium-blocks PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-document-smoke
    native/src/document.cpp
    tests/document-smoke.cpp
)
target_include_directories(codium-blocks-document-smoke PRIVATE native/include)
target_compile_options(codium-blocks-document-smoke PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(codium-blocks-document-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-vsix-smoke
    native/src/vsix_manager.cpp
    tests/vsix-smoke.cpp
)
target_include_directories(codium-blocks-vsix-smoke PRIVATE native/include)
target_compile_options(codium-blocks-vsix-smoke PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(codium-blocks-vsix-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-workspace-smoke
    native/src/workspace.cpp
    tests/workspace-smoke.cpp
)
target_include_directories(codium-blocks-workspace-smoke PRIVATE native/include)
target_compile_options(codium-blocks-workspace-smoke PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(codium-blocks-workspace-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-task-smoke
    native/src/project_config.cpp
    native/src/task_runner.cpp
    tests/task-smoke.cpp
)
target_include_directories(codium-blocks-task-smoke PRIVATE native/include)
target_compile_options(codium-blocks-task-smoke PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(codium-blocks-task-smoke PRIVATE ${wxWidgets_LIBRARIES})

add_executable(codium-blocks-transport-smoke
    native/src/terminal_session.cpp
    native/src/dap_client.cpp
    tests/transport-smoke.cpp
)
target_include_directories(codium-blocks-transport-smoke PRIVATE native/include)
target_compile_options(codium-blocks-transport-smoke PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(codium-blocks-transport-smoke PRIVATE ${wxWidgets_LIBRARIES})

install(TARGETS codium-blocks RUNTIME DESTINATION bin)
install(DIRECTORY extension-host DESTINATION share/codium-blocks)
install(DIRECTORY extensions DESTINATION share/codium-blocks)
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
    add_test(NAME workspace-smoke COMMAND codium-blocks-workspace-smoke)
    add_test(NAME task-smoke COMMAND codium-blocks-task-smoke)
    add_test(NAME transport-smoke COMMAND codium-blocks-transport-smoke ${CMAKE_SOURCE_DIR})
endif()

message(STATUS "Codium::Blocks ${PROJECT_VERSION}: native wxWidgets core + optional Node.js extension host")
message(STATUS "Electron is not a dependency of this target")
