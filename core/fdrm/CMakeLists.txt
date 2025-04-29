add_library(pdfium_fdrm
    fx_crypt.cpp
    fx_crypt_aes.cpp
    fx_crypt_sha.cpp
)

target_include_directories(pdfium_fdrm
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pdfium_fdrm
    PUBLIC
        pdfium_fxcrt
)

set_target_properties(pdfium_fdrm PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)