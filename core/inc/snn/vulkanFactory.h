/* Copyright (C) 2020 - 2022 OPPO. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "snn/snn.h"

// Include appropriate vulkan headers before this file
// For compilation units using uvkc:
// #include "uvkc/vulkan/dynamic_symbols.h"
// For compilation units using volk:
// #include "volk.h"
// Or include any other headers that contain Vulkan API

namespace snn {

// This function creates Vulkan context to be used by ShaderNN
// from already initialized Vulkan variables
// It supposed to be used to integrate ShaderNN into application's pipeline
GpuContext* createVulkanContext(VkInstance instance_,
                                VkPhysicalDevice physicalDevice_,
                                VkPhysicalDeviceMemoryProperties deviceMemoryProperties_,
                                VkDevice device_,
                                VkCommandPool commandPool_,
                                VkQueue queue_,
                                uint32_t queueIndex_);
}
