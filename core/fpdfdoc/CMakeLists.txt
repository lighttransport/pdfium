add_library(pdfium_fpdfdoc
    cpdf_aaction.cpp
    cpdf_action.cpp
    cpdf_annot.cpp
    cpdf_annotlist.cpp
    cpdf_apsettings.cpp
    cpdf_bafontmap.cpp
    cpdf_bookmark.cpp
    cpdf_bookmarktree.cpp
    cpdf_color_utils.cpp
    cpdf_defaultappearance.cpp
    cpdf_dest.cpp
    cpdf_filespec.cpp
    cpdf_formcontrol.cpp
    cpdf_formfield.cpp
    cpdf_generateap.cpp
    cpdf_icon.cpp
    cpdf_iconfit.cpp
    cpdf_interactiveform.cpp
    cpdf_link.cpp
    cpdf_linklist.cpp
    cpdf_metadata.cpp
    cpdf_nametree.cpp
    cpdf_numbertree.cpp
    cpdf_pagelabel.cpp
    cpdf_structelement.cpp
    cpdf_structtree.cpp
    cpdf_viewerpreferences.cpp
    cpvt_fontmap.cpp
    cpvt_section.cpp
    cpvt_variabletext.cpp
    cpvt_wordinfo.cpp
)

target_include_directories(pdfium_fpdfdoc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pdfium_fpdfdoc
    PUBLIC
        pdfium_fxcrt
        pdfium_fpdfapi
)

set_target_properties(pdfium_fpdfdoc PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)