/* Copyright (c) 2018-2022, Arm Limited and Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
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

#include <cassert>
#include <stdexcept>
#include <string>

#include "strings.h"
#include "snn/utils.h"

#if defined(__clang__)
// CLANG ENABLE/DISABLE WARNING DEFINITION
#    define VKBP_DISABLE_WARNINGS()                             \
        _Pragma("clang diagnostic push")                        \
            _Pragma("clang diagnostic ignored \"-Wall\"")       \
                _Pragma("clang diagnostic ignored \"-Wextra\"") \
                    _Pragma("clang diagnostic ignored \"-Wtautological-compare\"")

#    define VKBP_ENABLE_WARNINGS() \
        _Pragma("clang diagnostic pop")
#elif defined(__GNUC__) || defined(__GNUG__)
// GCC ENABLE/DISABLE WARNING DEFINITION
#    define VKBP_DISABLE_WARNINGS()                             \
        _Pragma("GCC diagnostic push")                          \
            _Pragma("GCC diagnostic ignored \"-Wall\"")         \
                _Pragma("clang diagnostic ignored \"-Wextra\"") \
                    _Pragma("clang diagnostic ignored \"-Wtautological-compare\"")

#    define VKBP_ENABLE_WARNINGS() \
        _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
// MSVC ENABLE/DISABLE WARNING DEFINITION
#    define VKBP_DISABLE_WARNINGS() \
        __pragma(warning(push, 0))

#    define VKBP_ENABLE_WARNINGS() \
        __pragma(warning(pop))
#endif

namespace vkb
{
/**
 * @brief Vulkan exception structure
 */
class VulkanException : public std::runtime_error
{
  public:
    /**
     * @brief Vulkan exception constructor
     */
    VulkanException(VkResult result, const std::string &msg = "Vulkan error");

    /**
     * @brief Returns the Vulkan error code as string
     * @return String message of exception
     */
    const char *what() const noexcept override;

    VkResult result;

  private:
    std::string error_message;
};
}        // namespace vkb

/// @brief Helper macro to test the result of Vulkan calls which can return an error.
#define VK_CHECK(x)                                                             \
    do                                                                          \
    {                                                                           \
        VkResult err = x;                                                       \
        if (err) {                                                              \
            SNN_LOGE("Detected Vulkan error: %s", vkb::to_string(err).c_str()); \
            abort();                                                            \
        }                                                                       \
    } while (0)

#define ASSERT_VK_HANDLE(handle)         \
    do                                   \
    {                                    \
        if ((handle) == VK_NULL_HANDLE) {\
            SNN_LOGE("Handle is NULL");  \
            abort();                     \
        }                                \
    } while (0)
