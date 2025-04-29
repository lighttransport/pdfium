add_library(pdfium_fxcrt
    binary_buffer.cpp
    bytestring.cpp
    cfx_bitstream.cpp
    cfx_datetime.cpp
    cfx_fileaccess_posix.cpp
    cfx_memorystream.cpp
    cfx_read_only_span_stream.cpp
    cfx_read_only_string_stream.cpp
    cfx_read_only_vector_stream.cpp
    cfx_seekablestreamproxy.cpp
    cfx_timer.cpp
    code_point_view.cpp
    fx_bidi.cpp
    fx_codepage.cpp
    fx_coordinates.cpp
    fx_extension.cpp
    fx_folder_posix.cpp
    fx_memory.cpp
    fx_memory_malloc.cpp
    fx_memory_pa.cpp
    fx_number.cpp
    fx_random.cpp
    fx_stream.cpp
    fx_string.cpp
    fx_system.cpp
    fx_unicode.cpp
    observed_ptr.cpp
    string_data_template.cpp
    string_template.cpp
    widestring.cpp
    widetext_buffer.cpp
)

if(WIN32)
    target_sources(pdfium_fxcrt PRIVATE
        cfx_fileaccess_windows.cpp
        fx_folder_windows.cpp
    )
endif()

target_include_directories(pdfium_fxcrt
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_compile_definitions(pdfium_fxcrt
    PRIVATE
        $<$<BOOL:${WIN32}>:NOMINMAX>
)

# Use position independent code for shared library builds
set_target_properties(pdfium_fxcrt PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)