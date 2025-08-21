add_executable(${PROJECT_NAME}-tests test_main.cpp dsp_basics.cpp)
target_link_libraries(${PROJECT_NAME}-tests
        ${PROJECT_NAME}-impl
        fmt
        sst-plugininfra::version_information
        catch2)
