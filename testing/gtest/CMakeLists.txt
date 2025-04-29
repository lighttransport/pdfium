add_library(gtest INTERFACE)

target_include_directories(gtest
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# For an interface library, we need to set these properties
# to ensure proper linking with GoogleTest
target_compile_definitions(gtest
    INTERFACE
        GTEST_HAS_RTTI=0
)