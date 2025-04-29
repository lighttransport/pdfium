target_sources(pdfium_fpdfapi
    PRIVATE
        fpdf_cmaps.cpp
)

# Add CMap subdirectories
add_subdirectory(CNS1)
add_subdirectory(GB1)
add_subdirectory(Japan1)
add_subdirectory(Korea1)