add_executable(${PROJECT_NAME}-tests test_main.cpp dsp_basics.cpp patch_sync.cpp)
target_link_libraries(${PROJECT_NAME}-tests
        ${PROJECT_NAME}-impl
        fmt
        simde
        sst-basic-blocks
        sst-cpputils
        sst-filters sst-filters-extras
        sst-plugininfra::patchbase
        sst-plugininfra::filesystem
        sst-plugininfra::tinyxml
        sst-plugininfra::version_information
        catch2)
