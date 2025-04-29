add_library(pdfium_sdk
    cpdfsdk_annot.cpp
    cpdfsdk_annotiteration.cpp
    cpdfsdk_annotiterator.cpp
    cpdfsdk_appstream.cpp
    cpdfsdk_baannot.cpp
    cpdfsdk_customaccess.cpp
    cpdfsdk_filewriteadapter.cpp
    cpdfsdk_formfillenvironment.cpp
    cpdfsdk_helpers.cpp
    cpdfsdk_interactiveform.cpp
    cpdfsdk_pageview.cpp
    cpdfsdk_pauseadapter.cpp
    cpdfsdk_renderpage.cpp
    cpdfsdk_widget.cpp
    fpdf_annot.cpp
    fpdf_attachment.cpp
    fpdf_catalog.cpp
    fpdf_dataavail.cpp
    fpdf_doc.cpp
    fpdf_editimg.cpp
    fpdf_editpage.cpp
    fpdf_editpath.cpp
    fpdf_edittext.cpp
    fpdf_ext.cpp
    fpdf_flatten.cpp
    fpdf_formfill.cpp
    fpdf_javascript.cpp
    fpdf_ppo.cpp
    fpdf_progressive.cpp
    fpdf_save.cpp
    fpdf_searchex.cpp
    fpdf_signature.cpp
    fpdf_structtree.cpp
    fpdf_sysfontinfo.cpp
    fpdf_text.cpp
    fpdf_thumbnail.cpp
    fpdf_transformpage.cpp 
    fpdf_view.cpp
)

# Add subdirectories
add_subdirectory(formfiller)
add_subdirectory(pwl)

if(PDFIUM_ENABLE_XFA)
    add_subdirectory(fpdfxfa)
endif()

target_include_directories(pdfium_sdk
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pdfium_sdk
    PUBLIC
        pdfium_core
        pdfium_fdrm
        pdfium_fpdfapi
        pdfium_fpdfdoc
        pdfium_fpdftext
        pdfium_fxcodec
        pdfium_fxcrt
        pdfium_fxge
)

set_target_properties(pdfium_sdk PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)