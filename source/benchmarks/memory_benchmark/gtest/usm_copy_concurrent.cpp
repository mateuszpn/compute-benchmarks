/*
 * Copyright (C) 2023-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "definitions/usm_copy_concurrent.h"

#include "framework/test_case/register_test_case.h"
#include "framework/utility/common_gtest_args.h"
#include "framework/utility/memory_constants.h"
[[maybe_unused]] static const inline RegisterTestCase<UsmConcurrentCopy> registerTestCase{};

#include <gtest/gtest.h>

class UsmConcurrentCopyTest : public ::testing::TestWithParam<std::tuple<Api, size_t, Engine, Engine, bool>> {
};

TEST_P(UsmConcurrentCopyTest, Test) {
    UsmConcurrentCopyArguments args;
    args.api = Api::L0;
    args.size = std::get<1>(GetParam());
    args.h2dEngine = std::get<2>(GetParam());
    args.d2hEngine = std::get<3>(GetParam());
    args.withCopyOffload = std::get<4>(GetParam());

    UsmConcurrentCopy test;
    test.run(args);
}
using namespace MemoryConstants;
INSTANTIATE_TEST_SUITE_P(
    UsmConcurrentCopyTest,
    UsmConcurrentCopyTest,
    ::testing::Combine(
        ::testing::Values(Api::L0),
        ::testing::Values(256 * megaByte),
        ::testing::Values(Engine::Ccs0, Engine::Bcs),
        ::testing::Values(Engine::Ccs0, Engine::Bcs),
        ::testing::Values(true, false)));
