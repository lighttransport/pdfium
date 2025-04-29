add_library(pdfium_constants
    access_permissions.h
    annotation_common.cpp
    annotation_common.h
    annotation_flags.h
    appearance.cpp
    appearance.h
    ascii.h
    font_encodings.cpp
    font_encodings.h
    form_fields.cpp
    form_fields.h
    form_flags.h
    page_object.cpp
    page_object.h
    stream_dict_common.cpp
    stream_dict_common.h
    transparency.cpp
    transparency.h
)

target_include_directories(pdfium_constants
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

set_target_properties(pdfium_constants PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)