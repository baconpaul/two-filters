
add_subdirectory(sst/sst-cmake)
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)

add_subdirectory(clap-libs/clap EXCLUDE_FROM_ALL)
add_subdirectory(clap-libs/clap-helpers EXCLUDE_FROM_ALL)
add_subdirectory(fmt EXCLUDE_FROM_ALL)

add_subdirectory(sst/sst-clap-helpers)
# The juce shim (and with it all of JUCE) is out of the build; the neui
# editor drives the clap gui extension directly.
# add_clap_juce_shim(JUCE_PATH "${JUCE_PATH}")

# neui (C ui framework) + neuiplusplus (C++20 layer over it) - the MIT ui
# stack the neui editor and sst-neuigui are built on. neui's examples/tests
# default off when consumed by add_subdirectory but make it explicit.
set(NEUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NEUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(neui/neui)
add_subdirectory(neui/neuiplusplus)
add_subdirectory(sst-neuigui)

add_library(simde INTERFACE)
target_include_directories(simde INTERFACE simde)
add_subdirectory(sst/sst-cpputils)
add_subdirectory(sst/sst-basic-blocks)
# sst-jucegui needs juce; sst-neuigui above is its neui replacement
# add_subdirectory(sst/sst-jucegui)
add_subdirectory(sst/sst-filters)
add_subdirectory(sst/sst-waveshapers)

set(SST_PLUGININFRA_PROVIDE_TINYXML ON CACHE BOOL "yesxml")
set(SST_PLUGININFRA_PROVIDE_PATCHBASE ON CACHE BOOL "patchbase pls")
add_subdirectory(sst/sst-plugininfra)

add_library(mts-esp-client STATIC MTS-ESP/Client/libMTSClient.cpp)
target_include_directories(mts-esp-client PUBLIC MTS-ESP/Client)


set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE CACHE BOOL "Get em")
set(CLAP_WRAPPER_DONT_ADD_TARGETS TRUE CACHE BOOL "I'll targetize")
set(CLAP_WRAPPER_BUILD_AUV2 TRUE CACHE BOOL "It's only logical")
add_compile_definitions(CLAP_WRAPPER_LOGLEVEL=0)

if (APPLE)
    # BaconPaul has jack kinda installed
    set(RTAUDIO_API_JACK FALSE CACHE BOOL "Not on apple")
endif()

add_subdirectory(clap-libs/clap-wrapper)


add_library(catch2 INTERFACE)
target_include_directories(catch2 INTERFACE catch2/include)
