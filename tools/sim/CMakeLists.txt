# Each unit test is tools/sim/test_<name>.c, linked against core and registered as test_<name>.
function(sim_test name)
    add_executable(test_${name} test_${name}.c)
    target_link_libraries(test_${name} PRIVATE core)
    add_test(NAME test_${name} COMMAND test_${name})
endfunction()

sim_test(sim)
sim_test(keys)
