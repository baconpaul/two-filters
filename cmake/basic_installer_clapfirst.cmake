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
    elseif (WIN32)
        message(STATUS "Configuring for win installer")
        include(InnoSetup)
        install_inno_setup()


        execute_process(
                COMMAND
                "${CMAKE_BINARY_DIR}/innosetup-6.5.4.exe" /VERYSILENT
                /CURRENTUSER /DIR=${CMAKE_BINARY_DIR}/innosetup-6.5.4

                OUTPUT_VARIABLE isout
                ERROR_VARIABLE iserr
        )

        message(STATUS "OV = ${isout}")
        message(STATUS "EG = ${iserr}")

        find_program(
                INNOSETUP_COMPILER_EXECUTABLE
                iscc
                PATHS ${CMAKE_BINARY_DIR}/innosetup-6.5.4
        )

        cmake_path(REMOVE_EXTENSION INST_ZIP OUTPUT_VARIABLE WIN_INSTALLER)
        set(WINCOL ${CIN_ASSET_OUTPUT_DIRECTORY}/installer_copy)
        file(MAKE_DIRECTORY ${WINCOL})

        foreach (INST ${CIN_TARGETS})
            if (TARGET ${INST})
                message(STATUS "Copying ${INST} installer copy")
                add_custom_command(TARGET ${TGT}
                        POST_BUILD
                        USES_TERMINAL
                        COMMAND cmake -E echo "Staging " $<TARGET_FILE:${INST}> " to " ${WINCOL}
                        COMMAND cmake -E copy "$<TARGET_FILE:${INST}>" "${WINCOL}"
                )
            endif()
        endforeach()
        add_custom_command(
            TARGET ${TGT}
            POST_BUILD
            USES_TERMINAL
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory installer
            COMMAND 7z a -r installer/${INST_ZIP} ${WINCOL}
            COMMAND ${CMAKE_COMMAND} -E echo "ZIP Installer in: installer/${INST_ZIP}"
        )
         #   COMMAND ${INNOSETUP_COMPILER_EXECUTABLE}
         #       /O"${CMAKE_BINARY_DIR}/installer" /F"${WIN_INSTALLER}" /DName="${PRODUCT_NAME}"
         #       /DNameCondensed="${PRODUCT_NAME}" /DVersion="${GIT_COMMIT_HASH}"
         #       /DID="a74e3385-ee81-404d-b2ce-93452c512018"
         #       /DPublisher="BaconPaul"
         #       /DCLAP /DVST3 /DVST3_IS_SINGLE_FILE /DSA
         #       # /DIcon="${CMAKE_SOURCE_DIR}/resources/installer/logo.ico"
         #       # /DBanner="${CMAKE_SOURCE_DIR}/resources/installer/banner.png"
         #       /DArch="${INNOSETUP_ARCH_ID}"
         #       /DLicense="${CMAKE_SOURCE_DIR}/resources/LICENSE_GPL3"
         #       /DStagedAssets="${WINCOL}"
         #       "${INNOSETUP_INSTALL_SCRIPT}"

    else ()
        message(STATUS "Basic Installer: Target is installer/${OBXF_ZIP}")
        add_custom_command(
                TARGET ${TGT}
                POST_BUILD
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMAND ${CMAKE_COMMAND} -E make_directory installer
                COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/resources/LICENSE_GPL3 ${CIN_ASSET_OUTPUT_DIRECTORY}
                COMMAND ${CMAKE_COMMAND} -E tar cvf installer/${INST_ZIP} --format=zip ${CIN_ASSET_OUTPUT_DIRECTORY}/
                COMMAND ${CMAKE_COMMAND} -E echo "Installer in: installer/${INST_ZIP}")

        #add_custom_command(
        #        TARGET obxf-installer
        #        POST_BUILD
        #        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        #        USES_TERMINAL
        #        COMMAND scripts/installer_linux/make_deb.sh ${OBXF_PRODUCT_DIR} ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/installer "${OBXF_DATE}-${GIT_COMMIT_HASH}"
        #)

    endif ()
endfunction()
