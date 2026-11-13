#include <gtest/gtest.h>
#include "kirimaspice/solvers/parameter_sweep.hpp"
#include <fstream>
#include <cstdio>

// Regression/perf guard: ensure parse_string cache path remains functional.

using namespace kirimaspice;

TEST(ParameterSweepPerf, NoStepFallsBackSingleRun) {
    const char* net = R"(
* simple dc run
V1 in 0 DC 1
R1 in 0 1k
.op
.end
)";
    const std::string path = "tmp_param_perf.cir";
    {
        std::ofstream f(path);
        f << net;
    }

    parser::NetlistParser parser;
    auto parse_result = parser.parse_file(path);
    solvers::ParameterSweepExecutor exec(parse_result.config);
    auto result = exec.execute_dc_sweep(path);

    EXPECT_EQ(result.overall_status, ConvergenceStatus::SUCCESS);
    EXPECT_EQ(result.num_success, 1);
    EXPECT_EQ(result.num_failed, 0);

    std::remove(path.c_str());
}
