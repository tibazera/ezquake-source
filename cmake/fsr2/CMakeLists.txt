# Build the pinned AMD API/backend unmodified; the sample framework is not needed.
set(FFX_FSR2_API_DX12 OFF)
set(FFX_FSR2_API_VK ON)
set(FSR2_BUILD_AS_DLL OFF)
set(FSR2_AUTO_COMPILE_SHADERS ON)
# AMD 2.2.1's platform detection only recognizes this exact linker flag under Ninja.
set(CMAKE_EXE_LINKER_FLAGS "/machine:x64")
add_subdirectory(${PROJECT_SOURCE_DIR}/external/fsr2/src/ffx-fsr2-api
                 ${CMAKE_CURRENT_BINARY_DIR}/api)
foreach(target ffx_fsr2_api_x64 ffx_fsr2_api_vk_x64)
    set_target_properties(${target} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/Debug
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/Release
        ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_CURRENT_BINARY_DIR}/RelWithDebInfo)
endforeach()
