# Due to the large number of files in this directory, we use file globbing
file(GLOB PARSER_SOURCES "*.cpp")

target_sources(pdfium_xfa
    PRIVATE
        ${PARSER_SOURCES}
)