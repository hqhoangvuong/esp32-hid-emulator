add_executable(check_script check_script.c)
target_link_libraries(check_script PRIVATE core)
add_test(NAME check_default_scripts COMMAND check_script ${FW}/macros/developer.txt ${FW}/macros/snippets.txt)
