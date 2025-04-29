# Add C++ headers to the public headers collection
set(PDFIUM_PUBLIC_CPP_HEADERS
    fpdf_deleters.h
    fpdf_scopers.h
)

# Install public C++ wrapper headers
install(FILES ${PDFIUM_PUBLIC_CPP_HEADERS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pdfium/cpp
    COMPONENT Devel
)

# Add these headers to the public interface
target_sources(pdfium_public
    INTERFACE
        ${PDFIUM_PUBLIC_CPP_HEADERS}
)