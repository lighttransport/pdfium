add_library(pdfium_fxcodec
    cfx_codec_memory.cpp
    data_and_bytes_consumed.cpp
    fx_codec.cpp
    progressive_decoder.cpp
    scanlinedecoder.cpp
)

# Add codec subdirectories
add_subdirectory(basic)
add_subdirectory(bmp)
add_subdirectory(fax)
add_subdirectory(flate)
add_subdirectory(gif)
add_subdirectory(icc)
add_subdirectory(jbig2)
add_subdirectory(jpeg)
add_subdirectory(jpx)
add_subdirectory(png)
add_subdirectory(tiff)

target_include_directories(pdfium_fxcodec
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pdfium_fxcodec
    PUBLIC
        pdfium_fxcrt
)

set_target_properties(pdfium_fxcodec PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)