add_library(pdfium_fxjs
    cfx_globaldata.cpp
    cfx_keyvalue.cpp
    cfx_v8.cpp
    cfx_v8_array_buffer_allocator.cpp
    cfxjs_engine.cpp
    cjs_annot.cpp
    cjs_app.cpp
    cjs_border.cpp
    cjs_color.cpp
    cjs_console.cpp
    cjs_delaydata.cpp
    cjs_display.cpp
    cjs_document.cpp
    cjs_event.cpp
    cjs_event_context.cpp
    cjs_event_context_stub.cpp
    cjs_field.cpp
    cjs_font.cpp
    cjs_global.cpp
    cjs_globalarrays.cpp
    cjs_globalconsts.cpp
    cjs_highlight.cpp
    cjs_icon.cpp
    cjs_object.cpp
    cjs_position.cpp
    cjs_publicmethods.cpp
    cjs_result.cpp
    cjs_runtime.cpp
    cjs_runtimestub.cpp
    cjs_scalehow.cpp
    cjs_scalewhen.cpp
    cjs_style.cpp
    cjs_timerobj.cpp
    cjs_util.cpp
    cjs_zoomtype.cpp
    fx_date_helpers.cpp
    fxv8.cpp
    global_timer.cpp
    ijs_runtime.cpp
    js_define.cpp
    js_resources.cpp
)

target_include_directories(pdfium_fxjs
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(pdfium_fxjs
    PUBLIC
        pdfium_fxcrt
        pdfium_fpdfapi
)

if(PDFIUM_ENABLE_XFA)
    add_subdirectory(xfa)
endif()

add_subdirectory(gc)

set_target_properties(pdfium_fxjs PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)