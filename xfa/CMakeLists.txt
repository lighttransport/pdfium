add_library(pdfium_xfa)

# Add XFA components
add_subdirectory(fde)
add_subdirectory(fgas)
add_subdirectory(fwl)
add_subdirectory(fxfa)

target_include_directories(pdfium_xfa
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pdfium_xfa
    PUBLIC
        pdfium_core
        pdfium_fxjs
)

set_target_properties(pdfium_xfa PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)