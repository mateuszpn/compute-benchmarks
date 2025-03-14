/*
 * Copyright (C) 2022-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "framework/l0/levelzero.h"
#include "framework/test_case/register_test_case.h"
#include "framework/utility/file_helper.h"
#include "framework/utility/memory_constants.h"
#include "framework/utility/timer.h"

#include "definitions/stream_memory_immediate.h"

#include <gtest/gtest.h>

using namespace MemoryConstants;

static TestResult run(const StreamMemoryImmediateArguments &arguments, Statistics &statistics) {
    MeasurementFields typeSelector(MeasurementUnit::GigabytesPerSecond, arguments.useEvents ? MeasurementType::Gpu : MeasurementType::Cpu);

    if (isNoopRun()) {
        statistics.pushUnitAndType(typeSelector.getUnit(), typeSelector.getType());
        return TestResult::Nooped;
    }

    // Setup
    QueueProperties queueProperties = QueueProperties::create();
    ContextProperties contextProperties = ContextProperties::create();
    ExtensionProperties extensionProperties = ExtensionProperties::create();

    LevelZero levelzero(queueProperties, contextProperties, extensionProperties);
    if (levelzero.commandQueue == nullptr) {
        return TestResult::DeviceNotCapable;
    }
    Timer timer;

    // Query double support
    ze_device_module_properties_t moduleProperties{};
    ASSERT_ZE_RESULT_SUCCESS(zeDeviceGetModuleProperties(levelzero.device, &moduleProperties));

    const bool useDoubles = moduleProperties.fp64flags != 0u;
    const size_t elementSize = useDoubles ? sizeof(double) : sizeof(float);
    const size_t fillValue = 313u;
    const int32_t scalarValue = -999;
    const uint32_t gws = static_cast<uint32_t>(arguments.size / elementSize);
    const uint64_t timerResolution = levelzero.getTimerResolution(levelzero.device);

    // Create module
    const char *kernelFile = useDoubles ? "memory_benchmark_stream_memory_fp64.spv" : "memory_benchmark_stream_memory.spv";
    auto spirvModule = FileHelper::loadBinaryFile(kernelFile);
    if (spirvModule.size() == 0) {
        return TestResult::KernelNotFound;
    }
    ze_module_handle_t module;
    ze_kernel_handle_t kernel;
    ze_module_desc_t moduleDesc{ZE_STRUCTURE_TYPE_MODULE_DESC};
    moduleDesc.format = ZE_MODULE_FORMAT_IL_SPIRV;
    moduleDesc.pInputModule = reinterpret_cast<const uint8_t *>(spirvModule.data());
    moduleDesc.inputSize = spirvModule.size();
    ASSERT_ZE_RESULT_SUCCESS(zeModuleCreate(levelzero.context, levelzero.device, &moduleDesc, &module, nullptr));

    // Create buffers
    size_t bufferSize = arguments.size;
    void *buffers[3] = {};
    size_t buffersCount = {};
    size_t bufferSizes[3] = {bufferSize, bufferSize, bufferSize};

    const ze_device_mem_alloc_desc_t deviceAllocationDesc{ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC};
    ze_kernel_desc_t kernelDesc{ZE_STRUCTURE_TYPE_KERNEL_DESC};
    switch (arguments.type) {
    case StreamMemoryType::Read:
        kernelDesc.pKernelName = "read";
        ASSERT_ZE_RESULT_SUCCESS(zeMemAllocDevice(levelzero.context, &deviceAllocationDesc, bufferSize, 0, levelzero.device, &buffers[buffersCount++]));
        bufferSizes[buffersCount] = 16u;
        ASSERT_ZE_RESULT_SUCCESS(zeMemAllocDevice(levelzero.context, &deviceAllocationDesc, 16u, 0, levelzero.device, &buffers[buffersCount++]));
        break;
    case StreamMemoryType::Write:
        kernelDesc.pKernelName = "write";
        ASSERT_ZE_RESULT_SUCCESS(zeMemAllocDevice(levelzero.context, &deviceAllocationDesc, bufferSize, 0, levelzero.device, &buffers[buffersCount++]));
        break;
    case StreamMemoryType::Scale:
        kernelDesc.pKernelName = "scale";
        ASSERT_ZE_RESULT_SUCCESS(zeMemAllocDevice(levelzero.context, &deviceAllocationDesc, bufferSize, 0, levelzero.device, &buffers[buffersCount++]));
        ASSERT_ZE_RESULT_SUCCESS(zeMemAllocDevice(levelzero.context, &deviceAllocationDesc, bufferSize, 0, levelzero.device, &buffers[buffersCount++]));
        break;
    case StreamMemoryType::Triad:
        kernelDesc.pKernelName = "triad";
        ASSERT_ZE_RESULT_SUCCESS(zeMemAllocDevice(levelzero.context, &deviceAllocationDesc, bufferSize, 0, levelzero.device, &buffers[buffersCount++]));
        ASSERT_ZE_RESULT_SUCCESS(zeMemAllocDevice(levelzero.context, &deviceAllocationDesc, bufferSize, 0, levelzero.device, &buffers[buffersCount++]));
        ASSERT_ZE_RESULT_SUCCESS(zeMemAllocDevice(levelzero.context, &deviceAllocationDesc, bufferSize, 0, levelzero.device, &buffers[buffersCount++]));
        break;
    default:
        FATAL_ERROR("Unknown StreamMemoryType");
    }

    // Create kernel
    ASSERT_ZE_RESULT_SUCCESS(zeKernelCreate(module, &kernelDesc, &kernel));

    // Query maximum group size
    const uint32_t groupSizeX = std::min(levelzero.getDeviceComputeProperties().maxGroupSizeX, gws);

    // Configure kernel group size
    ASSERT_ZE_RESULT_SUCCESS(zeKernelSetGroupSize(kernel, groupSizeX, 1u, 1u));
    const ze_group_count_t dispatchTraits{gws / groupSizeX, 1u, 1u};

    // Create an immediate command list
    ze_command_list_handle_t cmdList;
    ze_command_queue_desc_t commandQueueDesc = levelzero.commandQueueDesc;
    commandQueueDesc.mode = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS;
    ASSERT_ZE_RESULT_SUCCESS(zeCommandListCreateImmediate(levelzero.context, levelzero.device, &commandQueueDesc, &cmdList));

    // Create event
    ze_event_pool_flags_t eventPoolFlags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE;
    if (arguments.useEvents) {
        eventPoolFlags |= ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP;
    }
    ze_event_pool_handle_t eventPool{};
    ze_event_handle_t event{};
    ze_event_pool_desc_t eventPoolDesc{ZE_STRUCTURE_TYPE_EVENT_POOL_DESC};
    eventPoolDesc.flags = eventPoolFlags;
    eventPoolDesc.count = 1;
    ASSERT_ZE_RESULT_SUCCESS(zeEventPoolCreate(levelzero.context, &eventPoolDesc, 1, &levelzero.commandQueueDevice, &eventPool));
    ze_event_desc_t eventDesc{ZE_STRUCTURE_TYPE_EVENT_DESC};
    eventDesc.index = 0;
    eventDesc.signal = ZE_EVENT_SCOPE_FLAG_HOST;
    eventDesc.wait = ZE_EVENT_SCOPE_FLAG_HOST;
    ASSERT_ZE_RESULT_SUCCESS(zeEventCreate(eventPool, &eventDesc, &event));

    // Enqueue filling of the buffers and set kernel arguments
    for (auto i = 0u; i < buffersCount; i++) {
        ASSERT_ZE_RESULT_SUCCESS(zeCommandListAppendMemoryFill(cmdList, buffers[i], &fillValue, sizeof(fillValue), bufferSizes[i], event, 0, nullptr));
        ASSERT_ZE_RESULT_SUCCESS(zeKernelSetArgumentValue(kernel, static_cast<int>(i), sizeof(buffers[i]), &buffers[i]));
        ASSERT_ZE_RESULT_SUCCESS(zeEventHostSynchronize(event, std::numeric_limits<uint64_t>::max()));
        ASSERT_ZE_RESULT_SUCCESS(zeEventHostReset(event));
    }
    ASSERT_ZE_RESULT_SUCCESS(zeKernelSetArgumentValue(kernel, static_cast<uint32_t>(buffersCount), sizeof(scalarValue), &scalarValue));

    // Warmup
    ASSERT_ZE_RESULT_SUCCESS(zeCommandListAppendLaunchKernel(cmdList, kernel, &dispatchTraits, event, 0, nullptr));
    ASSERT_ZE_RESULT_SUCCESS(zeEventHostSynchronize(event, std::numeric_limits<uint64_t>::max()));
    ASSERT_ZE_RESULT_SUCCESS(zeEventHostReset(event));

    // Benchmark
    for (auto i = 0u; i < arguments.iterations; i++) {
        // Launch kernel
        timer.measureStart();
        ASSERT_ZE_RESULT_SUCCESS(zeCommandListAppendLaunchKernel(cmdList, kernel, &dispatchTraits, event, 0, nullptr));
        ASSERT_ZE_RESULT_SUCCESS(zeEventHostSynchronize(event, std::numeric_limits<uint64_t>::max()));
        timer.measureEnd();

        size_t transferSize = arguments.size;
        switch (arguments.type) {
        case StreamMemoryType::Scale:
            transferSize *= 2;
            break;
        case StreamMemoryType::Triad:
            transferSize *= 3;
            break;
        default:
            break;
        }

        if (arguments.useEvents) {
            ze_kernel_timestamp_result_t timestampResult{};
            ASSERT_ZE_RESULT_SUCCESS(zeEventQueryKernelTimestamp(event, &timestampResult));
            auto commandTime = std::chrono::nanoseconds(timestampResult.global.kernelEnd - timestampResult.global.kernelStart);
            commandTime *= timerResolution;
            statistics.pushValue(commandTime, transferSize, typeSelector.getUnit(), typeSelector.getType());
        } else {
            statistics.pushValue(timer.get(), transferSize, typeSelector.getUnit(), typeSelector.getType());
        }
        ASSERT_ZE_RESULT_SUCCESS(zeEventHostReset(event));
    }

    // Cleanup
    ASSERT_ZE_RESULT_SUCCESS(zeEventDestroy(event));
    ASSERT_ZE_RESULT_SUCCESS(zeEventPoolDestroy(eventPool));
    for (size_t i = 0; i < buffersCount; i++) {
        ASSERT_ZE_RESULT_SUCCESS(zeMemFree(levelzero.context, buffers[i]));
    }
    ASSERT_ZE_RESULT_SUCCESS(zeCommandListDestroy(cmdList));
    ASSERT_ZE_RESULT_SUCCESS(zeKernelDestroy(kernel));
    ASSERT_ZE_RESULT_SUCCESS(zeModuleDestroy(module));

    return TestResult::Success;
}

static RegisterTestCaseImplementation<StreamMemoryImmediate> registerTestCase(run, Api::L0);
