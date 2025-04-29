add_library(pdfium_fpdfapi STATIC)

# Add all subdirectories
add_subdirectory(cmaps)
add_subdirectory(edit)
add_subdirectory(font)
add_subdirectory(page)
add_subdirectory(parser)
add_subdirectory(render)

target_include_directories(pdfium_fpdfapi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pdfium_fpdfapi
    PUBLIC
        pdfium_fxcrt
        pdfium_fxge
)