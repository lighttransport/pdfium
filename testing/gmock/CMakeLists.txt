add_library(gmock INTERFACE)

target_include_directories(gmock
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link to gtest as gmock is built on top of gtest
target_link_libraries(gmock
    INTERFACE
        gtest
)

# For an interface library, we need to set these properties
target_compile_definitions(gmock
    INTERFACE
        GMOCK_HAS_RTTI=0
)