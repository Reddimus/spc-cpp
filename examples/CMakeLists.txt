add_executable(example_parse_outlook parse_outlook.cpp)
target_link_libraries(example_parse_outlook PRIVATE spc)

# PR-2 adds: example_static_feed, example_arcgis, example_archive
# (and the matching Makefile run-* targets).
