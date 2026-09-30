cmake_minimum_required(VERSION 3.25)

project(125A_Arporator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(ArporatorCore STATIC
    source/arporator_engine.cpp
    source/arporator_engine.h
    source/variation.cpp
    source/variation.h
)

target_include_directories(ArporatorCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/source")
target_compile_features(ArporatorCore PUBLIC cxx_std_17)

add_executable(ArporatorEngineTests tests/arporator_engine_tests.cpp)
target_link_libraries(ArporatorEngineTests PRIVATE ArporatorCore)

add_executable(ArporatorVariationTests tests/variation_tests.cpp)
target_link_libraries(ArporatorVariationTests PRIVATE ArporatorCore)

enable_testing()
add_test(NAME ArporatorEngineTests COMMAND ArporatorEngineTests)
add_test(NAME ArporatorVariationTests COMMAND ArporatorVariationTests)


option(ARPORATOR_BUILD_VST3 "Build the VST3 plugin target" OFF)

if(ARPORATOR_BUILD_VST3)
    include(FetchContent)

    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES ON CACHE BOOL "" FORCE)
    set(SMTG_RUN_VST_VALIDATOR ON CACHE BOOL "" FORCE)
    set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.8.1_build_84
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(vst3sdk)

    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resource/125A_Logo_Master_FINAL.svg" ARPORATOR_BRANDING_SVG)
    string(REGEX MATCHALL "<path d=\\\"[^\\\"]+\\\" fill=\\\"#[0-9A-Fa-f]+\\\"/>" ARPORATOR_BRANDING_PATH_TAGS "${ARPORATOR_BRANDING_SVG}")
    list(LENGTH ARPORATOR_BRANDING_PATH_TAGS ARPORATOR_BRANDING_PATH_COUNT)
    if(NOT ARPORATOR_BRANDING_PATH_COUNT EQUAL 30)
        message(FATAL_ERROR "125A branding master must contain exactly 30 vector paths; found ${ARPORATOR_BRANDING_PATH_COUNT}.")
    endif()

    set(ARPORATOR_BRANDING_PATHS "")
    foreach(ARPORATOR_BRANDING_TAG IN LISTS ARPORATOR_BRANDING_PATH_TAGS)
        if(NOT ARPORATOR_BRANDING_TAG MATCHES "^<path d=\\\"([^\\\"]+)\\\" fill=\\\"([^\\\"]+)\\\"/>$")
            message(FATAL_ERROR "Could not parse 125A branding path tag.")
        endif()
        set(ARPORATOR_BRANDING_D "${CMAKE_MATCH_1}")
        set(ARPORATOR_BRANDING_FILL "${CMAKE_MATCH_2}")
        if(ARPORATOR_BRANDING_FILL STREQUAL "#D71920")
            set(ARPORATOR_BRANDING_RED "true")
        elseif(ARPORATOR_BRANDING_FILL STREQUAL "#D9D9D9")
            set(ARPORATOR_BRANDING_RED "false")
        else()
            message(FATAL_ERROR "Unexpected 125A branding fill: ${ARPORATOR_BRANDING_FILL}")
        endif()
        string(APPEND ARPORATOR_BRANDING_PATHS "    {\"${ARPORATOR_BRANDING_D}\", ${ARPORATOR_BRANDING_RED}},\n")
    endforeach()

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/vst3/branding_master.h.in"
        "${CMAKE_BINARY_DIR}/branding_master.h"
        @ONLY
    )

    smtg_add_vst3plugin(
        Arporator
        PACKAGE_NAME "125A-Arporator"
        SOURCES_LIST
            vst3/AtomicSnapshot.h
            vst3/ArporatorIDs.h
            vst3/ArporatorPlugin.h
            vst3/ArporatorPlugin.cpp
            vst3/ArporatorViews.h
            vst3/ArporatorViews.cpp
    )

    target_link_libraries(
        Arporator
        PRIVATE
            sdk
            vstgui_support
            ArporatorCore
    )

    target_include_directories(
        Arporator
        PRIVATE
            "${CMAKE_BINARY_DIR}"
    )

    target_compile_features(Arporator PRIVATE cxx_std_17)

    if(WIN32)
        target_sources(
            Arporator
            PRIVATE
                ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/dllmain.cpp
        )
    endif()

    smtg_target_add_plugin_resources(
        Arporator
        RESOURCES
            resource/Arporator.uidesc
            resource/125A_Logo_Master_FINAL.svg
    )

    smtg_target_configure_version_file(Arporator)
endif()
