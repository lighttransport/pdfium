find_package(Freetype REQUIRED)

target_sources(pdfium_fxge
    PRIVATE
        fx_freetype.cpp
)

target_include_directories(pdfium_fxge
    PRIVATE
        ${FREETYPE_INCLUDE_DIRS}
)

target_link_libraries(pdfium_fxge
    PRIVATE
        ${FREETYPE_LIBRARIES}
)