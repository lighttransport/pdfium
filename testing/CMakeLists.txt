add_executable(pdfium_test pdfium_test.cc
    command_line_helpers.cpp
    test_loader.cpp
    font_renamer.cpp
    helpers/dump.cpp
    helpers/event.cpp
    helpers/page_renderer.cpp
    helpers/write.cpp
    utils/file_util.cpp
    utils/hash.cpp
    utils/path_service.cpp
)

target_link_libraries(pdfium_test PRIVATE pdfium)

# Testing utilities and test harnesses for unit tests
if(BUILD_TESTING)
    add_library(pdfium_testing
        embedder_test.cpp
        embedder_test_constants.cpp
        embedder_test_environment.cpp
        fx_string_testhelpers.cpp
        string_write_stream.cpp
        scoped_locale.cc
        scoped_set_tz.cpp
        test_fonts.cpp
        utils/file_util.cpp
        utils/path_service.cpp
    )
    
    target_link_libraries(pdfium_testing PUBLIC pdfium)
    
    # Add unittest support with Google Test
    add_subdirectory(gtest)
    add_subdirectory(gmock)
    
    # Build unit tests
    add_executable(pdfium_unittests
        unit_test_main.cpp
        # Add specific unit test files if needed
    )
    
    target_link_libraries(pdfium_unittests
        PRIVATE
            pdfium
            pdfium_testing
            gtest
            gmock
    )
    
    # Build embedder tests
    add_executable(pdfium_embeddertests
        embedder_test_main.cpp
        # Add specific embedder test files if needed
    )
    
    target_link_libraries(pdfium_embeddertests
        PRIVATE
            pdfium
            pdfium_testing
            gtest
            gmock
    )
endif()