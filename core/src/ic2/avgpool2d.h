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

#include "genericlayer.h"
#include "snn/snn.h"
#include "snn/inferencegraph.h"
#include "modelparser.h"
#include <string>
#include <utility>

namespace snn {
namespace dp { // short for Dynamic Pipeline

struct AveragePooling2DDesc : GenericConvDesc {
    std::string padding, paddingValue, paddingT, paddingB, paddingL, paddingR;
    void parse(ModelParser& parser, int layerId) {
        GenericConvDesc::parse(parser, layerId);
        parser.getAvgPoolLayer(layerId, (int&) numOutputPlanes, (int&) numInputPlanes, (int&) kernelSize, (int&) stride, padding);
    }
};

// This is a base class to generates a shader for average pooling function
class AveragePooling2DLayer : public GenericConvolutionLayer {
public:
    AveragePooling2DLayer(AveragePooling2DDesc&& d): GenericConvolutionLayer(d), _desc(std::move(d)) {}

    virtual ~AveragePooling2DLayer() = default;

    InferenceGraph::Transform getOutputScaleDimAdjustment() const override;

protected:
    AveragePooling2DDesc _desc;

    static void getPaddingOffsetOrig(uint32_t (&offsets)[4], const std::string& paddingT, const std::string& paddingB, const std::string& paddingL,
        const std::string& paddingR, int kernelSize);
};

}; // namespace dp
} // namespace snn
