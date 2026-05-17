add_executable(example_parse_outlook parse_outlook.cpp)
target_link_libraries(example_parse_outlook PRIVATE spc)

add_executable(example_static_feed static_feed.cpp)
target_link_libraries(example_static_feed PRIVATE spc)

add_executable(example_arcgis arcgis.cpp)
target_link_libraries(example_arcgis PRIVATE spc)

add_executable(example_archive archive.cpp)
target_link_libraries(example_archive PRIVATE spc)
