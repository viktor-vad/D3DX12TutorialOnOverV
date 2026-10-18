# CMakeList.txt : CMake project for D3DX12TutorialOnOverV, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 4.00)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("D3DX12TutorialOnOverV")

find_package(SDL3 CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(directx12-agility CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)
find_package(winpixevent CONFIG REQUIRED)

function (compile_shader_file)
	cmake_parse_arguments ("SHADER" "" "TARGET;FILE;STAGE;ENTRY" ""
		${ARGN})
#	message("UNPARSED_ARGUMENTS ${SHADER_UNPARSED_ARGUMENTS}")
	message("TARGET ${SHADER_TARGET}")
#	message("SHADER_FILE ${SHADER_FILE}") 
#	message("SHADER_STAGE  ${SHADER_STAGE}")
#	message("SHADER_ENTRY ${SHADER_ENTRY}")
	
	set_source_files_properties(
    "${SHADER_FILE}"
    PROPERTIES
        HEADER_FILE_ONLY TRUE
	)

	target_sources(${SHADER_TARGET}
		PRIVATE
        "${SHADER_FILE}"
	)

	cmake_path(GET SHADER_FILE STEM SHADER_BASEFILENAME)
	
	set(SHADER_OUTPUT_TARGET ${SHADER_TARGET}_${SHADER_BASEFILENAME}_${SHADER_STAGE})

	set(SLANGC_OPTIONS
    "$<$<CONFIG:Debug>:-O0>"
	"$<$<CONFIG:Debug>:-g3>"

    "$<$<CONFIG:Release>:-O3>"
	"$<$<CONFIG:Release>:-g0>"

	"$<$<CONFIG:RelWithDebInfo>:-O2>"
	"$<$<CONFIG:RelWithDebInfo>:-g2>"

	"$<$<CONFIG:MinSizeRel>:-O2>"
	"$<$<CONFIG:MinSizeRel>:-g0>"
	)

	add_custom_target(${SHADER_OUTPUT_TARGET}
	
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${SHADER_TARGET}>/shaders"
		COMMAND slang::slangc ${SLANGC_OPTIONS} -profile sm_6_0 -stage ${SHADER_STAGE} -entry ${SHADER_ENTRY} -target dxil -o "$<TARGET_FILE_DIR:${SHADER_TARGET}>/shaders/${SHADER_BASEFILENAME}_${SHADER_STAGE}.dxil" ${SHADER_FILE}
		DEPENDS ${SHADER_FILE}
		VERBATIM
		COMMAND_EXPAND_LISTS
	)

	add_dependencies(${SHADER_TARGET} ${SHADER_OUTPUT_TARGET})

endfunction()

add_subdirectory(00_base_code)
add_subdirectory(01_device_creation)
add_subdirectory(02_no_buffer_triangle)
add_subdirectory(03_buffer_triangle)
# TODO: Add tests and install targets if needed.
