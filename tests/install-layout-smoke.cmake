# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Codium::Blocks Contributors

if(NOT DEFINED BUILD_DIR OR NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "BUILD_DIR and SOURCE_DIR are required")
endif()

set(prefix "${BUILD_DIR}/install-layout-smoke")
file(REMOVE_RECURSE "${prefix}")
file(MAKE_DIRECTORY "${prefix}")

set(install_command "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --prefix "${prefix}")
if(DEFINED CONFIGURATION AND NOT CONFIGURATION STREQUAL "")
    list(APPEND install_command --config "${CONFIGURATION}")
endif()
execute_process(
    COMMAND ${install_command}
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_output
    ERROR_VARIABLE install_error)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "Install staging failed (${install_result})\n${install_output}\n${install_error}")
endif()

function(require_file path description)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing installed ${description}: ${path}")
    endif()
endfunction()

if(APPLE AND CODIUM_BLOCKS_MACOS_BUNDLE)
    set(bundle "${prefix}/codium-blocks.app")
    require_file("${bundle}/Contents/MacOS/codium-blocks" "macOS application executable")
    require_file("${bundle}/Contents/Resources/CodiumBlocks.icns" "macOS application icon")
    require_file("${bundle}/Contents/Resources/codium-blocks/extension-host/src/host.mjs" "bundled Extension Host")
    require_file("${bundle}/Contents/Resources/codium-blocks/locales/en-US.tsv" "English localization catalog")
    require_file("${bundle}/Contents/Resources/codium-blocks/locales/pt-BR.tsv" "Brazilian Portuguese localization catalog")
    require_file("${bundle}/Contents/Resources/codium-blocks/share/doc/codium-blocks/CHANGELOG.md" "changelog")
else()
    if(WIN32)
        set(executable "${prefix}/bin/codium-blocks.exe")
    else()
        set(executable "${prefix}/bin/codium-blocks")
    endif()
    require_file("${executable}" "native executable")
    require_file("${prefix}/share/codium-blocks/extension-host/src/host.mjs" "installed Extension Host")
    require_file("${prefix}/share/codium-blocks/extensions/hello-codium/extension.cjs" "demo extension")
    require_file("${prefix}/share/codium-blocks/locales/en-US.tsv" "English localization catalog")
    require_file("${prefix}/share/codium-blocks/locales/pt-BR.tsv" "Brazilian Portuguese localization catalog")
    require_file("${prefix}/share/doc/codium-blocks/CHANGELOG.md" "changelog")
    require_file("${prefix}/share/doc/codium-blocks/LICENSE" "license")

    if(UNIX AND NOT APPLE)
        require_file("${prefix}/share/applications/codium-blocks.desktop" "Linux desktop entry")
        require_file("${prefix}/share/icons/hicolor/512x512/apps/codium-blocks.png" "Linux application icon")
    endif()
endif()

message(STATUS "Install layout smoke passed: ${prefix}")
