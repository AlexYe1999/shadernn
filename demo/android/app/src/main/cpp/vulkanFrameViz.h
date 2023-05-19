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
#include "volk.h"
#include "framevizImpl.h"
#include "vulkanContext.h"

namespace snn
{

// Empty implementation here
// Vulkan rendering logic is handled by VulkanApp
class VulkanNightVisionFrameViz : public NightVisionFrameVizImpl
{
public:
    VulkanNightVisionFrameViz();

    ~VulkanNightVisionFrameViz();

    void resize(uint32_t /*w*/, uint32_t /*h*/) override {}

    virtual void render(const NightVisionFrameViz::RenderParameters& rp) override;

    virtual void setWindow(intptr_t /*window*/) override {}

    virtual void reset() override {
        samplerUpdated = false;
    }

private:
    VulkanGpuContext* context = nullptr;

    VkSampler textureSampler = VK_NULL_HANDLE;

    bool samplerUpdated = false;
};

class VulkanNightVisionFrameRec : public VulkanNightVisionFrameViz {
protected:
public:
    // TODO
    void render(const NightVisionFrameViz::RenderParameters & /*rp*/) override {}

    // TODO
    void setWindow(intptr_t /*window*/) override {}
};

}
