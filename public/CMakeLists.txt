add_library(pdfium_public INTERFACE)

# Create a collection of the public API headers
set(PDFIUM_PUBLIC_HEADERS
    fpdf_annot.h
    fpdf_attachment.h
    fpdf_catalog.h
    fpdf_dataavail.h
    fpdf_doc.h
    fpdf_edit.h
    fpdf_ext.h
    fpdf_flatten.h
    fpdf_formfill.h
    fpdf_fwlevent.h
    fpdf_javascript.h
    fpdf_ppo.h
    fpdf_progressive.h
    fpdf_save.h
    fpdf_searchex.h
    fpdf_signature.h
    fpdf_structtree.h
    fpdf_sysfontinfo.h
    fpdf_text.h
    fpdf_thumbnail.h
    fpdf_transformpage.h
    fpdfview.h
)

# Public C++ wrapper headers
add_subdirectory(cpp)

# Set up include directories for the public interface
target_include_directories(pdfium_public
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

# Create the main PDFium library that links everything together
add_library(pdfium)
target_link_libraries(pdfium
    PUBLIC
        pdfium_public
    PRIVATE
        pdfium_core
        pdfium_fdrm
        pdfium_fpdfapi
        pdfium_fpdfdoc
        pdfium_fpdftext
        pdfium_fxcodec
        pdfium_fxcrt
        pdfium_fxge
        pdfium_sdk
)

if(PDFIUM_ENABLE_V8)
    target_link_libraries(pdfium
        PRIVATE
            pdfium_fxjs
    )
endif()

if(PDFIUM_ENABLE_XFA)
    target_link_libraries(pdfium
        PRIVATE
            pdfium_xfa
    )
endif()

# Installation rules
include(GNUInstallDirs)

# Install the main PDFium library
install(TARGETS pdfium
    EXPORT pdfium-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install public headers
install(FILES ${PDFIUM_PUBLIC_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pdfium
)

# Generate and install package configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/pdfium-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/pdfium-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pdfium
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/pdfium-config-version.cmake
    VERSION 1.0
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/pdfium-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/pdfium-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pdfium
)

install(EXPORT pdfium-targets
    FILE pdfium-targets.cmake
    NAMESPACE pdfium::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/pdfium
)