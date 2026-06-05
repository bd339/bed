cmake_minimum_required(VERSION 3.20)
project(bed LANGUAGES C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(tree-sitter
	tree-sitter-c/src/parser.c
	tree-sitter/lib/src/lib.c
)
target_include_directories(tree-sitter PRIVATE
	tree-sitter-c/src
	tree-sitter/lib/src
	tree-sitter/lib/include
)

set(CXX_SOURCES
    main_win32.c
    buffer.c
    gui.c
    util.c
    syntax.c
    log.c
)
# TODO: rename CXX_SOURCES to .cpp so this can go away
set_source_files_properties(${CXX_SOURCES} PROPERTIES LANGUAGE CXX)
add_executable(bed WIN32 ${CXX_SOURCES})
target_compile_options(bed PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Wdouble-promotion
    -Wconversion
    -fsanitize=undefined
    -fsanitize-trap
)
target_include_directories(bed PRIVATE
    tree-sitter/lib/include
)
target_link_libraries(bed PRIVATE tree-sitter)

install(TARGETS bed DESTINATION "C:/bin")
