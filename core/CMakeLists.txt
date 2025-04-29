# Core components
add_subdirectory(fxcrt)    # Runtime - fundamental component
add_subdirectory(fdrm)     # DRM
add_subdirectory(fxcodec)  # Codecs
add_subdirectory(fxge)     # Graphics engine
add_subdirectory(fpdfapi)  # PDF API
add_subdirectory(fpdfdoc)  # PDF document
add_subdirectory(fpdftext) # PDF text

# Create an interface library to group all core components
add_library(pdfium_core INTERFACE)
target_link_libraries(pdfium_core INTERFACE
    pdfium_fxcrt
    pdfium_fdrm
    pdfium_fxcodec
    pdfium_fxge
    pdfium_fpdfapi
    pdfium_fpdfdoc
    pdfium_fpdftext
)

# Export core include directories
target_include_directories(pdfium_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)