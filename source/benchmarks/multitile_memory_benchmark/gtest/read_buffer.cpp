/*
 * Copyright (C) 2022-2025 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "definitions/read_buffer.h"

#include "framework/test_case/register_test_case.h"
#include "framework/utility/common_gtest_args.h"
#include "framework/utility/memory_constants.h"

#include <gtest/gtest.h>

[[maybe_unused]] static const inline RegisterTestCase<ReadBuffer> registerTestCase{};

class ReadBufferTest : public ::testing::TestWithParam<std::tuple<DeviceSelection, DeviceSelection, DeviceSelection, size_t, bool, bool>> {};

TEST_P(ReadBufferTest, Test) {
    ReadBufferArguments args;
    args.api = Api::OpenCL;
    args.contextPlacement = std::get<0>(GetParam());
    args.queuePlacement = std::get<1>(GetParam());
    args.bufferPlacement = std::get<2>(GetParam());
    args.size = std::get<3>(GetParam());
    args.compressed = std::get<4>(GetParam());
    args.useEvents = std::get<5>(GetParam());

    if (!args.validateArgumentsExtra()) {
        GTEST_SKIP(); // If above arguments make no sense (e.g. queue created outside of the context), skip the case
    }

    ReadBuffer test;
    test.run(args);
}

using namespace MemoryConstants;
INSTANTIATE_TEST_SUITE_P(
    ReadBufferTest,
    ReadBufferTest,
    ::testing::Combine(
        ::CommonGtestArgs::contextDeviceSelections(),
        ::CommonGtestArgs::resourceDeviceSelections(),
        ::CommonGtestArgs::resourceDeviceSelections(),
        ::testing::Values(128 * megaByte, 512 * megaByte),
        ::testing::Values(false, true),
        ::testing::Values(true)));

INSTANTIATE_TEST_SUITE_P(
    ReadBufferTestLIMITED,
    ReadBufferTest,
    ::testing::Combine(
        ::testing::Values(DeviceSelection::Root),
        ::testing::Values(DeviceSelection::Root),
        ::testing::Values(DeviceSelection::Host),
        ::testing::Values(512 * megaByte),
        ::testing::Values(false),
        ::testing::Values(true)));
