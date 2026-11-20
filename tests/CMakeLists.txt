# Copyright (c) 2026 The kirimaspice Authors
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Test configuration
# Timeout: 20 minutes (1200 seconds) for long-running tests
set(CTEST_TEST_TIMEOUT 1200)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Tests write their files under the build tree, each in a directory of its
# own (support/test_output.hpp)
set(KIRIMASPICE_TEST_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/test_output)
add_compile_definitions(KIRIMASPICE_TEST_OUTPUT_DIR="${KIRIMASPICE_TEST_OUTPUT_DIR}")
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/support)

add_executable(test_analysis_edge_cases unit/test_analysis_edge_cases.cpp)
target_link_libraries(test_analysis_edge_cases PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_analysis_edge_cases PROPERTIES TIMEOUT 1200)

add_test(NAME python_tools_edge_cases
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/scripts/test_tool_edge_cases.py
        --binary $<TARGET_FILE:kirimaspice>
)
set_tests_properties(python_tools_edge_cases PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME cli_analyses
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/scripts/test_cli_analyses.py
        --binary $<TARGET_FILE:kirimaspice>
)

# Python integration tests (REPL + PyAPI)
add_test(NAME python_tools_pyapi
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/scripts/test_pyapi.py
        --binary $<TARGET_FILE:kirimaspice>
        --netlist ${CMAKE_SOURCE_DIR}/tests/netlists/repl_rc.sp
)
set_tests_properties(python_tools_pyapi PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR}"
)

add_test(NAME python_tools_repl
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/scripts/test_repl.py
        --binary $<TARGET_FILE:kirimaspice>
        --netlist ${CMAKE_SOURCE_DIR}/tests/netlists/repl_rc.sp
)
set_tests_properties(python_tools_repl PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR}"
)

add_test(NAME python_tools_ngspice_shim
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/scripts/test_ngspice_shim.py
        --out-dir ${KIRIMASPICE_TEST_OUTPUT_DIR}/python_tools_ngspice_shim
)
set_tests_properties(python_tools_ngspice_shim PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(NAME bsim4_dc_smoke
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/scripts/test_bsim4_dc.py
        --binary $<TARGET_FILE:kirimaspice>
        --template ${CMAKE_SOURCE_DIR}/tests/netlists/bsim4_qa_dc.template.sp
        --json-out ${KIRIMASPICE_TEST_OUTPUT_DIR}/bsim4_dc_smoke/bsim4_dc_compare.json
)
set_tests_properties(bsim4_dc_smoke PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR}"
)

add_test(NAME cli_help_long
    COMMAND $<TARGET_FILE:kirimaspice> --help
)
set_tests_properties(cli_help_long PROPERTIES
    PASS_REGULAR_EXPRESSION "Usage: .*kirimaspice.*<netlist\\.cir>"
    FAIL_REGULAR_EXPRESSION "Loading netlist|Failed to parse|Error:"
)

add_test(NAME cli_help_short
    COMMAND $<TARGET_FILE:kirimaspice> -h
)
set_tests_properties(cli_help_short PROPERTIES
    PASS_REGULAR_EXPRESSION "Options:"
    FAIL_REGULAR_EXPRESSION "Loading netlist|Failed to parse|Error:"
)

# gnd in any case is ground (docs/spec/NETLIST_CONVENTIONS.md, rule 5): the
# printed operating point has no V(GND), and the divider puts mid at 0.75 V.
# V1 delivers 0.25 mA, a current of -0.25 mA (rule 3).
add_test(NAME cli_op_gnd_is_ground
    COMMAND $<TARGET_FILE:kirimaspice> ${CMAKE_SOURCE_DIR}/tests/netlists/gnd_is_ground_op.sp
)
set_tests_properties(cli_op_gnd_is_ground PROPERTIES
    PASS_REGULAR_EXPRESSION "V\\(a\\) = 1 V\nV\\(mid\\) = 0\\.75 V\nI\\(V1\\) = -0\\.00025 A"
)

# The netlists of examples/, run by the program and checked against the
# analytic solutions that examples/README.md and the manuals quote.
foreach(example_case divider params_subckt rc_step rc_discharge rc_lowpass_ac diode_iv)
    add_test(NAME example_${example_case}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/scripts/test_examples.py
            --binary $<TARGET_FILE:kirimaspice>
            --examples ${CMAKE_SOURCE_DIR}/examples
            --case ${example_case}
    )
endforeach()

# .TEMP with one value and with several (tests/scripts/test_cli_temperature.py)
foreach(temperature_case single list)
    add_test(NAME cli_temperature_${temperature_case}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/scripts/test_cli_temperature.py
            --binary $<TARGET_FILE:kirimaspice>
            --case ${temperature_case}
    )
endforeach()

#==============================================================================
# Unit Tests
#==============================================================================
add_executable(test_types
    unit/test_types.cpp
)

target_link_libraries(test_types
    PRIVATE
        kirimaspice_lib
        GTest::gtest_main
)

gtest_discover_tests(test_types
    PROPERTIES
        TIMEOUT 1200
)

# Additional unit tests
add_executable(test_parser unit/test_parser.cpp)
target_link_libraries(test_parser PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_parser PROPERTIES TIMEOUT 1200)

add_executable(test_param_sweep unit/test_param_sweep.cpp)
target_link_libraries(test_param_sweep PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_param_sweep PROPERTIES TIMEOUT 1200)

add_executable(test_transfer_function unit/test_transfer_function.cpp)
target_link_libraries(test_transfer_function PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_transfer_function PROPERTIES TIMEOUT 1200)

add_executable(test_fourier_analyzer unit/test_fourier_analyzer.cpp)
target_link_libraries(test_fourier_analyzer PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_fourier_analyzer PROPERTIES TIMEOUT 1200)

add_executable(test_pole_zero unit/test_pole_zero.cpp)
target_link_libraries(test_pole_zero PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_pole_zero PROPERTIES TIMEOUT 1200)

add_executable(test_distortion unit/test_distortion.cpp)
target_link_libraries(test_distortion PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_distortion PROPERTIES TIMEOUT 1200)

add_executable(test_monte_carlo_parser unit/test_monte_carlo_parser.cpp)
target_link_libraries(test_monte_carlo_parser PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_monte_carlo_parser PROPERTIES TIMEOUT 1200)

add_executable(test_monte_carlo_paths unit/test_monte_carlo_paths.cpp)
target_link_libraries(test_monte_carlo_paths PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_monte_carlo_paths PROPERTIES TIMEOUT 1200)

add_executable(test_subcircuit_parser unit/test_subcircuit_parser.cpp)
target_link_libraries(test_subcircuit_parser PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_subcircuit_parser PROPERTIES TIMEOUT 1200)

add_executable(test_subcircuit_expansion unit/test_subcircuit_expansion.cpp)
target_link_libraries(test_subcircuit_expansion PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_subcircuit_expansion PROPERTIES TIMEOUT 1200)

add_executable(test_global_directive unit/test_global_directive.cpp)
target_link_libraries(test_global_directive PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_global_directive PROPERTIES TIMEOUT 1200)

add_executable(test_circuit unit/test_circuit.cpp)
target_link_libraries(test_circuit PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_circuit PROPERTIES TIMEOUT 1200)

add_executable(test_dc_solver unit/test_dc_solver.cpp)
target_link_libraries(test_dc_solver PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_dc_solver PROPERTIES TIMEOUT 1200)

add_executable(test_dc_convergence_helpers unit/test_dc_convergence_helpers.cpp)
target_link_libraries(test_dc_convergence_helpers PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_dc_convergence_helpers PROPERTIES TIMEOUT 1200)

add_executable(test_source_stepping unit/test_source_stepping.cpp)
target_link_libraries(test_source_stepping PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_source_stepping PROPERTIES TIMEOUT 1200)

add_executable(test_gmin_stepping unit/test_gmin_stepping.cpp)
target_link_libraries(test_gmin_stepping PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_gmin_stepping PROPERTIES TIMEOUT 1200)

add_executable(test_dynamic_gmin unit/test_dynamic_gmin.cpp)
target_link_libraries(test_dynamic_gmin PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_dynamic_gmin PROPERTIES TIMEOUT 1200)

# Pattern difference tests
add_executable(test_pattern_difference unit/test_pattern_difference.cpp)
target_link_libraries(test_pattern_difference PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_pattern_difference PROPERTIES TIMEOUT 1200)

# Selective reanalysis tests
add_executable(test_selective_reanalysis unit/test_selective_reanalysis.cpp)
target_link_libraries(test_selective_reanalysis PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_selective_reanalysis PROPERTIES TIMEOUT 1200)

add_executable(test_voltage_limiting unit/test_voltage_limiting.cpp)
target_link_libraries(test_voltage_limiting PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_voltage_limiting PROPERTIES TIMEOUT 1200)

add_executable(test_dc_sweep unit/test_dc_sweep.cpp)
target_link_libraries(test_dc_sweep PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_dc_sweep PROPERTIES TIMEOUT 1200)

add_executable(test_transient_solver unit/test_transient_solver.cpp)
target_link_libraries(test_transient_solver PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_transient_solver PROPERTIES TIMEOUT 1200)

add_executable(test_predictor_corrector_lte unit/test_predictor_corrector_lte.cpp)
target_link_libraries(test_predictor_corrector_lte PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_predictor_corrector_lte PROPERTIES TIMEOUT 1200)

add_executable(test_trapezoidal_variable_step unit/test_trapezoidal_variable_step.cpp)
target_link_libraries(test_trapezoidal_variable_step PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_trapezoidal_variable_step PROPERTIES TIMEOUT 1200)

add_executable(test_trap_fixed_uic unit/test_trap_fixed_uic.cpp)
target_link_libraries(test_trap_fixed_uic PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_trap_fixed_uic PROPERTIES TIMEOUT 1200)

add_executable(test_diode unit/test_diode.cpp)
target_link_libraries(test_diode PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_diode PROPERTIES TIMEOUT 1200)

add_executable(test_bjt unit/test_bjt.cpp)
target_link_libraries(test_bjt PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bjt PROPERTIES TIMEOUT 1200)

add_executable(test_bjt_pnp_dc unit/test_bjt_pnp_dc.cpp)
target_link_libraries(test_bjt_pnp_dc PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bjt_pnp_dc PROPERTIES TIMEOUT 1200)

add_executable(test_bjt_early_effect unit/test_bjt_early_effect.cpp)
target_link_libraries(test_bjt_early_effect PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bjt_early_effect PROPERTIES TIMEOUT 1200)

add_executable(test_bjt_high_current unit/test_bjt_high_current.cpp)
target_link_libraries(test_bjt_high_current PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bjt_high_current PROPERTIES TIMEOUT 1200)

add_executable(test_bjt_base_resistance unit/test_bjt_base_resistance.cpp)
target_link_libraries(test_bjt_base_resistance PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bjt_base_resistance PROPERTIES TIMEOUT 1200)

add_executable(test_bjt_nonideal_current unit/test_bjt_nonideal_current.cpp)
target_link_libraries(test_bjt_nonideal_current PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bjt_nonideal_current PROPERTIES TIMEOUT 1200)

add_executable(test_mosfet unit/test_mosfet.cpp)
target_link_libraries(test_mosfet PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_mosfet PROPERTIES TIMEOUT 1200)

add_executable(test_mosfet_p unit/test_mosfet_p.cpp)
target_link_libraries(test_mosfet_p PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_mosfet_p PROPERTIES TIMEOUT 1200)

add_executable(test_mosfet_transient unit/test_mosfet_transient.cpp)
target_link_libraries(test_mosfet_transient PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_mosfet_transient PROPERTIES TIMEOUT 1200)

add_executable(test_mosfet_level3 unit/test_mosfet_level3.cpp)
target_link_libraries(test_mosfet_level3 PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_mosfet_level3 PROPERTIES TIMEOUT 1200)

add_executable(test_mosfet_level3_parser unit/test_mosfet_level3_parser.cpp)
target_link_libraries(test_mosfet_level3_parser PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_mosfet_level3_parser PROPERTIES TIMEOUT 1200)

add_executable(test_mosfet_level3_capacitance unit/test_mosfet_level3_capacitance.cpp)
target_link_libraries(test_mosfet_level3_capacitance PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_mosfet_level3_capacitance PROPERTIES TIMEOUT 1200)

add_executable(test_cmos_circuits unit/test_cmos_circuits.cpp)
target_link_libraries(test_cmos_circuits PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_cmos_circuits PROPERTIES TIMEOUT 1200)

# AC Validation Tests (requires ngspice reference data)
add_executable(test_rc_ac_validation unit/test_rc_ac_validation.cpp)
target_link_libraries(test_rc_ac_validation PRIVATE kirimaspice_lib GTest::gtest_main)
target_include_directories(test_rc_ac_validation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/validation)
gtest_discover_tests(test_rc_ac_validation
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(test_device_ac_validation unit/test_device_ac_validation.cpp)
target_link_libraries(test_device_ac_validation PRIVATE kirimaspice_lib GTest::gtest_main)
target_include_directories(test_device_ac_validation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/validation)
gtest_discover_tests(test_device_ac_validation
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(test_filter_ac_validation unit/test_filter_ac_validation.cpp)
target_link_libraries(test_filter_ac_validation PRIVATE kirimaspice_lib GTest::gtest_main)
target_include_directories(test_filter_ac_validation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/validation)
gtest_discover_tests(test_filter_ac_validation
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(test_multistage_amplifier_ac_validation unit/test_multistage_amplifier_ac_validation.cpp)
target_link_libraries(test_multistage_amplifier_ac_validation PRIVATE kirimaspice_lib GTest::gtest_main)
target_include_directories(test_multistage_amplifier_ac_validation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/validation)
gtest_discover_tests(test_multistage_amplifier_ac_validation
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(test_ssf_writer unit/test_ssf_writer.cpp)
target_link_libraries(test_ssf_writer PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_ssf_writer PROPERTIES TIMEOUT 1200)

add_executable(test_bdf_order_comparison unit/test_bdf_order_comparison.cpp)
target_link_libraries(test_bdf_order_comparison PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bdf_order_comparison PROPERTIES TIMEOUT 1200)

add_executable(test_rlc_damping unit/test_rlc_damping.cpp)
target_link_libraries(test_rlc_damping PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_rlc_damping PROPERTIES TIMEOUT 1200)

add_executable(test_lte_tolerance unit/test_lte_tolerance.cpp)
target_link_libraries(test_lte_tolerance PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_lte_tolerance PROPERTIES TIMEOUT 1200)

add_executable(test_multistage_rc unit/test_multistage_rc.cpp)
target_link_libraries(test_multistage_rc PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_multistage_rc PROPERTIES TIMEOUT 1200)

add_executable(test_initial_timestep_control unit/test_initial_timestep_control.cpp)
target_link_libraries(test_initial_timestep_control PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_initial_timestep_control PROPERTIES TIMEOUT 1200)

add_executable(test_tr_bdf2_accuracy unit/test_tr_bdf2_accuracy.cpp)
target_link_libraries(test_tr_bdf2_accuracy PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_tr_bdf2_accuracy PROPERTIES TIMEOUT 1200)

add_executable(test_adaptive_physical_lte unit/test_adaptive_physical_lte.cpp)
target_link_libraries(test_adaptive_physical_lte PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_adaptive_physical_lte PROPERTIES TIMEOUT 1200)

add_executable(test_rlc_high_q unit/test_rlc_high_q.cpp)
target_link_libraries(test_rlc_high_q PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_rlc_high_q PROPERTIES TIMEOUT 1200)

add_executable(test_rlc_high_q_final unit/test_rlc_high_q_final.cpp)
target_link_libraries(test_rlc_high_q_final PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_rlc_high_q_final PROPERTIES TIMEOUT 1200)

add_executable(test_rlc_high_q_bdf2_ngspice unit/test_rlc_high_q_bdf2_ngspice.cpp)
target_link_libraries(test_rlc_high_q_bdf2_ngspice PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_rlc_high_q_bdf2_ngspice PROPERTIES TIMEOUT 1200)

add_executable(test_rlc_high_q_bdf2_adaptive unit/test_rlc_high_q_bdf2_adaptive.cpp)
target_link_libraries(test_rlc_high_q_bdf2_adaptive PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_rlc_high_q_bdf2_adaptive PROPERTIES TIMEOUT 1200)

add_executable(test_rlc_bdf2_qsweep unit/test_rlc_bdf2_qsweep.cpp)
target_link_libraries(test_rlc_bdf2_qsweep PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_rlc_bdf2_qsweep PROPERTIES TIMEOUT 1200)

add_executable(test_rlc_high_q_bdf3_ngspice unit/test_rlc_high_q_bdf3_ngspice.cpp)
target_link_libraries(test_rlc_high_q_bdf3_ngspice PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_rlc_high_q_bdf3_ngspice PROPERTIES TIMEOUT 1200)

add_executable(test_rlc_high_q_bdf3_adaptive unit/test_rlc_high_q_bdf3_adaptive.cpp)
target_link_libraries(test_rlc_high_q_bdf3_adaptive PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_rlc_high_q_bdf3_adaptive PROPERTIES TIMEOUT 1200)

add_executable(test_rlc_bdf3_qsweep unit/test_rlc_bdf3_qsweep.cpp)
target_link_libraries(test_rlc_bdf3_qsweep PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_rlc_bdf3_qsweep PROPERTIES TIMEOUT 1200)

# Complex number matrix types
add_executable(test_complex_matrix_types unit/test_complex_matrix_types.cpp)
target_link_libraries(test_complex_matrix_types PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_complex_matrix_types PROPERTIES TIMEOUT 1200)

# AC analysis solver
add_executable(test_ac_solver unit/test_ac_solver.cpp)
target_link_libraries(test_ac_solver PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_ac_solver PROPERTIES TIMEOUT 1200)

# AC CSV writer
add_executable(test_ac_csv_writer unit/test_ac_csv_writer.cpp)
target_link_libraries(test_ac_csv_writer PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_ac_csv_writer PROPERTIES TIMEOUT 1200)

# Refactorize pattern change detection (asks for SuperLU_MT, which is not built, so it runs on KLU)
add_executable(test_solver_refactorize_pattern_change unit/test_solver_refactorize_pattern_change.cpp)
target_link_libraries(test_solver_refactorize_pattern_change PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_solver_refactorize_pattern_change PROPERTIES TIMEOUT 1200)

# Controlled sources (VCVS, CCCS, VCCS, CCVS)
add_executable(test_controlled_sources unit/test_controlled_sources.cpp)
target_link_libraries(test_controlled_sources PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_controlled_sources PROPERTIES TIMEOUT 1200)

add_executable(test_mutual_inductance unit/test_mutual_inductance.cpp)
target_link_libraries(test_mutual_inductance PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_mutual_inductance
    PROPERTIES TIMEOUT 1200
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Mutual inductance transient smoke (2025-11-29)
add_executable(test_mutual_inductance_transient unit/test_mutual_inductance_transient.cpp)
target_link_libraries(test_mutual_inductance_transient PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_mutual_inductance_transient
    PROPERTIES TIMEOUT 1200
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# AC + Noise regression (RC lowpass)
add_executable(test_noise_ac_regression unit/test_noise_ac_regression.cpp)
target_link_libraries(test_noise_ac_regression PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_noise_ac_regression
    PROPERTIES TIMEOUT 1200
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Parameter sweep perf/no-step regression
add_executable(test_parameter_sweep_perf unit/test_parameter_sweep_perf.cpp)
target_link_libraries(test_parameter_sweep_perf PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_parameter_sweep_perf
    PROPERTIES TIMEOUT 1200
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(gen_mutual_wave ../tools/gen_mutual_wave.cpp)
target_link_libraries(gen_mutual_wave PRIVATE kirimaspice_lib)

add_executable(test_noise_analysis unit/test_noise_analysis.cpp)
target_link_libraries(test_noise_analysis PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_noise_analysis PROPERTIES TIMEOUT 1200)

# MOSFET transient analysis tests (already in test_mosfet_transient.cpp)

# .measure functionality
add_executable(test_measure unit/test_measure.cpp)
target_link_libraries(test_measure PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_measure PROPERTIES TIMEOUT 1200)

# JFET devices
add_executable(test_jfet unit/test_jfet.cpp)
target_link_libraries(test_jfet PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_jfet PROPERTIES TIMEOUT 1200)

# VDMOS devices
add_executable(test_vdmos unit/test_vdmos.cpp)
target_link_libraries(test_vdmos PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_vdmos PROPERTIES TIMEOUT 1200)

# IGBT devices
add_executable(test_igbt unit/test_igbt.cpp)
target_link_libraries(test_igbt PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_igbt PROPERTIES TIMEOUT 1200)

# SCR (Silicon Controlled Rectifier / Thyristor) devices
add_executable(test_scr unit/test_scr.cpp)
target_link_libraries(test_scr PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_scr PROPERTIES TIMEOUT 1200)

# BSIM3 devices (skeleton)
add_executable(test_bsim3 unit/test_bsim3.cpp)
target_link_libraries(test_bsim3 PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bsim3 PROPERTIES TIMEOUT 1200)

add_executable(test_bsim1 unit/test_bsim1.cpp)
target_link_libraries(test_bsim1 PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bsim1 PROPERTIES TIMEOUT 1200)

# Sensitivity Analysis
add_executable(test_sensitivity_analyzer unit/test_sensitivity_analyzer.cpp)
target_link_libraries(test_sensitivity_analyzer PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_sensitivity_analyzer PROPERTIES TIMEOUT 1200)

# Lossless Transmission Line
add_executable(test_tline unit/test_tline.cpp)
target_link_libraries(test_tline PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_tline PROPERTIES TIMEOUT 1200)

# Lumped LC Ladder Transmission Line
add_executable(test_tline_lumped unit/test_tline_lumped.cpp)
target_link_libraries(test_tline_lumped PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_tline_lumped PROPERTIES TIMEOUT 1200)

# T-line ngspice validation
# Note: ShortTermination test enabled (passes)
# MatchedTermination/OpenTermination tests disabled (require MNA branch variables)
add_executable(test_tline_ngspice validation/test_tline_ngspice.cpp)
target_link_libraries(test_tline_ngspice PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_tline_ngspice PROPERTIES TIMEOUT 1200 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# add_executable(test_devices unit/test_devices.cpp)
# target_link_libraries(test_devices PRIVATE kirimaspice_lib GTest::gtest_main)
# gtest_discover_tests(test_devices PROPERTIES TIMEOUT 1200)

#==============================================================================
# Integration Tests
#==============================================================================
# add_executable(test_dc_solver integration/test_dc_solver.cpp)
# target_link_libraries(test_dc_solver PRIVATE kirimaspice_lib GTest::gtest_main)
# gtest_discover_tests(test_dc_solver PROPERTIES TIMEOUT 1200)

# add_executable(test_transient_solver integration/test_transient_solver.cpp)
# target_link_libraries(test_transient_solver PRIVATE kirimaspice_lib GTest::gtest_main)
# gtest_discover_tests(test_transient_solver PROPERTIES TIMEOUT 1200)

#==============================================================================
# Validation Tests (executables, not unit tests)
#==============================================================================
add_executable(test_adaptive_rl validation/test_adaptive_rl.cpp)
target_link_libraries(test_adaptive_rl PRIVATE kirimaspice_lib)
target_include_directories(test_adaptive_rl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_dc_rl validation/test_dc_rl.cpp)
target_link_libraries(test_dc_rl PRIVATE kirimaspice_lib)
target_include_directories(test_dc_rl PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_rl_debug validation/test_rl_debug.cpp)
target_link_libraries(test_rl_debug PRIVATE kirimaspice_lib)
target_include_directories(test_rl_debug PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_adaptive_rl_debug validation/test_adaptive_rl_debug.cpp)
target_link_libraries(test_adaptive_rl_debug PRIVATE kirimaspice_lib)
target_include_directories(test_adaptive_rl_debug PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_mosfet_ngspice_comparison validation/test_mosfet_ngspice_comparison.cpp)
target_link_libraries(test_mosfet_ngspice_comparison PRIVATE kirimaspice_lib)
target_include_directories(test_mosfet_ngspice_comparison PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_bdf_methods validation/test_bdf_methods.cpp)
target_link_libraries(test_bdf_methods PRIVATE kirimaspice_lib)
target_include_directories(test_bdf_methods PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_bdf_adaptive validation/test_bdf_adaptive.cpp)
target_link_libraries(test_bdf_adaptive PRIVATE kirimaspice_lib)
target_include_directories(test_bdf_adaptive PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_bdf2_fixed validation/test_bdf2_fixed.cpp)
target_link_libraries(test_bdf2_fixed PRIVATE kirimaspice_lib)
target_include_directories(test_bdf2_fixed PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_tr_bdf2 validation/test_tr_bdf2.cpp)
target_link_libraries(test_tr_bdf2 PRIVATE kirimaspice_lib)
target_include_directories(test_tr_bdf2 PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_integration_methods_comparison validation/test_integration_methods_comparison.cpp)
target_link_libraries(test_integration_methods_comparison PRIVATE kirimaspice_lib)
target_include_directories(test_integration_methods_comparison PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)


add_executable(test_rlc_resonance validation/test_rlc_resonance.cpp)
target_link_libraries(test_rlc_resonance PRIVATE kirimaspice_lib)
target_include_directories(test_rlc_resonance PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_rlc_uic validation/test_rlc_uic.cpp)
target_link_libraries(test_rlc_uic PRIVATE kirimaspice_lib)
target_include_directories(test_rlc_uic PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_ngspice_params validation/test_ngspice_params.cpp)
target_link_libraries(test_ngspice_params PRIVATE kirimaspice_lib)
target_include_directories(test_ngspice_params PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_adaptive_tuned validation/test_adaptive_tuned.cpp)
target_link_libraries(test_adaptive_tuned PRIVATE kirimaspice_lib)
target_include_directories(test_adaptive_tuned PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_adaptive_physical validation/test_adaptive_physical.cpp)
target_link_libraries(test_adaptive_physical PRIVATE kirimaspice_lib)
target_include_directories(test_adaptive_physical PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_trapezoidal_adaptive validation/test_trapezoidal_adaptive.cpp)
target_link_libraries(test_trapezoidal_adaptive PRIVATE kirimaspice_lib)
target_include_directories(test_trapezoidal_adaptive PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_diode_rectifier_ngspice validation/test_diode_rectifier_ngspice.cpp)
target_link_libraries(test_diode_rectifier_ngspice PRIVATE kirimaspice_lib)
target_include_directories(test_diode_rectifier_ngspice PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_mutual_inductance_fix validation/test_mutual_inductance_fix.cpp)
target_link_libraries(test_mutual_inductance_fix PRIVATE kirimaspice_lib GTest::gtest_main)
add_test(NAME MutualInductanceFixTest COMMAND test_mutual_inductance_fix)

add_executable(test_sin_voltage_source validation/test_sin_voltage_source.cpp)
target_link_libraries(test_sin_voltage_source PRIVATE kirimaspice_lib)
target_include_directories(test_sin_voltage_source PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_sin_simple validation/test_sin_simple.cpp)
target_link_libraries(test_sin_simple PRIVATE kirimaspice_lib)
target_include_directories(test_sin_simple PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_bjt_amplifier_ngspice validation/test_bjt_amplifier_ngspice.cpp)
target_link_libraries(test_bjt_amplifier_ngspice PRIVATE kirimaspice_lib)
target_include_directories(test_bjt_amplifier_ngspice PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_bjt_simple_dc validation/test_bjt_simple_dc.cpp)
target_link_libraries(test_bjt_simple_dc PRIVATE kirimaspice_lib)
target_include_directories(test_bjt_simple_dc PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_bjt_current_debug validation/test_bjt_current_debug.cpp)
target_link_libraries(test_bjt_current_debug PRIVATE kirimaspice_lib)
target_include_directories(test_bjt_current_debug PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_bjt_nr_debug validation/test_bjt_nr_debug.cpp)
target_link_libraries(test_bjt_nr_debug PRIVATE kirimaspice_lib)
target_include_directories(test_bjt_nr_debug PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

add_executable(test_bjt_dc_validation validation/test_bjt_dc_validation.cpp)
target_link_libraries(test_bjt_dc_validation PRIVATE kirimaspice_lib GTest::gtest_main)
target_include_directories(test_bjt_dc_validation PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
gtest_discover_tests(test_bjt_dc_validation)

add_executable(test_klu_linear_simple validation/test_klu_linear_simple.cpp)
target_link_libraries(test_klu_linear_simple PRIVATE kirimaspice_lib)
target_include_directories(test_klu_linear_simple PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# AC analysis ngspice validation
add_executable(test_rc_lowpass_ac_ngspice validation/test_rc_lowpass_ac_ngspice.cpp)
target_link_libraries(test_rc_lowpass_ac_ngspice PRIVATE kirimaspice_lib)
target_include_directories(test_rc_lowpass_ac_ngspice PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/validation  # For ngspice_ac_utils.hpp
)

add_executable(test_diode_ac_ngspice validation/test_diode_ac_ngspice.cpp)
target_link_libraries(test_diode_ac_ngspice PRIVATE kirimaspice_lib)
target_include_directories(test_diode_ac_ngspice PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/validation  # For ngspice_ac_utils.hpp
)

add_executable(test_bjt_amplifier_ac_ngspice validation/test_bjt_amplifier_ac_ngspice.cpp)
target_link_libraries(test_bjt_amplifier_ac_ngspice PRIVATE kirimaspice_lib)
target_include_directories(test_bjt_amplifier_ac_ngspice PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/validation  # For ngspice_ac_utils.hpp
)

add_executable(test_mosfet_amplifier_ac_ngspice validation/test_mosfet_amplifier_ac_ngspice.cpp)
target_link_libraries(test_mosfet_amplifier_ac_ngspice PRIVATE kirimaspice_lib)
target_include_directories(test_mosfet_amplifier_ac_ngspice PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/validation  # For ngspice_ac_utils.hpp
)

# Phase Debugging: Minimal BJT phase investigation
add_executable(test_minimal_bjt_phase validation/test_minimal_bjt_phase.cpp)
target_link_libraries(test_minimal_bjt_phase PRIVATE kirimaspice_lib GTest::gtest_main)
target_include_directories(test_minimal_bjt_phase PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
gtest_discover_tests(test_minimal_bjt_phase PROPERTIES TIMEOUT 1200)

# Phase Debugging: BJT with base resistor
add_executable(test_bjt_with_rb_phase validation/test_bjt_with_rb_phase.cpp)
target_link_libraries(test_bjt_with_rb_phase PRIVATE kirimaspice_lib GTest::gtest_main)
target_include_directories(test_bjt_with_rb_phase PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
gtest_discover_tests(test_bjt_with_rb_phase PROPERTIES TIMEOUT 1200)

#==============================================================================
# Benchmarks (optional, not run by CTest)
#==============================================================================
if(BUILD_BENCHMARKS)
    # Large circuit benchmark
    add_executable(bench_large_circuit
        benchmarks/bench_large_circuit.cpp
    )

    target_link_libraries(bench_large_circuit
        PRIVATE
            kirimaspice_lib
            benchmark::benchmark
    )

    # Backend comparison benchmark (AC)
    add_executable(bench_solver_backends_ac
        benchmarks/bench_solver_backends_ac.cpp
    )
    target_link_libraries(bench_solver_backends_ac
        PRIVATE
            kirimaspice_lib
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # optimization benchmark (memory alignment + prefetch)
    add_executable(bench_sparse_refactorize
        benchmarks/bench_sparse_refactorize.cpp
    )
    target_link_libraries(bench_sparse_refactorize
        PRIVATE
            kirimaspice_lib
            benchmark::benchmark
    )

    # Sparse matrix parallel zero() operations benchmark
    add_executable(bench_sparse_matrix_zero
        benchmarks/bench_sparse_matrix_zero.cpp
    )
    target_link_libraries(bench_sparse_matrix_zero
        PRIVATE
            kirimaspice_lib
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # Parallel device stamping benchmark (DC and Transient)
    add_executable(bench_device_stamping
        benchmarks/bench_device_stamping.cpp
    )
    target_link_libraries(bench_device_stamping
        PRIVATE
            kirimaspice_lib
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # Detailed profiling benchmark (identify parallelization bottleneck)
    add_executable(bench_dc_thread_profile
        benchmarks/bench_dc_thread_profile.cpp
    )
    target_link_libraries(bench_dc_thread_profile
        PRIVATE
            kirimaspice_lib
            benchmark::benchmark
            benchmark::benchmark_main
    )

    # Simple CLI bench (no Google Benchmark) for stability measurements
    add_executable(simple_backend_bench
        ../tools/simple_backend_bench.cpp
    )
    target_link_libraries(simple_backend_bench PRIVATE kirimaspice_lib)

    # DC profiling tool (measure parallel overhead with detailed logs)
    add_executable(profile_dc_1000
        ../tools/profile_dc_1000.cpp
    )
    target_link_libraries(profile_dc_1000 PRIVATE kirimaspice_lib)

    # Benchmarks are not added to CTest (manual execution only)
endif()

# Diagnostic test (not added to CTest)
add_executable(bjt_ac_diagnostic
    validation/bjt_ac_diagnostic.cpp
)

target_link_libraries(bjt_ac_diagnostic
    PRIVATE
        kirimaspice_lib
)

# BJT parameter diagnostic (temporary)
add_executable(bjt_param_diagnostic
    validation/bjt_param_diagnostic.cpp
)

target_link_libraries(bjt_param_diagnostic
    PRIVATE
        kirimaspice_lib
)

# BJT AC matrix dump (temporary)
add_executable(bjt_matrix_dump
    validation/bjt_matrix_dump.cpp
)

target_link_libraries(bjt_matrix_dump
    PRIVATE
        kirimaspice_lib
)

# BJT complex voltage debug (temporary)
add_executable(bjt_complex_debug
    validation/bjt_complex_debug.cpp
)

target_link_libraries(bjt_complex_debug
    PRIVATE
        kirimaspice_lib
)

# Compare BJT matrices (with/without Rb)
add_executable(compare_bjt_matrices
    validation/compare_bjt_matrices.cpp
)

target_link_libraries(compare_bjt_matrices
    PRIVATE
        kirimaspice_lib
)

# BJT 2-stage DC debug (identify singular matrix column)
add_executable(debug_bjt_2stage_dc
    debug_bjt_2stage_dc.cpp
)

target_link_libraries(debug_bjt_2stage_dc
    PRIVATE
        kirimaspice_lib
)

# Darlington DC debug (identify singular matrix column)
add_executable(debug_darlington_dc
    debug_darlington_dc.cpp
)

target_link_libraries(debug_darlington_dc
    PRIVATE
        kirimaspice_lib
)

# BJT AC analysis debug (output actual complex values)
add_executable(debug_bjt_ac
    debug_bjt_ac.cpp
)

target_link_libraries(debug_bjt_ac
    PRIVATE
        kirimaspice_lib
)

# Trace BJT stamping step-by-step
add_executable(trace_bjt_stamping
    validation/trace_bjt_stamping.cpp
)

target_link_libraries(trace_bjt_stamping
    PRIVATE
        kirimaspice_lib
)

# Dump COO entries in stamping order
add_executable(dump_coo_entries
    validation/dump_coo_entries.cpp
)

target_link_libraries(dump_coo_entries
    PRIVATE
        kirimaspice_lib
)

# Dump COO entries WITH base resistor
add_executable(dump_coo_with_rb
    validation/dump_coo_with_rb.cpp
)

target_link_libraries(dump_coo_with_rb
    PRIVATE
        kirimaspice_lib
)

# Verify matrix assembly and conversion
add_executable(verify_matrix_assembly
    validation/verify_matrix_assembly.cpp
)

target_link_libraries(verify_matrix_assembly
    PRIVATE
        kirimaspice_lib
)

# Dump fully assembled matrix
add_executable(dump_assembled_matrix
    validation/dump_assembled_matrix.cpp
)

target_link_libraries(dump_assembled_matrix
    PRIVATE
        kirimaspice_lib
)

# Solve and dump complete solution
add_executable(solve_and_dump_solution
    validation/solve_and_dump_solution.cpp
)

target_link_libraries(solve_and_dump_solution
    PRIVATE
        kirimaspice_lib
)

# Check 0.75V DC operating point
add_executable(check_075v_dc
    validation/check_075v_dc.cpp
)

target_link_libraries(check_075v_dc
    PRIVATE
        kirimaspice_lib
)

# Debug CMOS inverter singular matrix issue
add_executable(debug_cmos_inverter
    debug/debug_cmos_inverter.cpp
)

target_link_libraries(debug_cmos_inverter
    PRIVATE
        kirimaspice_lib
)

#==============================================================================
# Integration Tests - Manufacturer Models
#==============================================================================
add_executable(test_manufacturer_models
    integration/test_manufacturer_models.cpp
)

target_link_libraries(test_manufacturer_models
    PRIVATE
        kirimaspice_lib
        GTest::gtest_main
)

gtest_discover_tests(test_manufacturer_models
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

#==============================================================================
# Integration Tests - BJT Simple Model Test
#==============================================================================
add_executable(test_bjt_simple_model
    integration/test_bjt_simple_model.cpp
)

target_link_libraries(test_bjt_simple_model
    PRIVATE
        kirimaspice_lib
        GTest::gtest_main
)

gtest_discover_tests(test_bjt_simple_model
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

#==============================================================================
# Integration Tests - .print/.save Directives
#==============================================================================
add_executable(test_print_save
    integration/test_print_save.cpp
)

target_link_libraries(test_print_save
    PRIVATE
        kirimaspice_lib
        GTest::gtest_main
)

gtest_discover_tests(test_print_save
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

#==============================================================================
# Integration Test - Parameter Sweep
#==============================================================================
add_executable(test_parameter_sweep_integration
    integration/test_parameter_sweep_integration.cpp
)

target_link_libraries(test_parameter_sweep_integration
    PRIVATE
        kirimaspice_lib
        GTest::gtest_main
)

gtest_discover_tests(test_parameter_sweep_integration
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

#==============================================================================
# Integration Test - Monte Carlo Analysis
#==============================================================================
add_executable(test_monte_carlo_analysis
    integration/test_monte_carlo_analysis.cpp
)

target_link_libraries(test_monte_carlo_analysis
    PRIVATE
        kirimaspice_lib
        GTest::gtest_main
)

gtest_discover_tests(test_monte_carlo_analysis
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

#==============================================================================
# Integration Test - Gummel-Poon Full Model
#==============================================================================
add_executable(test_gummel_poon_integration
    integration/test_gummel_poon_integration.cpp
)

target_link_libraries(test_gummel_poon_integration
    PRIVATE
        kirimaspice_lib
        GTest::gtest_main
)

gtest_discover_tests(test_gummel_poon_integration
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# CMOS Pixel Subcircuit Integration Test
add_executable(test_cmos_pixel_subcircuit
    integration/test_cmos_pixel_subcircuit.cpp
)

target_link_libraries(test_cmos_pixel_subcircuit
    PRIVATE
        kirimaspice_lib
        GTest::gtest_main
)

gtest_discover_tests(test_cmos_pixel_subcircuit
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

#==============================================================================
# Debug Tool - Emitter Resistor Matrix Singularity
#==============================================================================
add_executable(debug_emitter_resistor validation/debug_emitter_resistor.cpp)
target_link_libraries(debug_emitter_resistor PRIVATE kirimaspice_lib)
target_include_directories(debug_emitter_resistor PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Debug: T-line initialization
add_executable(debug_tline_init validation/debug_tline_init.cpp)
target_link_libraries(debug_tline_init PRIVATE kirimaspice_lib)

#==============================================================================
# S-parameter Analysis Tests
#==============================================================================
add_executable(test_sparam unit/test_sparam.cpp)
target_link_libraries(test_sparam PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_sparam PROPERTIES TIMEOUT 1200)

#==============================================================================
# Worst-Case Analysis Tests
#==============================================================================
add_executable(test_wcase unit/test_wcase.cpp)
target_link_libraries(test_wcase PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_wcase PROPERTIES TIMEOUT 1200)

#==============================================================================
# Circuit Optimization Tests
#==============================================================================
add_executable(test_optimizer unit/test_optimizer.cpp)
target_link_libraries(test_optimizer PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_optimizer PROPERTIES TIMEOUT 1200)

# BSIM4 parameter extraction
add_executable(test_bsim4_parameter_extractor unit/test_bsim4_parameter_extractor.cpp)
target_link_libraries(test_bsim4_parameter_extractor PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bsim4_parameter_extractor PROPERTIES TIMEOUT 1200)

# BSIM4 comprehensive testing
add_executable(test_bsim4_comprehensive unit/test_bsim4_comprehensive.cpp)
target_link_libraries(test_bsim4_comprehensive PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bsim4_comprehensive PROPERTIES TIMEOUT 1200)

# BSIM4 temperature dependence validation
add_executable(test_bsim4_temperature_validation unit/test_bsim4_temperature_validation.cpp)
target_link_libraries(test_bsim4_temperature_validation PRIVATE kirimaspice_lib GTest::gtest_main nlohmann_json::nlohmann_json)
gtest_discover_tests(test_bsim4_temperature_validation
    PROPERTIES TIMEOUT 1200
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# BSIM4 noise RC sanity test
add_executable(test_noise_rc unit/test_noise_rc.cpp)
target_link_libraries(test_noise_rc PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_noise_rc PROPERTIES TIMEOUT 1200)

# Units parser sanity tests (basic suffix parsing)
add_executable(test_units_parse unit/test_units_parse.cpp)
target_link_libraries(test_units_parse PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_units_parse PROPERTIES TIMEOUT 1200)

add_executable(test_stamps_keep_history unit/test_stamps_keep_history.cpp)
target_link_libraries(test_stamps_keep_history PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_stamps_keep_history PROPERTIES TIMEOUT 1200)

add_executable(test_dc_convergence_kcl unit/test_dc_convergence_kcl.cpp)
target_link_libraries(test_dc_convergence_kcl PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_dc_convergence_kcl PROPERTIES TIMEOUT 1200)

add_executable(test_netlist_lines unit/test_netlist_lines.cpp)
target_link_libraries(test_netlist_lines PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_netlist_lines PROPERTIES TIMEOUT 1200)

add_executable(test_subcircuit_instances unit/test_subcircuit_instances.cpp)
target_link_libraries(test_subcircuit_instances PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_subcircuit_instances PROPERTIES TIMEOUT 1200)

add_executable(test_bsim4_fingers unit/test_bsim4_fingers.cpp)
target_link_libraries(test_bsim4_fingers PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bsim4_fingers PROPERTIES TIMEOUT 1200)

add_executable(test_bsim4_junctions unit/test_bsim4_junctions.cpp)
target_link_libraries(test_bsim4_junctions PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bsim4_junctions PROPERTIES TIMEOUT 1200)

add_executable(test_bjt_card unit/test_bjt_card.cpp)
target_link_libraries(test_bjt_card PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bjt_card PROPERTIES TIMEOUT 1200)

add_executable(test_bjt_gummel_poon unit/test_bjt_gummel_poon.cpp)
target_link_libraries(test_bjt_gummel_poon PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bjt_gummel_poon PROPERTIES TIMEOUT 1200)

add_executable(test_analysis_requests unit/test_analysis_requests.cpp)
target_link_libraries(test_analysis_requests PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_analysis_requests PROPERTIES TIMEOUT 1200)

add_executable(test_dc_solver_logging unit/test_dc_solver_logging.cpp)
target_link_libraries(test_dc_solver_logging PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_dc_solver_logging PROPERTIES TIMEOUT 1200)

add_executable(test_source_waveforms unit/test_source_waveforms.cpp)
target_link_libraries(test_source_waveforms PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_source_waveforms PROPERTIES TIMEOUT 1200)

add_executable(test_small_signal_consistency unit/test_small_signal_consistency.cpp)
target_link_libraries(test_small_signal_consistency PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_small_signal_consistency PROPERTIES TIMEOUT 1200)

add_executable(test_behavioral_source unit/test_behavioral_source.cpp)
target_link_libraries(test_behavioral_source PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_behavioral_source PROPERTIES TIMEOUT 1200)

# BSIM4 ngspice comparison tests
add_executable(test_bsim4_ngspice_compare unit/test_bsim4_ngspice_compare.cpp)
target_link_libraries(test_bsim4_ngspice_compare PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bsim4_ngspice_compare PROPERTIES TIMEOUT 1200)

# BSIM4 drain current and conductances against reference runs of ngspice-47
add_executable(test_bsim4_reference unit/test_bsim4_reference.cpp)
target_link_libraries(test_bsim4_reference PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bsim4_reference)

# P-channel devices stamp the mirror of the N-channel ones
add_executable(test_p_channel_stamps unit/test_p_channel_stamps.cpp)
target_link_libraries(test_p_channel_stamps PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_p_channel_stamps)

# Gate capacitances of the three-terminal FETs in a transient
add_executable(test_gate_capacitances unit/test_gate_capacitances.cpp)
target_link_libraries(test_gate_capacitances PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_gate_capacitances)

# Temperatures of a netlist (TNOM) are in degrees Celsius
add_executable(test_netlist_temperatures unit/test_netlist_temperatures.cpp)
target_link_libraries(test_netlist_temperatures PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_netlist_temperatures)

# The level 3 MOSFET against its published equations and ngspice-47
add_executable(test_mosfet_level3_reference unit/test_mosfet_level3_reference.cpp)
target_link_libraries(test_mosfet_level3_reference PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_mosfet_level3_reference)

# BSIM4 high VDS Vth tests
add_executable(test_bsim4_high_vds_vth unit/test_bsim4_high_vds_vth.cpp)
target_link_libraries(test_bsim4_high_vds_vth PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bsim4_high_vds_vth PROPERTIES TIMEOUT 1200)

# BSIM4 transient analysis tests (ngspice comparison)
add_executable(test_bsim4_transient unit/test_bsim4_transient.cpp)
target_link_libraries(test_bsim4_transient PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_bsim4_transient
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

#==============================================================================
# MESFET Device Tests
#==============================================================================
add_executable(test_mesfet unit/test_mesfet.cpp)
target_link_libraries(test_mesfet PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_mesfet PROPERTIES TIMEOUT 1200)

#==============================================================================
# TRIAC Device Tests
#==============================================================================
add_executable(test_triac unit/test_triac.cpp)
target_link_libraries(test_triac PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_triac PROPERTIES TIMEOUT 1200)

#==============================================================================
# SPICE3 Raw File Writer Tests
#==============================================================================
add_executable(test_raw_writer unit/test_raw_writer.cpp)
target_link_libraries(test_raw_writer PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_raw_writer PROPERTIES TIMEOUT 1200)

#==============================================================================
# Large Scale Circuit Tests (100-3000 devices)
#==============================================================================
# 100 device tests
add_executable(test_large_scale_100 unit/test_large_scale_100.cpp)
target_link_libraries(test_large_scale_100 PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_large_scale_100 PROPERTIES TIMEOUT 300)  # 5 minutes

# 300 device tests
add_executable(test_large_scale_300 unit/test_large_scale_300.cpp)
target_link_libraries(test_large_scale_300 PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_large_scale_300 PROPERTIES TIMEOUT 600)  # 10 minutes

# 1000 device tests
add_executable(test_large_scale_1000 unit/test_large_scale_1000.cpp)
target_link_libraries(test_large_scale_1000 PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_large_scale_1000 PROPERTIES TIMEOUT 1200)  # 20 minutes

# 3000 device tests
add_executable(test_large_scale_3000 unit/test_large_scale_3000.cpp)
target_link_libraries(test_large_scale_3000 PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_large_scale_3000 PROPERTIES TIMEOUT 1800)  # 30 minutes

#==============================================================================
# ngspice Validation Tests for Large-Scale Circuits
#==============================================================================
add_executable(test_large_scale_validation unit/test_large_scale_validation.cpp)
target_link_libraries(test_large_scale_validation PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_large_scale_validation
    PROPERTIES
        TIMEOUT 1200
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

#==============================================================================
# Regression Tests (2025-11-29)
# Verifies BSIM4 temperature recalculation and noise adjoint transfer function
#==============================================================================
add_executable(test_regression_2025_11_29 unit/test_regression_2025_11_29.cpp)
target_link_libraries(test_regression_2025_11_29 PRIVATE kirimaspice_lib GTest::gtest_main)
gtest_discover_tests(test_regression_2025_11_29 PROPERTIES TIMEOUT 120)
