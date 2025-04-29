add_library(pdfium_fpdftext
    cpdf_linkextract.cpp
    cpdf_textpage.cpp
    cpdf_textpagefind.cpp
    unicodenormalizationdata.cpp
)

target_include_directories(pdfium_fpdftext
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pdfium_fpdftext
    PUBLIC
        pdfium_fxcrt
        pdfium_fpdfapi
)

set_target_properties(pdfium_fpdftext PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)