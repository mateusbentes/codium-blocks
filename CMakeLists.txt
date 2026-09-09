cmake_minimum_required(VERSION 3.20)
project(CodiumBlocks VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

add_executable(codium-blocks
    native/src/main.cpp
    native/src/extension_host_client.cpp
    native/src/vsix_manager.cpp
)

target_include_directories(codium-blocks PRIVATE native/include)
target_compile_definitions(codium-blocks PRIVATE
    CODIUM_BLOCKS_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CODIUM_BLOCKS_VERSION="${PROJECT_VERSION}"
)
target_compile_options(codium-blocks PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(codium-blocks PRIVATE ${wxWidgets_LIBRARIES})

install(TARGETS codium-blocks RUNTIME DESTINATION bin)
install(DIRECTORY extension-host DESTINATION share/codium-blocks)
install(DIRECTORY extensions DESTINATION share/codium-blocks)
install(FILES README.md LICENSE COPYING DESTINATION share/doc/codium-blocks)

include(CTest)
if(BUILD_TESTING)
    add_test(
        NAME extension-host-smoke
        COMMAND ${CMAKE_COMMAND} -E env
            CODIUM_BLOCKS_ROOT=${CMAKE_SOURCE_DIR}
            node ${CMAKE_SOURCE_DIR}/tests/host-smoke.mjs
    )
endif()

message(STATUS "Codium::Blocks ${PROJECT_VERSION}: native wxWidgets core + optional Node.js extension host")
message(STATUS "Electron is not a dependency of this target")
