cmake_minimum_required(VERSION 3.24)

project(SynpleWebClap VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

set(JAPE_SOURCE_DIR "" CACHE PATH "Path to a local Jape engine checkout (default: pinned tag)")
set(JAPE_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if (CMAKE_SYSTEM_NAME STREQUAL "WASI")
    # wasi-sdk has no C++ exception support
    add_compile_options(-fno-exceptions -msimd128)
    set(SYNPLE_IS_WASM ON)
endif ()

# ─── Dependencies ─────────────────────────────────────────────────────────────
set(CPM_DOWNLOAD_VERSION 0.42.3)
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
if (NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD
        "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
        "${CPM_DOWNLOAD_LOCATION}"
        STATUS CPM_DOWNLOAD_STATUS
    )
    list(GET CPM_DOWNLOAD_STATUS 0 CPM_DOWNLOAD_RESULT)
    if (NOT CPM_DOWNLOAD_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not download CPM.cmake: ${CPM_DOWNLOAD_STATUS}")
    endif ()
endif ()
include("${CPM_DOWNLOAD_LOCATION}")

if (JAPE_SOURCE_DIR)
    add_subdirectory("${JAPE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/jape")
else ()
    CPMAddPackage(
        NAME Jape
        GITHUB_REPOSITORY jonnyminusx/Jape
        GIT_TAG 0.1.2
    )
endif ()

CPMAddPackage(
    NAME clap
    GITHUB_REPOSITORY free-audio/clap
    GIT_TAG 1.2.7
    DOWNLOAD_ONLY YES
)

# ─── UI (Vite single-file build embedded into the module) ─────────────────────
set(UI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ui")
set(UI_HTML "${UI_DIR}/dist/index.html")
set(UI_EMBEDDED_CPP "${CMAKE_BINARY_DIR}/generated/UiResources.cpp")

file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS "${UI_DIR}/src/*" "${UI_DIR}/index.html" "${UI_DIR}/vite.config.ts")

add_custom_command(
    OUTPUT "${UI_HTML}"
    COMMAND npm run build
    WORKING_DIRECTORY "${UI_DIR}"
    DEPENDS ${UI_SOURCES} "${UI_DIR}/package.json"
    COMMENT "Building UI..."
    VERBATIM
)

add_custom_command(
    OUTPUT "${UI_EMBEDDED_CPP}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${UI_HTML} -DOUTPUT=${UI_EMBEDDED_CPP} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake"
    DEPENDS "${UI_HTML}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFile.cmake"
    COMMENT "Embedding UI..."
    VERBATIM
)

# ─── Plugin ───────────────────────────────────────────────────────────────────
set(SYNPLE_SOURCES
    source/ParamTable.cpp
    source/Plugin.cpp
    source/Presets.cpp
    source/Snapshot.cpp
    "${UI_EMBEDDED_CPP}"
)

if (SYNPLE_IS_WASM)
    add_executable(synple_wclap ${SYNPLE_SOURCES})
    set_target_properties(synple_wclap PROPERTIES OUTPUT_NAME synple SUFFIX ".wclap.wasm")
    target_link_options(synple_wclap PRIVATE
        -mexec-model=reactor
        -Wl,--max-memory=2147483648,--export-table,--growable-table,--export=malloc,--export=clap_entry
    )
else ()
    add_library(synple_wclap MODULE ${SYNPLE_SOURCES})
    set_target_properties(synple_wclap PROPERTIES OUTPUT_NAME synple)
endif ()

target_include_directories(synple_wclap PRIVATE source "${clap_SOURCE_DIR}/include")
target_link_libraries(synple_wclap PRIVATE Jape::Synth)
