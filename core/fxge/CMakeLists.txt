add_library(pdfium_fxge
    calculate_pitch.cpp
    cfx_color.cpp
    cfx_defaultrenderdevice.cpp
    cfx_drawutils.cpp
    cfx_face.cpp
    cfx_folderfontinfo.cpp
    cfx_font.cpp
    cfx_fontcache.cpp
    cfx_fontmapper.cpp
    cfx_fontmgr.cpp
    cfx_gemodule.cpp
    cfx_glyphbitmap.cpp
    cfx_glyphcache.cpp
    cfx_graphstate.cpp
    cfx_graphstatedata.cpp
    cfx_path.cpp
    cfx_renderdevice.cpp
    cfx_substfont.cpp
    cfx_unicodeencoding.cpp
    cfx_unicodeencodingex.cpp
    fx_font.cpp
    renderdevicedriver_iface.cpp
    scoped_font_transform.cpp
    text_char_pos.cpp
    text_glyph_pos.cpp
)

# Add subdirectories
add_subdirectory(agg)
add_subdirectory(dib)
add_subdirectory(fontdata)
add_subdirectory(freetype)

# Platform specific code
if(WIN32)
    add_subdirectory(win32)
    target_sources(pdfium_fxge PRIVATE
        cfx_windowsrenderdevice.cpp
    )
elseif(APPLE)
    add_subdirectory(apple)
elseif(ANDROID)
    add_subdirectory(android)
else()
    add_subdirectory(linux)
endif()

if(PDFIUM_USE_SKIA)
    add_subdirectory(skia)
endif()

target_include_directories(pdfium_fxge
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pdfium_fxge
    PUBLIC
        pdfium_fxcrt
)

set_target_properties(pdfium_fxge PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)