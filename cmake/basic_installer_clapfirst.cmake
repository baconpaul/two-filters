# A basic installer setup.
#
# Modified for products building clap first

function(add_clapfirst_installer)
    set(oneValueArgs
            INSTALLER_TARGET
            ASSET_OUTPUT_DIRECTORY
            PRODUCT_NAME
            INSTALLER_PREFIX
    )
    set(multiValueArgs
            TARGETS  # A list of plugin formats, "CLAP" "VST3" "AUV2"
    )
    cmake_parse_arguments(CIN "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN} )

    if ("${CIN_INSTALLER_PREFIX}" STREQUAL "")
        message(FATAL_ERROR "You must set INSTALLER_PREFIX in add_clapfirst_installer")
    endif()

    if ("${CIN_PRODUCT_NAME}" STREQUAL "")
        message(FATAL_ERROR "You must set PRODUCT_NAME in add_clapfirst_installer")
    endif()

    if ("${CIN_ASSET_OUTPUT_DIRECTORY}" STREQUAL "")
        message(FATAL_ERROR "You must set ASSET_OUTPUT_DIRECTORY in add_clapfirst_installer")
    endif()

    set(TGT ${CIN_INSTALLER_TARGET})
    add_custom_target(${TGT})
    foreach (INST ${CIN_TARGETS})
        if (TARGET ${INST})
            message(STATUS "Adding ${INST} to ${TGT} target deps")
            add_dependencies(${TGT} ${INST})
        endif()
    endforeach()


    string(TIMESTAMP INST_DATE "%Y-%m-%d")
    set(INST_ZIP ${CIN_INSTALLER_PREFIX}-${CMAKE_SYSTEM_NAME}${INST_EXTRA_ZIP_NAME}-${INST_DATE}-${GIT_COMMIT_HASH}.zip)
    message(STATUS "Zip File Name is ${INST_ZIP}")

    message(STATUS "PRODUCT NAME is ${CIN_PRODUCT_NAME}")

    if (APPLE)
        message(STATUS "Configuring for mac installer")
        add_custom_command(
                TARGET ${TGT}
                POST_BUILD
                USES_TERMINAL
                VERBATIM
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMAND ${CMAKE_COMMAND} -E make_directory installer
                COMMAND echo ${CMAKE_SOURCE_DIR}/libs/sst/sst-plugininfra/scripts/installer_mac/make_installer.sh ${CIN_PRODUCT_NAME} ${CIN_ASSET_OUTPUT_DIR} ${CMAKE_SOURCE_DIR}/resources/installer_mac ${CMAKE_BINARY_DIR}/installer "${INST_DATE}-${GIT_COMMIT_HASH}"
                COMMAND ${CMAKE_SOURCE_DIR}/libs/sst/sst-plugininfra/scripts/installer_mac/make_installer.sh ${CIN_PRODUCT_NAME} ${CIN_ASSET_OUTPUT_DIRECTORY} ${CMAKE_SOURCE_DIR}/resources/installer_mac ${CMAKE_BINARY_DIR}/installer "${INST_DATE}-${GIT_COMMIT_HASH}"
        )
    endif()

    if (FALSE)
    #elseif (WIN32)
    if (WIN32)
        message(STATUS "Configuring for win installer")
        include(InnoSetup)
        install_inno_setup()
        cmake_path(REMOVE_EXTENSION OBXF_ZIP OUTPUT_VARIABLE OBXF_INSTALLER)
        add_custom_command(
            TARGET obxf-installer
            POST_BUILD
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory installer
            COMMAND 7z a -r installer/${OBXF_ZIP} ${OBXF_PRODUCT_DIR}/
            COMMAND ${CMAKE_COMMAND} -E echo "ZIP Installer in: installer/${OBXF_ZIP}"
            COMMAND ${INNOSETUP_COMPILER_EXECUTABLE}
                /O"${CMAKE_BINARY_DIR}/installer" /F"${OBXF_INSTALLER}" /DName="${TARGET_BASE}"
                /DNameCondensed="${TARGET_BASE}" /DVersion="${GIT_COMMIT_HASH}"
                /DID="BBE27B03-BDB9-400E-8AC1-F197B964651A"
                /DCLAP /DVST3 /DSA
                /DIcon="${CMAKE_SOURCE_DIR}/resources/installer/logo.ico"
                /DBanner="${CMAKE_SOURCE_DIR}/resources/installer/banner.png"
                /DArch="${INNOSETUP_ARCH_ID}"
                /DLicense="${CMAKE_SOURCE_DIR}/LICENSE"
                /DStagedAssets="${OBXF_PRODUCT_DIR}"
                /DData="${CMAKE_SOURCE_DIR}/assets/installer" "${INNOSETUP_INSTALL_SCRIPT}"
        )
    else ()
        message(STATUS "Basic Installer: Target is installer/${OBXF_ZIP}")
        add_custom_command(
                TARGET obxf-installer
                POST_BUILD
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMAND ${CMAKE_COMMAND} -E make_directory installer
                COMMAND ${CMAKE_COMMAND} -E tar cvf installer/${OBXF_ZIP} --format=zip ${OBXF_PRODUCT_DIR}/
                COMMAND ${CMAKE_COMMAND} -E echo "Installer in: installer/${OBXF_ZIP}")

        add_custom_command(
                TARGET obxf-installer
                POST_BUILD
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                USES_TERMINAL
                COMMAND scripts/installer_linux/make_deb.sh ${OBXF_PRODUCT_DIR} ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/installer "${OBXF_DATE}-${GIT_COMMIT_HASH}"
        )
        # Only build the assets zip on linux, to be CI friendly
        add_custom_command(
                TARGET obxf-installer
                POST_BUILD
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/assets/installer
                COMMAND ${CMAKE_COMMAND} -E tar cvf ${CMAKE_BINARY_DIR}/installer/${OBXF_ASSETS_ZIP} --format=zip .
                COMMAND ${CMAKE_COMMAND} -E echo "Installer assets: installer/${OBXF_ASSETS_ZIP}")

    endif ()
        endif()
endfunction()
