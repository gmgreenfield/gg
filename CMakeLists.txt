cmake_minimum_required(VERSION 3.16)

project(gg-editor LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(editor_core STATIC src/editor.c)
target_include_directories(editor_core PUBLIC src)

add_executable(gg src/main.c src/file_io.c src/terminal.c)
target_link_libraries(gg PRIVATE editor_core)

include(CTest)
if(BUILD_TESTING)
    # Compile the same file-I/O source with fault injection only for the tests.
    add_library(test_file_io OBJECT src/file_io.c)
    target_compile_definitions(test_file_io PRIVATE
        fwrite=test_save_fwrite
        rename=test_save_rename
    )

    add_executable(editor_tests
        tests/test_main.c
        tests/test_helpers.c
        tests/test_buffer.c
        tests/test_navigation.c
        tests/test_search.c
        tests/test_file_io.c
        $<TARGET_OBJECTS:test_file_io>
    )
    target_link_libraries(editor_tests PRIVATE editor_core)
    add_test(NAME editor_tests COMMAND editor_tests)
endif()
