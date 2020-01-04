/* Copyright (C) 2020 - Present, OPPO Mobile Comm Corp., Ltd. All rights reserved.
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
#include "pch.h"
#include "dp.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <ios>

#define OLD_SHADER      0
#define CLAMPED_PADDING 1

using namespace snn;
using namespace snn::dp;

static constexpr const char* CONV2D_FS_ASSET_NAME     = "shaders/shadertemplate_fs_conv2d_RGBA.glsl";
static constexpr const char* CONV2D_CS_ASSET_NAME     = "shaders/3rdparty/shadertemplate_cs_conv2d.glsl";
static constexpr const char* CONV2D_1X1_CS_ASSET_NAME = "shaders/3rdparty/shadertemplate_cs_conv2d_1x1.glsl";

Conv2DLayer::Conv2DLayer(Conv2DDesc&& d): GenericConvolutionLayer(d), _desc(std::move(d)) {
    if (_desc.numInputPlanes <= 64) {
        _desc.useUniformShaders = false;
        _desc.weightMode        = snn::WeightAccessMethod::CONSTANTS;
    }

    if (_desc.weightMode == snn::WeightAccessMethod::CONSTANTS) {
        _desc.useUniformShaders = false;
    }

    switch (_desc.weightMode) {
    case snn::WeightAccessMethod::TEXTURES:
        this->weightTextures.allocate(_desc.numOutputPlanes);
        break;

    case snn::WeightAccessMethod::UNIFORM_BUFFER:
        this->weightUniformBuffers.allocate(_desc.numOutputPlanes);
        break;

    case snn::WeightAccessMethod::SSBO_BUFFER:
        this->weightSSBOBuffers.allocate(_desc.numOutputPlanes);
        break;

    default:
        break;
    }

    snn::ColorFormat weightFormat;
    if (_desc.preferrHalfPrecision) {
        weightFormat = snn::ColorFormat::RGBA16F;
    } else {
        weightFormat = snn::ColorFormat::RGBA32F;
    }

    this->biases = _desc.biases;

    for (std::size_t i = 0; i < _desc.numOutputPlanes; i++) {
        std::size_t depth = std::max(DIV_4_ROUND_UP(_desc.numInputPlanes), (uint32_t) 1);
        switch (_desc.weightMode) {
        case snn::WeightAccessMethod::TEXTURES: {
            if (depth == 1) {
                this->weightTextures[i].allocate2D(weightFormat, _desc.kernelSize, _desc.kernelSize, 1);
            } else {
                this->weightTextures[i].allocate2DArray(weightFormat, _desc.kernelSize, _desc.kernelSize, depth, 1);
            }
            break;
        }

        case snn::WeightAccessMethod::UNIFORM_BUFFER: {
            uint32_t count = 4 * _desc.kernelSize * _desc.kernelSize * _desc.numInputPlanes;
            if (_desc.preferrHalfPrecision) {
                std::vector<uint16_t> dummyVal(count, 0);
                this->weightUniformBuffers[i].allocate(count, dummyVal.data());
            } else {
                std::vector<float> dummyVal(count, 0.0f);
                this->weightUniformBuffers[i].allocate(count, dummyVal.data());
            }
            break;
        }

        case snn::WeightAccessMethod::SSBO_BUFFER: {
            uint32_t count = 4 * _desc.kernelSize * _desc.kernelSize * _desc.numInputPlanes;
            if (_desc.preferrHalfPrecision) {
                std::vector<uint16_t> dummyVal(count, 0);
                this->weightSSBOBuffers[i].allocate(count, dummyVal.data());
            } else {
                std::vector<float> dummyVal(count, 0.0f);
                this->weightSSBOBuffers[i].allocate(count, dummyVal.data());
            }
            break;
        }

        default:
            break;
        }
        // SNN_LOGD("Created weight Texture: %u, %u", this->weights[i].id(), this->weights[i].target());
        // std::cout << this->weights[i].id() << ", " << this->weights[i].target() << std::endl;
    }
    SNN_LOGD("Kernel texture: %d, %d, %d\n", weightFormat, DIV_4_ROUND_UP(_desc.numInputPlanes) * 4, DIV_4_ROUND_UP(_desc.numOutputPlanes));
    this->kernelTexture.allocate2DArray(weightFormat, DIV_4_ROUND_UP(_desc.numInputPlanes) * 4, DIV_4_ROUND_UP(_desc.numOutputPlanes),
                                        _desc.kernelSize * _desc.kernelSize, _desc.kernelSize * _desc.kernelSize * 4, 1);

    SNN_LOGD("Size of biases for current layer with input channels %u and output channels %u is %u", _desc.numInputPlanes, _desc.numOutputPlanes,
             this->biases.size());
    if (this->biases.size() % 4 != 0) {
        auto initSize  = this->biases.size();
        auto finalSize = 4 * ((uint32_t)(this->biases.size() / 4) + 1);
        for (std::size_t i = initSize; i < finalSize; i++) {
            this->biases.push_back(0.0);
        }
    }
    this->isRange01 = _desc.isRange01;
    //    if (_desc.preferrHalfPrecision) {
    //        snn::convertToMediumPrecision(this->biases);
    //    }
}

void Conv2DLayer::setTextureWeights() const {
    SNN_LOGD("Updating weights for: %s", this->name.c_str());
    SNN_LOGD("Input channel count: %u", _desc.numInputPlanes);
    SNN_LOGD("Output channel count: %u", _desc.numOutputPlanes);
    for (std::size_t filter = 0; filter < _desc.numOutputPlanes; filter++) {
        SNN_LOGD("Updating weights for weight texture: %u, %u", this->weightTextures[filter].id(), this->weightTextures[filter].target());
        std::vector<float> weightVal(4 * _desc.kernelSize * _desc.kernelSize, 0.0);
        for (std::size_t filterPlane = 0; filterPlane < _desc.numInputPlanes; filterPlane++) {
            std::size_t idx = filter * _desc.numInputPlanes + filterPlane;
            for (std::size_t i = 0; i < _desc.kernelSize; i++) {
                for (std::size_t j = 0; j < _desc.kernelSize; j++) {
                    std::size_t weightValIdx = (4 * _desc.kernelSize * i) + (4 * j) + (filterPlane % 4);
                    // SNN_LOGD("Filling IDX %u in buffer with size %u", weightValIdx, weightVal.size());
                    if (_desc.preferrHalfPrecision) {
                        weightVal[weightValIdx] = snn::convertToMediumPrecision(_desc.weights[idx].at<float>(i, j));
                    } else {
                        weightVal[weightValIdx] = _desc.weights[idx].at<float>(i, j);
                    }
                    // weightVal[weightValIdx] = _desc.weights[idx].at<float>(i, j);
                }
            }
            if ((filterPlane + 1) % 4 == 0) {
                this->weightTextures[filter].bind(0);
                SNN_LOGD("Adding weights at layer: %u", filterPlane / 4);
                if (_desc.numInputPlanes > 4) {
                    this->weightTextures[filter].bind(0);
                    this->weightTextures[filter].setPixels((int) (filterPlane / 4), 0, 0, 0, _desc.kernelSize, _desc.kernelSize, 0, weightVal.data());
                    glFinish();
                    this->weightTextures[filter].unbind();
                } else {
                    this->weightTextures[filter].bind(0);
                    this->weightTextures[filter].setPixels(0, 0, 0, _desc.kernelSize, _desc.kernelSize, 0, weightVal.data());
                    glFinish();
                    this->weightTextures[filter].unbind();
                }
                this->weightTextures[filter].unbind();
                // std::cout << "Weights updated at: " << filter << " ID: " << this->weights[filter].id() << " Target: " << this->weights[filter].target() <<
                // std::endl << std::endl;
                weightVal.clear();
                weightVal.resize(_desc.kernelSize * _desc.kernelSize * 4, 0.0);
            }
        }
        if (!weightVal.empty() && _desc.numInputPlanes % 4 != 0) {
            // for (auto weightValue : weightVal) {
            //     SNN_LOGI("%f", weightValue);
            // }
            if (_desc.numInputPlanes > 4) {
                this->weightTextures[filter].bind(0);
                this->weightTextures[filter].setPixels((int) (_desc.numInputPlanes / 4), 0, 0, 0, _desc.kernelSize, _desc.kernelSize, 0, weightVal.data());
                glFinish();
                this->weightTextures[filter].unbind();
            } else {
                this->weightTextures[filter].bind(0);
                this->weightTextures[filter].setPixels(0, 0, 0, _desc.kernelSize, _desc.kernelSize, 0, weightVal.data());
                glFinish();
                this->weightTextures[filter].unbind();
            }
            // std::cout << "Weights updated at: " << filter << " ID: " << this->weights[filter].id() << " Target: " << this->weights[filter].target() <<
            // std::endl << std::endl;
        }
    }
}

void Conv2DLayer::setBufferWeights() const {
    uint8_t byteSize = _desc.preferrHalfPrecision ? 2 : 4;
    for (std::size_t filter = 0; filter < _desc.numOutputPlanes; filter++) {
        std::vector<uint8_t> weightVal(byteSize * 4 * _desc.numInputPlanes * _desc.kernelSize * _desc.kernelSize, 0);
        for (std::size_t filterPlane = 0; filterPlane < _desc.numInputPlanes; filterPlane++) {
            std::size_t idx = filter * _desc.numInputPlanes + filterPlane;
            for (std::size_t i = 0; i < _desc.kernelSize; i++) {
                for (std::size_t j = 0; j < _desc.kernelSize; j++) {
                    std::size_t weightValIdx = (byteSize * 4 * _desc.kernelSize * i) + (4 * j) + (filterPlane % 4);
                    std::vector<uint8_t> byteRep;
                    snn::getByteRepresentation(_desc.weights[idx].at<float>(i, j), byteRep, _desc.preferrHalfPrecision);
                    for (std::size_t byteIdx = 0; byteIdx < byteSize; byteIdx++) {
                        weightVal[weightValIdx + byteIdx] = byteRep.at(byteIdx);
                    }
                }
            }
        }
        switch (_desc.weightMode) {
        case snn::WeightAccessMethod::UNIFORM_BUFFER:
            this->weightUniformBuffers[filter].update(weightVal.data(), 0, weightVal.size());
            break;

        case snn::WeightAccessMethod::SSBO_BUFFER:
            this->weightSSBOBuffers[filter].update(weightVal.data(), 0, weightVal.size());
            break;

        default:
            break;
        }
    }
}

void Conv2DLayer::getWeightConstantsMultipleInput(std::vector<std::ostringstream>& weightConstants, const std::vector<std::vector<float>>& vWeightMatrices,
                                                  int idxOutput4or8Chunk, int outputChannels) const {
    int nWeightStride       = _desc.kernelSize * _desc.kernelSize;
    int numWeightsPerMatrix = nWeightStride * ROUND_UP_DIV_4(_desc.numInputPlanes);

    int channelsPerPass = 4;
    switch (_desc.mrtMode) {
    case snn::MRTMode::DOUBLE_PLANE:
        channelsPerPass = 8;
        break;

    case snn::MRTMode::QUAD_PLANE:
        channelsPerPass = 16;
        break;

    default:
        break;
    }

    std::vector<float> shaderOutputWeights(numWeightsPerMatrix * outputChannels, 0.0f);
    for (int idxInput4Chunk = 0; idxInput4Chunk < static_cast<int>(DIV_4_ROUND_UP(_desc.numInputPlanes)); ++idxInput4Chunk) {
        // InputPlane index
        int startOffset    = idxInput4Chunk * 4 * nWeightStride;
        int nInputChannels = std::min(4, static_cast<int>(_desc.numInputPlanes) - idxInput4Chunk * 4);
        // Do a transpose to match the RGBA output layout
        for (int idxSqKernel = 0; idxSqKernel < nWeightStride; ++idxSqKernel) {
            // Serialized element index (kernel x kernel)
            for (int idxOutput = channelsPerPass * idxOutput4or8Chunk, idxOutputChannel = 0;
                idxOutput < (channelsPerPass * idxOutput4or8Chunk + outputChannels); ++idxOutput, ++idxOutputChannel) {
                shaderOutputWeights[(startOffset + idxSqKernel * 4) + (idxOutputChannel * numWeightsPerMatrix)] =
                    vWeightMatrices[idxOutput][startOffset + idxSqKernel];
                if (nInputChannels > 1) {
                    shaderOutputWeights[(startOffset + idxSqKernel * 4 + 1) + (idxOutputChannel * numWeightsPerMatrix)] =
                        vWeightMatrices[idxOutput][startOffset + idxSqKernel + nWeightStride];
                }
                if (nInputChannels > 2) {
                    shaderOutputWeights[(startOffset + idxSqKernel * 4 + 2) + (idxOutputChannel * numWeightsPerMatrix)] =
                        vWeightMatrices[idxOutput][startOffset + idxSqKernel + nWeightStride * 2];
                }
                if (nInputChannels > 3) {
                    shaderOutputWeights[(startOffset + idxSqKernel * 4 + 3) + (idxOutputChannel * numWeightsPerMatrix)] =
                        vWeightMatrices[idxOutput][startOffset + idxSqKernel + nWeightStride * 3];
                }
            }
        }
    }

    for (int idxOutputChannel = 0; idxOutputChannel < outputChannels; ++idxOutputChannel) {
        int idxWeight = channelsPerPass * idxOutput4or8Chunk + idxOutputChannel;
        weightConstants[idxWeight].precision(10);

        for (int idxInputAndSqKernel = 0; idxInputAndSqKernel < numWeightsPerMatrix; idxInputAndSqKernel += 4) {
            weightConstants[idxWeight] << "vec4(";
            weightConstants[idxWeight] << std::fixed << shaderOutputWeights[(idxOutputChannel * numWeightsPerMatrix) + idxInputAndSqKernel];
            weightConstants[idxWeight] << ", ";
            weightConstants[idxWeight] << std::fixed << shaderOutputWeights[(idxOutputChannel * numWeightsPerMatrix) + idxInputAndSqKernel + 1];
            weightConstants[idxWeight] << ", ";
            weightConstants[idxWeight] << std::fixed << shaderOutputWeights[(idxOutputChannel * numWeightsPerMatrix) + idxInputAndSqKernel + 2];
            weightConstants[idxWeight] << ", ";
            weightConstants[idxWeight] << std::fixed << shaderOutputWeights[(idxOutputChannel * numWeightsPerMatrix) + idxInputAndSqKernel + 3];
            weightConstants[idxWeight] << ((idxInputAndSqKernel + 4 >= numWeightsPerMatrix) ? ")" : "),") << std::endl;
        }
    }
}

void Conv2DLayer::getWeightConstantsSingleInput(std::vector<std::ostringstream>& weightConstants, const std::vector<std::vector<float>>& vWeightMatrices,
                                                int shaderPass, int outputChannels) const {
    const int floatsPerVec4 = 4;
    int nEffectiveWeights   = _desc.kernelSize * _desc.kernelSize;
    int nWeightStride       = nEffectiveWeights;
    int numWeightsPerMatrix = nWeightStride * floatsPerVec4;

    int channelsPerPass = 4;
    switch (_desc.mrtMode) {
    case snn::MRTMode::DOUBLE_PLANE:
        channelsPerPass = 8;
        break;

    case snn::MRTMode::QUAD_PLANE:
        channelsPerPass = 16;
        break;

    default:
        break;
    }

    SNN_LOGD(
        "Conv2DLayer::getWeightConstantsI1(): \
        shaderPass = %d, nInputPlanes = %d, outputChannels = %d, nKernelSize = %d, nEffectiveWeights = %d, nWeightStride = %d, numWeightsPerMatrix = %d",
        shaderPass, _desc.numInputPlanes, outputChannels, _desc.kernelSize, nEffectiveWeights, nWeightStride, numWeightsPerMatrix);

    std::vector<float> shaderOutputWeights(outputChannels * numWeightsPerMatrix * _desc.numInputPlanes, 0.0f);

    for (int i = 0; i < _desc.numInputPlanes; ++i) { // InputPlane index
        SNN_LOGD("Size of the input weight matrix is: %d, %d", vWeightMatrices[i].size(), shaderOutputWeights.size());
        for (int k = channelsPerPass * shaderPass, w = 0; k < (channelsPerPass * shaderPass + outputChannels); ++k, ++w) {
            int destOffset = i * numWeightsPerMatrix + (w * numWeightsPerMatrix);
            std::memcpy((char*) &shaderOutputWeights[destOffset], (char*) &vWeightMatrices[k][i * numWeightsPerMatrix], numWeightsPerMatrix * sizeof(float));

            // std::memset((char *) &shaderOutputWeights[destOffset + nEffectiveWeights],
            //         0, (nWeightStride - nEffectiveWeights) * sizeof(float));
        }
    }

    for (int i = 0; i < outputChannels; ++i) {
        int weightIndex = channelsPerPass * shaderPass + i;
        weightConstants[weightIndex].precision(10);
        int effectiveNumWeightsPerMatrix = numWeightsPerMatrix;
        for (int k = 0; k < effectiveNumWeightsPerMatrix; k += floatsPerVec4) {
            weightConstants[weightIndex] << "vec4(";
            weightConstants[weightIndex] << std::fixed << shaderOutputWeights[(i * numWeightsPerMatrix) + k];
            weightConstants[weightIndex] << ", ";
            weightConstants[weightIndex] << std::fixed << shaderOutputWeights[(i * numWeightsPerMatrix) + k + 1];
            weightConstants[weightIndex] << ", ";
            weightConstants[weightIndex] << std::fixed << shaderOutputWeights[(i * numWeightsPerMatrix) + k + 2];
            weightConstants[weightIndex] << ", ";
            weightConstants[weightIndex] << std::fixed << shaderOutputWeights[(i * numWeightsPerMatrix) + k + 3];
            weightConstants[weightIndex] << ((k == effectiveNumWeightsPerMatrix - floatsPerVec4) ? ")" : "),") << std::endl;
        }
    }
    shaderOutputWeights.clear();
}

void Conv2DLayer::getPaddingOffset(uint32_t (&offsets)[4]) const {
    std::string paddingT = this->_desc.paddingT;
    std::string paddingB = this->_desc.paddingB;
    std::string paddingL = this->_desc.paddingL;
    std::string paddingR = this->_desc.paddingR;
    bool isdigit         = std::all_of(paddingT.begin(), paddingT.end(), ::isdigit);
    if (isdigit) {
        offsets[0] = std::stoul(paddingT);
        offsets[1] = std::stoul(paddingB);
        offsets[2] = std::stoul(paddingL);
        offsets[3] = std::stoul(paddingR);
    } else {
        if (paddingT == "valid" || paddingT == "none") {
            offsets[0] = 0;
            offsets[1] = 0;
            offsets[2] = 0;
            offsets[3] = 0;
        } else {
            if (_desc.kernelSize > 1) {
                offsets[0] = std::max(static_cast<uint32_t>(_desc.kernelSize / 2), (uint32_t) 1);
                offsets[1] = std::max(static_cast<uint32_t>(_desc.kernelSize / 2), (uint32_t) 1);
                offsets[2] = std::max(static_cast<uint32_t>(_desc.kernelSize / 2), (uint32_t) 1);
                offsets[3] = std::max(static_cast<uint32_t>(_desc.kernelSize / 2), (uint32_t) 1);
                if (_desc.kernelSize % 2 == 0) {
                    offsets[0] = offsets[0] - 1;
                    offsets[2] = offsets[2] - 1;
                }
            } else {
                offsets[0] = 0;
                offsets[1] = 0;
                offsets[2] = 0;
                offsets[3] = 0;
            }
        }
    }
}

void Conv2DLayer::getWeightConstantsIndividualInputs(std::vector<std::ostringstream>& weightConstants, const std::vector<std::vector<float>>& vWeightMatrices,
                                                     int shaderPass, int outputChannels) const {
    const int floatsPerVec4 = 4;
    int nEffectiveWeights   = _desc.kernelSize * _desc.kernelSize;
    int nWeightStride       = ((nEffectiveWeights + 3) / 4) * 4;
    int numWeightsPerMatrix = nWeightStride * _desc.numInputPlanes;

#ifdef USE_SECUREC
    errno_t errNumWeightConstants;
    errno_t errNumShader;
#endif
    if (numWeightsPerMatrix <= 0 || outputChannels <= 0) {
        SNN_LOGE("Invalid length: numWeightsPerMatrix: %d, outputChannels: %d", numWeightsPerMatrix, outputChannels);
        return;
    }

    std::vector<float> shaderOutputWeights(numWeightsPerMatrix * outputChannels, 0.0f);
    for (int i = 0; i < outputChannels; ++i) {
        int kIndex = i + 4 * shaderPass;
        for (int j = 0; j < static_cast<int>(_desc.numInputPlanes); ++j) {
            int destOffset = i * numWeightsPerMatrix + (j * nWeightStride);
#ifdef USE_SECUREC
            errNumShader = memcpy_s((char*) &shaderOutputWeights[destOffset], nEffectiveWeights * sizeof(float),
                                    (char*) &vWeightMatrices[kIndex][j * nEffectiveWeights], nEffectiveWeights * sizeof(float));
            if (0 != errNumShader) {
                SNN_LOGE("ConvolutionLayer::getWeightConstantsSeparateInputs - memcpy_s failed! return with error code %d", errNumShader);
            }

            errNumWeightConstants = memset_s((char*) &shaderOutputWeights[destOffset + nEffectiveWeights], (nWeightStride - nEffectiveWeights) * sizeof(float),
                                             0, (nWeightStride - nEffectiveWeights) * sizeof(float));
            if (0 != errNumWeightConstants) {
                SNN_LOGE("ConvolutionLayer::getWeightConstantsSeparateInputs - memset_s failed! return with error code %d", errNumWeightConstants);
            }
#else
            memcpy((char*) &shaderOutputWeights[destOffset], (char*) &vWeightMatrices[kIndex][j * nEffectiveWeights], nEffectiveWeights * sizeof(float));
            memset((char*) &shaderOutputWeights[destOffset + nEffectiveWeights], 0, (nWeightStride - nEffectiveWeights) * sizeof(float));
#endif
        }
    }

    int channelsPerPass = 4;
    switch (_desc.mrtMode) {
    case snn::MRTMode::DOUBLE_PLANE:
        channelsPerPass = 8;
        break;

    case snn::MRTMode::QUAD_PLANE:
        channelsPerPass = 16;
        break;

    default:
        break;
    }

    for (int i = 0; i < outputChannels; ++i) {
        int weightIndex = channelsPerPass * shaderPass + i;
        weightConstants[weightIndex].precision(10);
        for (int j = 0; j < numWeightsPerMatrix; j += floatsPerVec4) {
            weightConstants[weightIndex] << "vec4(";
            weightConstants[weightIndex] << std::fixed << shaderOutputWeights[(i * numWeightsPerMatrix) + j];
            weightConstants[weightIndex] << ", ";
            weightConstants[weightIndex] << std::fixed << shaderOutputWeights[(i * numWeightsPerMatrix) + j + 1];
            weightConstants[weightIndex] << ", ";
            weightConstants[weightIndex] << std::fixed << shaderOutputWeights[(i * numWeightsPerMatrix) + j + 2];
            weightConstants[weightIndex] << ", ";
            weightConstants[weightIndex] << std::fixed << shaderOutputWeights[(i * numWeightsPerMatrix) + j + 3];
            weightConstants[weightIndex] << ((j >= numWeightsPerMatrix - floatsPerVec4) ? ")" : "),") << std::endl;
        }
    }
}

void Conv2DLayer::getAllWeightConstants(std::vector<std::ostringstream>& weightConstants, uint32_t numShaderPasses) const {
    // TODO: This code is unoptimized; we can do this transposal in place instead
    auto& weightMatrices = _desc.weights;
    SNN_LOGD("Size of the weight matrices (Should match %d x %d): %d", _desc.numInputPlanes, _desc.numOutputPlanes, weightMatrices.size());

    auto nWeightStride = _desc.kernelSize * _desc.kernelSize;

    // Rounding up to the next divide by 4
    std::vector<std::vector<float>> vWeightMatrices(_desc.numOutputPlanes, std::vector<float>(nWeightStride * ROUND_UP_DIV_4(_desc.numInputPlanes), 1.0f));

    for (uint32_t idxInput = 0; idxInput < _desc.numInputPlanes; ++idxInput) {
        uint32_t startOffset = idxInput * nWeightStride;

        for (uint32_t idxOutput = 0; idxOutput < _desc.numOutputPlanes; ++idxOutput) {
            if (_desc.numInputPlanes == 1) {
                std::vector<float> dummyWeights(4 * nWeightStride, 0.0f);
                for (size_t i = 0; i < _desc.kernelSize; i++) {
                    for (size_t j = 0; j < _desc.kernelSize; j++) {
                        size_t linearDim           = 4 * (i * _desc.kernelSize + j);
                        dummyWeights.at(linearDim) = weightMatrices[idxOutput * _desc.numInputPlanes + idxInput].at<float>(i, j);
                    }
                }
                std::memcpy(vWeightMatrices[idxOutput].data(), dummyWeights.data(), 4 * nWeightStride * sizeof(float));
            } else {
                SNN_ASSERT(startOffset < nWeightStride * _desc.numInputPlanes);
                SNN_ASSERT(idxOutput * _desc.numInputPlanes + idxInput < weightMatrices.size());
                //(void)startOffset; (void)vWeightMatrices; (void)weightMatrices;
                std::memcpy(&vWeightMatrices[idxOutput][startOffset], weightMatrices[idxOutput * _desc.numInputPlanes + idxInput].data,
                            _desc.kernelSize * _desc.kernelSize * sizeof(float));
            }
        }
    }

    for (int i = 0; i < static_cast<int>(numShaderPasses); ++i) {
        int outputChannels;
        switch (_desc.mrtMode) {
        case snn::MRTMode::SINGLE_PLANE:
            outputChannels = std::min(4, static_cast<int>(_desc.numOutputPlanes) - i * 4);
            break;

        case snn::MRTMode::DOUBLE_PLANE:
            outputChannels = std::min(8, static_cast<int>(_desc.numOutputPlanes) - i * 8);
            break;

        case snn::MRTMode::QUAD_PLANE:
            outputChannels = std::min(16, static_cast<int>(_desc.numOutputPlanes) - i * 16);
            break;

        default:
            outputChannels = std::min(4, static_cast<int>(_desc.numOutputPlanes) - i * 4);
            break;
        }
        if (_desc.numInputPlanes == 1) {
            getWeightConstantsSingleInput(weightConstants, vWeightMatrices, i, outputChannels);
        } else {
            getWeightConstantsMultipleInput(weightConstants, vWeightMatrices, i, outputChannels);
        }
    }
    // for (auto& vec : vWeightMatrices) {
    //     vec.clear();
    // }
    // vWeightMatrices.clear();
    // SNN_LOGD("Got all weight matrices");
}

void Conv2DLayer::buildDotProductLogic(std::ostringstream& stream) const {
    uint32_t iter = _desc.kernelSize;
    uint32_t k    = 0;
    for (uint32_t ip = 0; ip < 1024; ip += 4) {
        if (ip == 0) {
            stream << "if (i==" << ip << ") {" << std::endl;
        } else if (ip > 0 && ip != 1020) {
            stream << "\t\t\t#if NUM_INPUT_PLANES > " << ip << std::endl << std::endl;
            stream << "\t\telse if(i ==" << ip << ") {" << std::endl;
        } else {
            stream << "\t#if NUM_INPUT_PLANES > " << ip << std::endl << std::endl;
            stream << "\t\telse {" << std::endl;
        }

        if (iter > 1) {
            stream << "\t\t\ts.r += (dot(t0, weightMatrix1[" << k << "]) +" << std::endl;
        }
        else {
            stream << "\t\t\ts.r += (dot(t0, weightMatrix1[" << k << "]));" << std::endl;
        }
        for (uint32_t j = 1; j < iter * iter; j++) {
            if (j != iter * iter - 1) {
                stream << "\t\t\tdot(t" << j << ", "
                        << "weightMatrix1[" << k + j << "]) +" << std::endl;
            } else {
                stream << "\t\t\tdot(t" << j << ", "
                        << "weightMatrix1[" << k + j << "]));" << std::endl;
            }
        }

        stream << "\t\t\t#ifdef USE_COMPONENT_G" << std::endl;
        if (iter > 1) {
            stream << "\t\t\ts.g += (dot(t0, weightMatrix2[" << k << "]) +" << std::endl;
        } else {
            stream << "\t\t\ts.g += (dot(t0, weightMatrix2[" << k << "]));" << std::endl;
        }
        for (uint32_t j = 1; j < iter * iter; j++) {
            if (j != iter * iter - 1) {
                stream << "\t\t\tdot(t" << j << ", "
                        << "weightMatrix2[" << k + j << "]) +" << std::endl;
            } else {
                stream << "\t\t\tdot(t" << j << ", "
                        << "weightMatrix2[" << k + j << "]));" << std::endl;
            }
        }
        stream << "\t\t\t#endif" << std::endl << std::endl;

        stream << "\t\t\t#ifdef USE_COMPONENT_B" << std::endl;
        if (iter > 1) {
            stream << "\t\t\ts.b += (dot(t0, weightMatrix3[" << k << "]) +" << std::endl;
        } else {
            stream << "\t\t\ts.b += (dot(t0, weightMatrix3[" << k << "]));" << std::endl;
        }
        for (uint32_t j = 1; j < iter * iter; j++) {
            if (j != iter * iter - 1) {
                stream << "\t\t\tdot(t" << j << ", "
                        << "weightMatrix3[" << k + j << "]) +" << std::endl;
            } else {
                stream << "\t\t\tdot(t" << j << ", "
                        << "weightMatrix3[" << k + j << "]));" << std::endl;
            }
        }
        stream << "\t\t\t#endif" << std::endl << std::endl;

        stream << "\t\t\t#ifdef USE_COMPONENT_A" << std::endl;
        if (iter > 1) {
            stream << "\t\t\ts.a += (dot(t0, weightMatrix4[" << k << "]) +" << std::endl;
        } else {
            stream << "\t\t\ts.a += (dot(t0, weightMatrix4[" << k << "]));" << std::endl;
        }
        for (uint32_t j = 1; j < iter * iter; j++) {
            if (j != iter * iter - 1) {
                stream << "\t\t\tdot(t" << j << ", "
                        << "weightMatrix4[" << k + j << "]) +" << std::endl;
            } else {
                stream << "\t\t\tdot(t" << j << ", "
                        << "weightMatrix4[" << k + j << "]));" << std::endl;
            }
        }
        stream << "\t\t\t#endif" << std::endl;
        stream << "\t\t}" << std::endl;

        if (ip > 0) {
            stream << "\t\t\t#endif" << std::endl << std::endl;
        }
        k += (iter * iter);
    }
}

void Conv2DLayer::getBiasConstants(std::vector<std::ostringstream>& biasConstants, uint32_t numShaderPasses) const {
    uint32_t outputPlanes = _desc.numOutputPlanes;
    int channelsPerPass   = 4;
    switch (_desc.mrtMode) {
    case snn::MRTMode::SINGLE_PLANE:
        channelsPerPass = 4;
        break;

    case snn::MRTMode::DOUBLE_PLANE:
        channelsPerPass = 8;
        break;

    case snn::MRTMode::QUAD_PLANE:
        channelsPerPass = 16;
        break;

    default:
        channelsPerPass = 4;
        break;
    }
    uint32_t currentPlanes = 0;
    for (std::size_t i = 0; i < numShaderPasses; i++) {
        if (outputPlanes > channelsPerPass) {
            currentPlanes = channelsPerPass;
            outputPlanes  = outputPlanes - channelsPerPass;
        } else {
            currentPlanes = outputPlanes;
            outputPlanes  = 0;
        }

        // Force the Bias to 0. T.B.D.
        // biasConstants.at(i) << "const FLOAT_PRECISION vec4 bias = vec4("
        //             << "0.0, "
        //             << "0.0, "
        //             << "0.0, "
        //             << "0.0);";
        // continue;
        biasConstants.at(i) << "const FLOAT_PRECISION vec4 bias[] = vec4[](";
        for (std::size_t j = 0; j < DIV_4_ROUND_UP(currentPlanes); j++) {
            int remainingPlanes = ((currentPlanes - (j * 4)) > 4) ? 4 : (currentPlanes - (j * 4));
            switch (remainingPlanes) {
            case 4:
                biasConstants.at(i) << "vec4(" << _desc.biases.at(channelsPerPass * i + 4 * j) << ", " << _desc.biases.at(channelsPerPass * i + 4 * j + 1)
                                    << ", " << _desc.biases.at(channelsPerPass * i + 4 * j + 2) << ", " << _desc.biases.at(channelsPerPass * i + 4 * j + 3)
                                    << (((currentPlanes - (j * 4)) > 4) ? "),\n" : "));\n");

                break;

            case 3:
                biasConstants.at(i) << "vec4(" << _desc.biases.at(channelsPerPass * i + 4 * j) << ", " << _desc.biases.at(channelsPerPass * i + 4 * j + 1)
                                    << ", " << _desc.biases.at(channelsPerPass * i + 4 * j + 2) << ", "
                                    << "0.0" << (((currentPlanes - (j * 4)) > 4) ? "),\n" : "));\n");
                break;

            case 2:
                biasConstants.at(i) << "vec4(" << _desc.biases.at(channelsPerPass * i + 4 * j) << ", " << _desc.biases.at(channelsPerPass * i + 4 * j + 1)
                                    << ", "
                                    << "0.0, "
                                    << "0.0" << (((currentPlanes - (j * 4)) > 4) ? "),\n" : "));\n");
                break;

            case 1:
                biasConstants.at(i) << "vec4(" << _desc.biases.at(channelsPerPass * i + 4 * j) << ", "
                                    << "0.0, "
                                    << "0.0, "
                                    << "0.0" << (((currentPlanes - (j * 4)) > 4) ? "),\n" : "));\n");
                break;

            default:
                biasConstants.at(i) << " vec4("
                                    << "0.0, "
                                    << "0.0, "
                                    << "0.0, "
                                    << "0.0" << (((currentPlanes - (j * 4)) > 4) ? "),\n" : "));\n");
                break;
            }
        }
    }
}

void Conv2DLayer::getBatchNormalizationConstants(std::vector<std::ostringstream>& batchNormalizationConstants, const int shaderPass,
                                                 const int outputChannels) const {
    auto iter           = _desc.batchNormalization.begin();
    int nStartIndex     = 0;
    int channelsPerPass = 4;
    switch (_desc.mrtMode) {
    case snn::MRTMode::DOUBLE_PLANE:
        channelsPerPass = 8;
        break;

    case snn::MRTMode::QUAD_PLANE:
        channelsPerPass = 16;
        break;

    default:
        channelsPerPass = 4;
        break;
    }
    int planeCount = DIV_4_ROUND_UP(channelsPerPass);
    int index      = shaderPass * 4;
    for (; iter != _desc.batchNormalization.end(); ++iter, ++nStartIndex) {
        std::vector<float> shaderOutputBNConstants(outputChannels);
        batchNormalizationConstants[index + nStartIndex] << "vec4(";
        for (int i = 0; i < outputChannels; ++i) {
            if (i % 4 == 0 && i > 0) {
                batchNormalizationConstants[index + nStartIndex] << ", \n\tvec4(";
            }
            shaderOutputBNConstants[i] = iter->second[planeCount * index + i];
            batchNormalizationConstants[index + nStartIndex] << std::fixed << shaderOutputBNConstants[i] << (((i + 1) % 4 == 0) ? ") " : ",");
        }
    }
}

void Conv2DLayer::buildElementAccess(std::ostream& stream, std::string padding, bool isFirstLayer) const {
    uint32_t paddingOffsets[4];
    this->getPaddingOffset(paddingOffsets);

    std::vector<float> coordOffsetsW(_desc.kernelSize), coordOffsetsH(_desc.kernelSize);
    std::iota(coordOffsetsW.begin(), coordOffsetsW.end(), -0.5 - paddingOffsets[0]);
    std::iota(coordOffsetsH.begin(), coordOffsetsH.end(), -0.5 - paddingOffsets[2]);

    std::string paddingStr;
    std::string baseTabLevel = "\t\t";
    auto getTabLevels        = [&](int level) {
        std::string tabLevel = baseTabLevel;
        for (int i = 0; i < level; i++) {
            tabLevel += "\t";
        }
        return tabLevel;
    };
    if (std::isdigit(padding.front()) && padding != "0") {
        paddingStr = "same";
    } else if (padding == "0") {
        paddingStr = "none";
    } else {
        paddingStr = padding;
    }

    for (uint32_t i = 0; i < _desc.kernelSize; i++) {
        for (uint32_t j = 0; j < _desc.kernelSize; j++) {
            stream << getTabLevels(0) << "vec2 texCoord_" << _desc.kernelSize * i + j + 1 << " = (vec2(baseCoord) + ";
            stream << "vec2(" << coordOffsetsW.at(j) << ", " << coordOffsetsH.at(i) << ")) / vec2(maxUV);" << std::endl;
        }
    }

    stream << getTabLevels(0) << "for (int i = 0; i < NUM_INPUT_PLANES; i+= 4) {" << std::endl;
    stream << getTabLevels(1) << "int layer = i >> 2;" << std::endl;
    stream << "#ifdef USE_MULTI_INPUTS" << std::endl;
    stream << getTabLevels(1) << "layer = (i + 4 * int((NUM_INPUT_PLANES + 3) / 4)) >> 2;" << std::endl;
    stream << "#endif" << std::endl;

    if (paddingOffsets[0] == 0 || paddingStr == "valid" || paddingStr == "none") {
        stream << getTabLevels(1) << "bool validCoord = true;" << std::endl;
    }

    for (uint32_t i = 0; i < _desc.kernelSize; i++) {
        for (uint32_t j = 0; j < _desc.kernelSize; j++) {
            int linearDim = _desc.kernelSize * i + j;
#if CLAMPED_PADDING == 0
            if (paddingStr == "same") {
                stream << getTabLevels(1) << "FLOAT_PRECISION vec4 t" << linearDim << " = (checkValid(texCoord_" << linearDim + 1 << ")) ? ";
                stream << "TEXTURE(inputTextures, vec3(texCoord_";
                stream << linearDim + 1 << ", layer)) : vec4(PAD_VALUE, PAD_VALUE, PAD_VALUE, PAD_VALUE);\n";
            } else if (paddingStr == "replicate") {
                stream << getTabLevels(1) << "FLOAT_PRECISION vec2 repCoords" << linearDim << " = replicatePadding(texCoord_" << linearDim + 1 << ");\n";
                stream << getTabLevels(1) << "FLOAT_PRECISION vec4 t" << linearDim << " = (checkValid(texCoord_" << linearDim + 1 << ")) ? ";
                stream << "TEXTURE(inputTextures, vec3(texCoord_" << linearDim + 1 << ", layer)) : TEXTURE(inputTextures, vec3(repCoords" << linearDim + 1
                        << ", layer));\n";
            } else if (paddingStr == "valid" || paddingStr == "none") {
                stream << getTabLevels(1) << "FLOAT_PRECISION vec4 t" << linearDim << " = TEXTURE(inputTextures, vec3(texCoord_" << linearDim + 1
                        << ", layer));\n";
            }
#else
            stream << getTabLevels(1) << "FLOAT_PRECISION vec4 t" << linearDim << " = TEXTURE(inputTextures, vec3(texCoord_" << linearDim + 1 << ", layer));\n";
#endif
            // This line implements clamping of zero valued pixels.
            // This necessary only when the input is between 0 and 1, and the final layer
            // is an image-like output (segmentation or denoised image) with a sigmoid activation.
            // In case of all zeros, this leads to a 0.5 at the output, when we want a 0
            // By adding a small offset or clamp value, we avoid this issue, and the output is zero
            // like we expect
            if (this->isRange01 && isFirstLayer) {
                stream << getTabLevels(1) << "t" << linearDim << " = max(t" << linearDim << ", vec4(0.001));\n";
            }
        }
    }
}

void Conv2DLayer::buildFragmentCalc(std::ostringstream& stream) const {
    std::string baseTabLevel = "\t\t\t";
    uint32_t offsets[4];
    this->getPaddingOffset(offsets);
    if (offsets[0] == 0) {
        baseTabLevel += "\t";
    }
    int numInput = _desc.numInputPlanes;
    if (numInput % 4 != 0) {
        numInput = ((int) (numInput / 4) + 1) * 4;
    }
    const char channelVal[4]      = {'r', 'g', 'b', 'a'};
    const char channelValUpper[4] = {'R', 'G', 'B', 'A'};
    int channelsPerPass           = 4;
    switch (_desc.mrtMode) {
    case snn::MRTMode::DOUBLE_PLANE:
        channelsPerPass = 8;
        break;

    case snn::MRTMode::QUAD_PLANE:
        channelsPerPass = 16;
        break;

    default:
        break;
    }
    const int weightsOffset = _desc.kernelSize * _desc.kernelSize;
    int linearDim           = 0;
    for (int layerVal = 0; layerVal < numInput; layerVal += 4) {
        if (layerVal == 0) {
            stream << baseTabLevel << "if (i == " << layerVal << ") {" << std::endl;
        } else {
            stream << "#if NUM_INPUT_PLANES > " << layerVal << std::endl;
            stream << baseTabLevel << "else if (i == " << layerVal << ") {" << std::endl;
        }
        for (std::size_t channel = 0; channel < channelsPerPass; channel++) {
            stream << "#ifdef USE_COMPONENT_" << channelValUpper[channel % 4] << "_PLANE_" << channel / 4 << std::endl;
            if (channel / 4 == 0) {
                stream << baseTabLevel << "\ts." << channelVal[channel % 4] << " += (";
            } else {
                stream << baseTabLevel << "\ts" << channel / 4 << "." << channelVal[channel % 4] << " += (";
            }
            for (std::size_t i = 0; i < _desc.kernelSize; i++) {
                for (std::size_t j = 0; j < _desc.kernelSize; j++) {
                    linearDim = i * _desc.kernelSize + j;
                    if (linearDim == weightsOffset - 1) {
                        stream << baseTabLevel << "dot(t" << linearDim << ", weights" << channel + 1;
                        stream << "[" << (layerVal / 4) * weightsOffset + linearDim << "]));" << std::endl;
                    } else if (linearDim == 0) {
                        stream << "dot(t" << linearDim << ", weights" << channel + 1;
                        stream << "[" << (layerVal / 4) * weightsOffset + linearDim << "]) +" << std::endl;
                    } else {
                        stream << baseTabLevel << "\tdot(t" << linearDim << ", weights" << channel + 1;
                        stream << "[" << (layerVal / 4) * weightsOffset + linearDim << "]) +" << std::endl;
                    }
                }
            }
            stream << "#endif" << std::endl;
        }
        if (layerVal != 0) {
            stream << "#endif" << std::endl;
        }
        stream << baseTabLevel << "\t}" << std::endl;
    }
}

void Conv2DLayer::buildPreDefine(std::ostringstream& stream, const LayerGenOptions& options, const std::string& shaderFilePath) const {
    uint32_t offsets[4];
    this->getPaddingOffset(offsets);
    stream << "#version 320 es\n";
    stream << "// " << shaderFilePath << std::endl;
    stream << "#define NUM_INPUT_PLANES " << _desc.numInputPlanes << std::endl;
    stream << "#define NUM_OUTPUT_PLANES " << _desc.numOutputPlanes << std::endl;
    stream << "#define NUM_KERNEL_SIZE " << _desc.kernelSize << std::endl;
    stream << "#define INPUT_WIDTH " << options.desiredInput.width << std::endl;
    stream << "#define INPUT_HEIGHT " << options.desiredInput.height << std::endl;
    stream << "#define NUM_STRIDE " << _desc.stride << std::endl;
    stream << "#define PAD_VALUE 0.0f" << std::endl;
#if CLAMPED_PADDING == 1
    stream << "#define CLAMPED_PADDING" << std::endl;
#endif
    // Currently we have no way of getting which value
    // is being used to pad. So we default to 0.0

    switch (_desc.weightMode) {
    case snn::WeightAccessMethod::SSBO_BUFFER:
        stream << "#define USE_WEIGHT_BUFFERS\n";
        stream << "#define STORAGE_FORMAT std430\n";
        stream << "#define VARIABLE_SPECIFIER buffer\n";
        break;

    case snn::WeightAccessMethod::UNIFORM_BUFFER:
        stream << "#define USE_WEIGHT_BUFFERS\n";
        stream << "#define STORAGE_FORMAT std140\n";
        stream << "#define VARIABLE_SPECIFIER uniform\n";
        break;

    case snn::WeightAccessMethod::TEXTURES:
        stream << "#define USE_WEIGHT_TEXTURES\n";
        break;

    case snn::WeightAccessMethod::CONSTANTS:
        stream << "#define USE_WEIGHT_CONSTANTS\n";
        break;

    default:
        SNN_LOGE("No Weight setting found!");
        break;
    }

    if (_desc.useMultiInputs) {
        stream << "#define USE_MULTI_INPUTS " << std::endl;
    }

    if (options.isFirstLayer) {
        if (getDesc().isRange01) {
            stream << "#define REMOVE_ZERO 1" << std::endl;
        } else {
            stream << "#define SCALE_INPUT 1" << std::endl;
        }
    }
    if (_desc.numInputPlanes <= 4) {
        stream << "#define INPUT_TEXTURE_2D\n";
    }
    if (_desc.useBatchNormalization) {
        stream << "#define USE_BATCH_NORMALIZATION\n";
    }

    if (_desc.useUniformShaders) {
        stream << "#define USE_UNIFORM_WEIGHTS" << std::endl;
        stream << "#define PADDING_T " << offsets[0] << std::endl;
        stream << "#define PADDING_B " << offsets[1] << std::endl;
        stream << "#define PADDING_L " << offsets[2] << std::endl;
        stream << "#define PADDING_R " << offsets[3] << std::endl;
        if (_desc.paddingT == "same") {
            stream << "#define CONST_PADDING" << std::endl;
        } else if (_desc.paddingT == "replicate") {
            stream << "#define REPLCIATE_PADDING" << std::endl;
        } else {
            stream << "#define CHECKBOARD_PADDING" << std::endl;
        }
    } else {
        stream << "#define PADDING_W " << offsets[0] << std::endl;
        stream << "#define PADDING_H " << offsets[2] << std::endl;
    }
}

// Adds last text for fragment shader.
void Conv2DLayer::buildFragmentPostDefine(std::ostream& stream) const {
    // std::cout << "------------ [Debug activation] ------------" << std::endl;
    // std::cout << _desc.activation << std::endl;
    // std::cout << "--------------------------------------------" << std::endl;
#if 0
    // MRT code is currently disabled.
    if (shader.isMRT) {
        postDefine << (!_desc.activation.compare("relu") ? "s = max(s, 0.0);\n" :
            !_desc.activation.compare("tanh") ? "s = tanh(s);\n" :
            !_desc.activation.compare("sigmoid") ? "s = vec4(1.0f)/(vec4(1.0f) + exp(-s));\n" :
            !_desc.activation.compare("SiLU") ? "s = s * vec4(1.0f)/(vec4(1.0f)+ exp(-s))" : "");
        postDefine << (!_desc.activation.compare("relu") ? "s2 = max(s2, 0.0);\n" :
            !_desc.activation.compare("tanh") ? "s2 = tanh(s2);\n" :
            !_desc.activation.compare("sigmoid") ? "s2 = vec4(1.0f)/(vec4(1.0f) + exp(-s2));\n" :
            !_desc.activation.compare("SiLU") ? "s2 = s2 * vec4(1.0f)/(vec4(1.0f)+ exp(-s2))" : "");
        postDefine << (!_desc.activation.compare("relu") ? "s3 = max(s3, 0.0);\n" :
            !_desc.activation.compare("tanh") ? "s3 = tanh(s3);\n" :
            !_desc.activation.compare("sigmoid") ? "s3 = vec4(1.0f)/(vec4(1.0f) + exp(-s3));\n" :
            !_desc.activation.compare("SiLU") ? "s3 = s3 * vec4(1.0f)/(vec4(1.0f)+ exp(-s3))\n" : "");
        postDefine << (!_desc.activation.compare("relu") ? "s4 = max(s4, 0.0);\n" :
            !_desc.activation.compare("tanh") ? "s4 = tanh(s4);\n" :
            !_desc.activation.compare("sigmoid") ? "s4 = vec4(1.0f)/(vec4(1.0f) + exp(-s4));\n" :
            !_desc.activation.compare("SiLU") ? "s4 = s4 * vec4(1.0f)/(vec4(1.0f)+ exp(-s4));\n" : "");
        postDefine << "o_pixel = s;\n";
        postDefine << "o_pixel2 = s2;\n";
        postDefine << "o_pixel3 = s3;\n";
        postDefine << "o_pixel4 = s4;\n";
        postDefine << "}\n";
    }
    else if (shader.isRGBA) {
#endif
    const char* identifiers[4] = {"", "1", "2", "3"};
    for (std::size_t i = 0; i < 4; i++) {
        stream << "#if PLANE_COUNT > " << i << "\n";
        stream << (!_desc.activation.compare("relu")
                    ? "\ts" + std::string(identifiers[i]) + " = max(s" + std::string(identifiers[i]) + ", vec4(0.0));\n"
                    : !_desc.activation.compare("relu6")
                            ? "\ts" + std::string(identifiers[i]) + " = min(vec4(6.0),max(s" + std::string(identifiers[i]) + ", vec4(0.0)));\n"
                            : !_desc.activation.compare("tanh")
                                ? "\ts" + std::string(identifiers[i]) + " = tanh(s" + std::string(identifiers[i]) + ");\n"
                                : !_desc.activation.compare("sigmoid")
                                        ? "\ts" + std::string(identifiers[i]) + " = vec4(1.0f)/(vec4(1.0f)+ exp(-s" + std::string(identifiers[i]) + "));\n"
                                        : !_desc.activation.compare("leakyRelu")
                                            ? "\ts" + std::string(identifiers[i]) + " = max(s" + std::string(identifiers[i]) + ", (s" +
                                                    std::string(identifiers[i]) + " * vec4(" + std::to_string(_desc.leakyReluAlpha) + "f)));\n"
                                            : !_desc.activation.compare("leaky_relu")
                                                    ? "\ts" + std::string(identifiers[i]) + " = max(s" + std::string(identifiers[i]) + " , (s" +
                                                        std::string(identifiers[i]) + " * vec4(" + std::to_string(_desc.leakyReluAlpha) + "f)));\n"
                                                    : !_desc.activation.compare("SiLU")
                                                        ? "s" + std::string(identifiers[i]) + " = s" + std::string(identifiers[i]) +
                                                                " * vec4(1.0f)/(vec4(1.0f)+ exp(-s" + std::string(identifiers[i]) + "));\n"
                                                        : "");
        stream << "\to_pixel" << std::string(identifiers[i]) << " = s" << std::string(identifiers[i]) << ";\n";
        stream << "#endif\n";
    }
    stream << "}\n";
#if 0
    }
    else {
        stream << (!_desc.activation.compare("relu") ? "s = max(s, vec4(0.0));\n" :
            !_desc.activation.compare("tanh") ? "s = tanh(s);\n" :
            !_desc.activation.compare("sigmoid") ? "s = vec4(1.0f)/(vec4(1.0f) + exp(-s));\n" : "");
            !_desc.activation.compare("SiLU") ? "s = s * vec4(1.0f)/(vec4(1.0f)+ exp(-s));\n" : "");
        stream << "o_pixel = s;\n";
        stream << "}\n";
    }
#endif
}

// Adds last text for compute shader.
void Conv2DLayer::buildComputePostDefine(std::ostream& stream, uint32_t outputSliceIndex) const {
    // std::cout << "------------ [Debug activation] ------------" << std::endl;
    // std::cout << _desc.activation << std::endl;
    // std::cout << "--------------------------------------------" << std::endl;
    stream << (!_desc.activation.compare("relu")
                ? "s = max(s, vec4(0.0));\n"
                : !_desc.activation.compare("relu6")
                        ? "s = min(vec4(6.0),max(s, vec4(0.0)));\n"
                        : !_desc.activation.compare("tanh")
                            ? "s = tanh(s);\n"
                            : !_desc.activation.compare("sigmoid")
                                    ? "s = vec4(1.0f)/(vec4(1.0f)+ exp(-s));\n"
                                    : !_desc.activation.compare("leakyRelu")
                                        ? "s = max(s, (s * vec4(" + std::to_string(_desc.leakyReluAlpha) + ")));\n"
                                        : !_desc.activation.compare("SiLU") ? "s = s * vec4(1.0f)/(vec4(1.0f)+ exp(-s));\n" : "");
    if (_desc.numOutputPlanes > 4) {
        stream << "imageStore(outTexture,ivec3(gl_GlobalInvocationID.xy, " << outputSliceIndex << "),s);\n";
    } else {
        stream << "imageStore(outTexture,ivec2(gl_GlobalInvocationID.xy),s);\n";
    }
    stream << "}\n";
}

ShaderLayer::GLSLShaders Conv2DLayer::createFS(const LayerGenOptions& options) const {
    std::ostringstream shaderFilePathStream;
    if (_desc.useUniformShaders) {
        switch (_desc.weightMode) {
        case snn::WeightAccessMethod::CONSTANTS:
            break;
        case snn::WeightAccessMethod::TEXTURES:
            this->setTextureWeights();
            break;
        case snn::WeightAccessMethod::UNIFORM_BUFFER:
            this->setBufferWeights();
            break;
        case snn::WeightAccessMethod::SSBO_BUFFER:
            this->setBufferWeights();
            break;
        default:
            break;
        }
    }

#if OLD_SHADER
    std::string fileName = std::string("/shadertemplate_fs_");
    fileName += std::to_string(_desc.kernelSize) + "x";

    fileName = fileName + "_stride_RGBA.glsl";

    if (_desc.numInputPlanes == 1) {
        // SNN_LOGD("Silent for now!");
        // fileName +="1";
    }
    shaderFilePathStream << "shaders" << fileName;

    std::string shaderFilePath = shaderFilePathStream.str();
#else
    // std::string fileName = "/shadertemplate_fs_conv2d_RGBA.glsl";
    // shaderFilePathStream << "shaders" << fileName;
    // std::string shaderFilePath = shaderFilePathStream.str();
    std::string shaderFilePath = CONV2D_FS_ASSET_NAME;
#endif

    std::string fsTemplateCode = loadShader(shaderFilePath.c_str());

    // TODO: need to take MRT into consideration
    // uint32_t numShaderPasses = DIV_4_ROUND_UP(_desc.numOutputPlanes);
    int channelsPerPass = 4;
    switch (_desc.mrtMode) {
    case snn::MRTMode::SINGLE_PLANE:
        channelsPerPass = 4;
        break;

    case snn::MRTMode::DOUBLE_PLANE:
        channelsPerPass = 8;
        break;

    case snn::MRTMode::QUAD_PLANE:
        channelsPerPass = 16;
        break;

    default:
        channelsPerPass = 4;
        break;
    }
    uint32_t numShaderPasses = DIV_AND_ROUND_UP(_desc.numOutputPlanes, channelsPerPass);
    std::vector<std::ostringstream> weightConstants(_desc.numOutputPlanes);
    if (_desc.weightMode == snn::WeightAccessMethod::CONSTANTS) {
        getAllWeightConstants(weightConstants, numShaderPasses);
    }

    SNN_LOGD("Initialized weight matrices");

    // Build beginning shader code.
    std::ostringstream preDefineStream;
    buildPreDefine(preDefineStream, options, shaderFilePath);
    SNN_LOGD("Initialized pre define str()");
    std::string preDefine = preDefineStream.str();

    // Build ending shader code.
    std::ostringstream postDefineStream;
    this->buildFragmentPostDefine(postDefineStream);
    std::string postDefine = postDefineStream.str();
#if OLD_SHADER
    std::ostringstream DotProductDefStream;
    this->buildDotProductLogic(DotProductDefStream);
    auto DotProductDef = DotProductDefStream.str();
#else
    std::string calc, elementAccess;
    if (!_desc.useUniformShaders) {
        std::ostringstream elementAccessStream;
        this->buildElementAccess(elementAccessStream, _desc.paddingT, options.isFirstLayer);
        // this->buildElementAccess(elementAccessStream, "none");
        elementAccess = elementAccessStream.str();

        std::ostringstream calcStream;
        this->buildFragmentCalc(calcStream);
        calc = calcStream.str();
    }
#endif
    int numBatchNormParameters = 4;
    std::vector<std::ostringstream> batchNormalizationConstants(numBatchNormParameters * numShaderPasses);
    std::vector<std::ostringstream> biasStr(numShaderPasses);
    this->getBiasConstants(biasStr, numShaderPasses);

    GLSLShaders ret;
    std::vector<InferenceGraph::Pass>& passes = ret.passes;
    passes.resize(numShaderPasses);
    for (uint32_t i = 0, j = 0; i < numShaderPasses; ++i, j += channelsPerPass) {
        uint32_t outputChannels =
            static_cast<uint32_t>(std::min(channelsPerPass, static_cast<int>(_desc.numOutputPlanes) - static_cast<int>(i) * channelsPerPass));
        uint32_t planeCount = DIV_4_ROUND_UP(outputChannels);
        std::ostringstream rgbaDefine;
        for (std::size_t planeIdx = 0; planeIdx < planeCount; planeIdx++) {
            std::size_t remainingPlanes = static_cast<uint32_t>(std::min(4, static_cast<int>(outputChannels) - static_cast<int>(planeIdx) * 4));
            switch (remainingPlanes) {
            case 4:
                rgbaDefine << "#define USE_COMPONENT_A_PLANE_" << planeIdx << "\n";
                rgbaDefine << "#define USE_COMPONENT_B_PLANE_" << planeIdx << "\n";
                rgbaDefine << "#define USE_COMPONENT_G_PLANE_" << planeIdx << "\n";
                rgbaDefine << "#define USE_COMPONENT_R_PLANE_" << planeIdx << "\n";
                break;
            case 3:
                rgbaDefine << "#define USE_COMPONENT_B_PLANE_" << planeIdx << "\n";
                rgbaDefine << "#define USE_COMPONENT_G_PLANE_" << planeIdx << "\n";
                rgbaDefine << "#define USE_COMPONENT_R_PLANE_" << planeIdx << "\n";
                break;
            case 2:
                rgbaDefine << "#define USE_COMPONENT_G_PLANE_" << planeIdx << "\n";
                rgbaDefine << "#define USE_COMPONENT_R_PLANE_" << planeIdx << "\n";
                break;
            case 1:
                rgbaDefine << "#define USE_COMPONENT_R_PLANE_" << planeIdx << "\n";
                break;
            default:
                break;
            }
        }

        rgbaDefine << "#define PLANE_COUNT " << planeCount << "\n";

        // Create a copy of the template code.
        // After modification, this will contain the shader's true source code.
        std::string fsCode = fsTemplateCode;
        if (!_desc.useUniformShaders) {
            for (uint32_t k = 0; k < outputChannels; ++k) {
                std::ostringstream weightConstantShaderPlaceholder;
                weightConstantShaderPlaceholder << "_PLACEHOLDER_WEIGHT" << k + 1 << "_VEC_CONSTANTS_";
                findAndReplace(fsCode, weightConstantShaderPlaceholder.str(), weightConstants[j + k].str());
            }
            findAndReplace(fsCode, "_PLACEHOLDER_BIAS_CONSTANTS_", biasStr.at(i).str());
        }

        // findAndReplace(fsCode, "_PLACEHOLDER_BIAS_CONSTANTS_", biasStr.at(i).str());

        if (_desc.preferHp) {
            findAndReplace(fsCode, "_PLACEHOLDER_PRECISION_", "mediump");
        } else {
            findAndReplace(fsCode, "_PLACEHOLDER_PRECISION_", "highp");
        }

        if (_desc.useBatchNormalization && !_desc.useUniformShaders) {
            getBatchNormalizationConstants(batchNormalizationConstants, (int) i, (int) outputChannels);
            findAndReplace(fsCode, "_PLACEHOLDER_BETA_VEC_CONSTANTS_", batchNormalizationConstants[numBatchNormParameters * i].str());
            findAndReplace(fsCode, "_PLACEHOLDER_GAMMA_VEC_CONSTANTS_", batchNormalizationConstants[numBatchNormParameters * i + 1].str());
            findAndReplace(fsCode, "_PLACEHOLDER_MOVINGMEAN_VEC_CONSTANTS_", batchNormalizationConstants[numBatchNormParameters * i + 2].str());
            findAndReplace(fsCode, "_PLACEHOLDER_MOVINGVARIANCE_VEC_CONSTANTS_", batchNormalizationConstants[numBatchNormParameters * i + 3].str());
        }
#if OLD_SHADER
        findAndReplace(fsCode, "PLACEHOLDER_CALCULATION_CONV2D", DotProductDef);
#else
        if (!_desc.useUniformShaders) {
            findAndReplace(fsCode, "_PLACEHOLDER_ELEMENT_ACCESS_", elementAccess);
            findAndReplace(fsCode, "_PLACEHOLDER_CALC_", calc);
        }
#endif
        InferenceGraph::Pass& pass = passes[i];
        pass.source                = preDefine + rgbaDefine.str() + fsCode + postDefine;

        // std::ostringstream dumpFileStream;
        // dumpFileStream << "/home/us000145/bitbucket/debug_out/shaderout_fs_conv2d_rbga";
        // dumpFileStream << "_" << i << "_" << _desc.numInputPlanes << "_" << _desc.numOutputPlanes << ".glsl";

        // std::ofstream dumpFile;
        // dumpFile.open(dumpFileStream.str(), std::ios_base::out);
        // dumpFile << pass.source << std::endl;
        // dumpFile.close();

        // std::cout << pass.source << std::endl;

        pass.inputs = {{"inputTextures", 0}}; // Input is currently hard coded in GLSL file.
        if (_desc.useUniformShaders) {
            for (int k = 0; k < DIV_4_ROUND_UP(outputChannels); k++) {
                glm::vec4 dummyBias = {this->biases[j + (4 * k)], this->biases[j + (4 * k) + 1], this->biases[j + (4 * k) + 2], this->biases[j + (4 * k) + 3]};
                if ((DIV_4_ROUND_UP(outputChannels) == 1) || k == 0) {
                    pass.uniforms["bias"] = dummyBias;
                } else {
                    pass.uniforms["bias[" + std::to_string(k) + "]"] = dummyBias;
                }
                if (_desc.useBatchNormalization) {
                    glm::vec4 dummyBeta       = {_desc.batchNormalization.at("beta")[j + (4 * k)], _desc.batchNormalization.at("beta")[j + (4 * k) + 1],
                                           _desc.batchNormalization.at("beta")[j + (4 * k) + 2], _desc.batchNormalization.at("beta")[j + (4 * k) + 3]};
                    glm::vec4 dummyGamma      = {_desc.batchNormalization.at("gamma")[j + (4 * k)], _desc.batchNormalization.at("gamma")[j + (4 * k) + 1],
                                            _desc.batchNormalization.at("gamma")[j + (4 * k) + 2], _desc.batchNormalization.at("gamma")[j + (4 * k) + 3]};
                    glm::vec4 dummyMovingMean = {
                        _desc.batchNormalization.at("movingMean")[j + (4 * k)], _desc.batchNormalization.at("movingMean")[j + (4 * k) + 1],
                        _desc.batchNormalization.at("movingMean")[j + (4 * k) + 2], _desc.batchNormalization.at("movingMean")[j + (4 * k) + 3]};
                    glm::vec4 dummyMovingVariance = {
                        _desc.batchNormalization.at("movingVariance")[j + (4 * k)], _desc.batchNormalization.at("movingVariance")[j + (4 * k) + 1],
                        _desc.batchNormalization.at("movingVariance")[j + (4 * k) + 2], _desc.batchNormalization.at("movingVariance")[j + (4 * k) + 3]};
                    if ((DIV_4_ROUND_UP(outputChannels) == 1) || k == 0) {
                        pass.uniforms["beta"]           = dummyBeta;
                        pass.uniforms["gamma"]          = dummyGamma;
                        pass.uniforms["movingMean"]     = dummyMovingMean;
                        pass.uniforms["movingVariance"] = dummyMovingVariance;
                    } else {
                        pass.uniforms["beta[" + std::to_string(k) + "]"]           = dummyBeta;
                        pass.uniforms["gamma[" + std::to_string(k) + "]"]          = dummyGamma;
                        pass.uniforms["movingMean[" + std::to_string(k) + "]"]     = dummyMovingMean;
                        pass.uniforms["movingVariance[" + std::to_string(k) + "]"] = dummyMovingVariance;
                    }
                }
            }

            switch (_desc.weightMode) {
            case snn::WeightAccessMethod::TEXTURES:
                pass.weights = std::vector<const gl::TextureObject*>();
                break;

            case snn::WeightAccessMethod::UNIFORM_BUFFER:
                pass.weights = std::vector<const gl::BufferObject<GL_UNIFORM_BUFFER>*>();
                break;

            case snn::WeightAccessMethod::SSBO_BUFFER:
                pass.weights = std::vector<const gl::BufferObject<GL_SHADER_STORAGE_BUFFER>*>();
                break;

            default:
                break;
            }
            std::visit(match {[&](std::vector<const gl::TextureObject*>& weightTextures) {
                                for (std::size_t k = 0; k < outputChannels; k++) {
                                    // std::cout << "Weights at :" << j + k << " ID: " << this->weightTextures[j+k].id() << " Target: " <<
                                    // this->weightTextures[j+k].target() << std::endl;
                                    weightTextures.push_back(&this->weightTextures[j + k]);
                                }
                            },
                            [&](std::vector<const gl::BufferObject<GL_UNIFORM_BUFFER>*>& weightBuffers) {
                                for (std::size_t k = 0; k < outputChannels; k++) {
                                    weightBuffers.push_back(&this->weightUniformBuffers[j + k]);
                                }
                            },
                            [&](std::vector<const gl::BufferObject<GL_SHADER_STORAGE_BUFFER>*>& weightBuffers) {
                                for (std::size_t k = 0; k < outputChannels; k++) {
                                    weightBuffers.push_back(&this->weightSSBOBuffers[j + k]);
                                }
                            }},
                       pass.weights);
        }
        pass.program = InferenceGraph::Pass::FsProgram {planeCount * i, planeCount};
    }
    return ret;
}

static bool oihw2hwo4i4(std::vector<cv::Mat> inputWeights, std::vector<float>& outVec, int inChannels, int outChannels, int fw, int fh, int unit = 4) {
    int alignedWeightSize = ROUND_UP(outChannels, unit) * fw * fh * ROUND_UP(inChannels, unit);

    SNN_LOGD("Test:%s:%d, %d, %d, %d, %d, all: %d\n", __FUNCTION__, __LINE__, inChannels, outChannels, fw, fh, alignedWeightSize);

    outVec.clear();
    outVec.resize(alignedWeightSize);
    // float* out = (float*) malloc(alignedWeightSize*sizeof(float));
    float* out    = (float*) outVec.data();
    int planeSize = ROUND_UP(outChannels, unit) * ROUND_UP(inChannels, unit);
    memset(out, 0, alignedWeightSize * sizeof(float));
    for (int b = 0; b < outChannels; ++b) {
        int b_4 = b / unit;
        int mx  = b % unit;
        for (int d = 0; d < inChannels; ++d) {
            // int my       = d % unit;
            // int d_4      = d / unit;
            for (int y = 0; y < fh; ++y) {
                for (int x = 0; x < fw; ++x) {
                    int base                                 = (y * fw + x) * planeSize;
                    int inSize                               = ROUND_UP(inChannels, unit) * unit;
                    out[base + inSize * b_4 + d * unit + mx] = inputWeights[b * inChannels + d].at<float>(y * fw + x);
                    // SNN_LOGI("new: %d: %f\n",base + inSize * b_4 + d * unit + mx, inputWeights[b*inChannels+d].at<float>(y*fw+x));
                }
            }
        }
    }
    return 0;
}

#define TEXTURE_WEIGHTS

ShaderLayer::GLSLShaders Conv2DLayer::createCS(const LayerGenOptions& options) const {
    (void) options;

    GLSLShaders ret;
    // auto& desc = getDesc();
    // (void)desc;

    std::vector<InferenceGraph::Pass>& passes = ret.passes;
    passes.resize(1);

    InferenceGraph::Pass& pass = passes[0];

    uint32_t inputWidth  = inputDims[0].width;
    uint32_t inputHeight = inputDims[0].height;
    uint32_t inputDepth  = inputDims[0].depth;

    uint32_t outputWidth  = 0;
    uint32_t outputHeight = 0;
    uint32_t outputDepth  = 0;

    snn::dp::GenericModelLayer::getOutputDims(outputWidth, outputHeight, outputDepth);

    outputDepth = _desc.numOutputPlanes;

    // Conv2D CS
    SNN_LOGD("Test:%s:%d\n", __FUNCTION__, __LINE__);
    string shaderHeader;
    if (_desc.preferHp) {
        shaderHeader = "#version 320 es \n"
                       "#define PRECISION mediump\n"
                       "precision PRECISION float;\n"
                       "layout(std430) buffer;\n"
                       "#define OUTPUT_FORMAT rgba16f\n";
    } else {
        shaderHeader = "#version 320 es \n"
                       "#define PRECISION highp\n"
                       "precision PRECISION float;\n"
                       "layout(std430) buffer;\n"
                       "#define OUTPUT_FORMAT rgba32f\n";
    }
    if (_desc.numInputPlanes <= 4) {
        shaderHeader += "#define INPUT_TEXTURE_2D\n";
    }
    if (_desc.numOutputPlanes <= 4) {
        shaderHeader += "#define OUTPUT_TEXTURE_2D\n";
    }

    if (!_desc.activation.compare("relu")) {
        shaderHeader += "#define RELU\n";
    }

    if (!_desc.activation.compare("relu6")) {
        shaderHeader += "#define RELU6\n";
    }

    if (!_desc.activation.compare("tanh")) {
        shaderHeader += "#define TANH\n";
    }

    if (!_desc.activation.compare("sigmoid")) {
        shaderHeader += "#define SIGMOID\n";
    }

    if (!_desc.activation.compare("leakyRelu")) {
        shaderHeader += ("#define LEAKYRELU_VAL " + std::to_string(_desc.leakyReluAlpha) + "\n");
    }

    if (!_desc.activation.compare("SiLU")) {
        shaderHeader += "#define SILU\n";
    }

    if (!_desc.paddingMode.compare("constant")) {
        shaderHeader += "#define CONSTANT_PADDING\n";
    } else if (!_desc.paddingMode.compare("replicate")) {
        shaderHeader += "#define REPLICATE_PADDING\n";
    } else if (!_desc.paddingMode.compare("reflect")) {
        shaderHeader += "#define REFLECT_PADDING\n";
    }

    if (_desc.useBatchNormalization) {
        shaderHeader += "#define USE_BATCH_NORMALIZATION\n";
    }
    string debugLayer("[0X] Conv2D");
    /*
    if (this->name.find(debugLayer) != std::string::npos) {
        shaderHeader += "#define CONV2D_DEBUG\n";
    }
    */
    SNN_LOGD("Test:%s:%d, %s\n", __FUNCTION__, __LINE__, shaderHeader.c_str());
    string shaderUniforms = "#ifdef OUTPUT_TEXTURE_2D\n"
                            "layout(OUTPUT_FORMAT, binding=3) writeonly uniform PRECISION image2D uOutput;\n"
                            "#else\n"
                            "layout(OUTPUT_FORMAT, binding=3) writeonly uniform PRECISION image2DArray uOutput;\n"
                            "#endif\n"
                            "#ifdef INPUT_TEXTURE_2D\n"
                            "layout(OUTPUT_FORMAT, binding=0) readonly uniform PRECISION image2D uInput;\n"
                            "#else\n"
                            "layout(OUTPUT_FORMAT, binding=0) readonly uniform PRECISION image2DArray uInput;\n"
                            "#endif\n"
#ifdef TEXTURE_WEIGHTS
                            "layout(binding=2) uniform PRECISION sampler2DArray uKernel;\n";
#else
                            "const PRECISION vec4 weightMatrix1[] = vec4[]( \n"
                            "vec4(-0.0035270236, 0.0014674125, 0.0001864655, -0.0001915364),\n"
                            "vec4(-0.0032459043, -0.0016766586, 0.0002894130, 0.0010224803),\n"
                            "vec4(-0.0042301719, -0.0003793148, 0.0001178368, 0.0010762328),\n"
                            "vec4(-0.0026380727, -0.0019003409, -0.0019275651, 0.0005968372)\n"
                            ");\n";
#endif

    string shaderMain;

    if (_desc.kernelSize == 1) {
        shaderMain = loadShader(CONV2D_1X1_CS_ASSET_NAME);
    } else {
        shaderMain = loadShader(CONV2D_CS_ASSET_NAME);
    }

    int kernel    = _desc.kernelSize;
    int stride    = _desc.stride;
    int unit      = 4;
    uint32_t ic_4 = UP_DIV(_desc.numInputPlanes, unit);
    uint32_t oc_4 = UP_DIV(_desc.numOutputPlanes, unit);

    // std::vector<int> mLocalSize{8, 8, 2};

    SNN_LOGD("Test:%s:%d, %d, %d, %d, %d\n", __FUNCTION__, __LINE__, kernel, stride, ic_4, oc_4);

    shaderHeader += ("#define WORK_X " + std::to_string(mLocalSize[0]) + "\n");
    shaderHeader += ("#define WORK_Y " + std::to_string(mLocalSize[1]) + "\n");
    shaderHeader += ("#define WORK_Z " + std::to_string(mLocalSize[2]) + "\n");
    SNN_LOGD("Test:%s:%d, %s\n", __FUNCTION__, __LINE__, shaderHeader.c_str());

    if (kernel == 1) {
        // if (0) {
        pass.uniforms = {{"uUnroll", unit},
                         {"uStride", glm::ivec2(stride, stride)},
                         {"uOutputSize", glm::ivec3(outputWidth, outputHeight, oc_4)},
                         {"uInputSize", glm::ivec3(inputWidth, inputHeight, ic_4)}};
    } else {
        uint32_t paddingOffsets[4];
        this->getPaddingOffset(paddingOffsets);
        SNN_LOGD("%s:%d, Padding: %d, %d, %d, %d\n", __FUNCTION__, __LINE__, paddingOffsets[0], paddingOffsets[1], paddingOffsets[2], paddingOffsets[3]);

        pass.uniforms = {{"uPad", glm::ivec2(paddingOffsets[0], paddingOffsets[2])},
                         {"uKernelSize", glm::ivec2(kernel, kernel)},
                         {"uStride", glm::ivec2(stride, stride)},
                         {"uDilate", glm::ivec2(1, 1)},
                         {"uUnroll", unit},
                         {"uOutputSize", glm::ivec3(outputWidth, outputHeight, oc_4)},
                         {"uInputSize", glm::ivec3(inputWidth, inputHeight, ic_4)}};
    }
    pass.inputs = {{"uInput", 0}};
    pass.source = (shaderHeader + shaderUniforms + shaderMain);
    pass.program =
        InferenceGraph::Pass::CsProgram {"uOutput",
                                         // div-by-N is determined by work group size defined CS program.
                                        {UP_DIV(outputWidth, unit * mLocalSize[0]), UP_DIV(outputHeight, mLocalSize[1]), UP_DIV(oc_4, mLocalSize[2])}};

    SNN_LOGD("%s:%d, input:%d:%d:%d, output:%d:%d:%d\n", __FUNCTION__, __LINE__, inputWidth, inputHeight, inputDepth, outputWidth, outputHeight, outputDepth);

    oihw2hwo4i4(_desc.weights, pass._vecWeights, _desc.numInputPlanes, _desc.numOutputPlanes, kernel, kernel);
    float* kernelBuf = pass._vecWeights.data();

    if (this->name.find(debugLayer) != std::string::npos) {
        for (unsigned int i = 0; i < ROUND_UP(_desc.numOutputPlanes, unit) * kernel * kernel * ROUND_UP(_desc.numInputPlanes, unit); i++) {
            SNN_LOGD("%s:%d: %d, %f\n", __FUNCTION__, __LINE__, i, *(kernelBuf + i));
        }
    }

    this->kernelTexture.bind(0);

    SNN_LOGD("Updating weights for: %s", this->name.c_str());

    int planeSize = ROUND_UP(_desc.numOutputPlanes, unit) * ROUND_UP(_desc.numInputPlanes, unit) * unit;
    for (int i = 0; i < kernel * kernel; i++) {
        SNN_LOGD("%s:%d: %d, %d\n", __FUNCTION__, __LINE__, i, planeSize / unit * i);
        this->kernelTexture.setPixels(i, 0, 0, 0, ic_4 * unit, oc_4, 0, kernelBuf + planeSize / unit * i);
    }
    // if (this->name.find(debugLayer) != std::string::npos) {
    // readTexture(kernel*kernel*ic_4*oc_4*unit, kernelTexture.id(),  ic_4 * unit, oc_4, kernel * kernel, 1);
    // }
    this->kernelTexture.unbind();

    SNN_LOGD("%s:%d, kernel texture %d:%d\n", __FUNCTION__, __LINE__, kernelTexture.id(), kernelTexture.target());

#ifdef TEXTURE_WEIGHTS
    pass.weightUniformTags.resize(1);
    pass.weightUniformTags[0] = "uKernel";
    pass.weights              = std::vector<const gl::TextureObject*>(1, &kernelTexture);
#endif

    // pass._vecWeights.resize(inputWidth*outputWidth);
    // pass._boWeights.reset(new gl::BufferObject<GL_SHADER_STORAGE_BUFFER>());
    // pass._boWeights->allocate(inputWidth*outputWidth*4, pass._vecWeights.data());
    // float *destWeight = (float *)pass._boWeights->map(GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);

    // unsigned int width = _denseDesc.weights[0].size();
    // int k = 0;
    // for (size_t i = 0; i < _denseDesc.weights.size(); i++){
    //     for (size_t j = 0; j < width; j++)  {
    //         //SNN_LOGD("%zu:%zu: %zu, %f\n",i, j, i * inputWidth + j, _denseDesc.weights[i][j]);
    //         *(destWeight + k) =  _denseDesc.weights[i][j];
    //         k++;
    //     }
    // }
    // pass._boWeights->unmap();

    pass._vecBias.resize(_desc.numOutputPlanes);
    pass._boBias.reset(new gl::BufferObject<GL_SHADER_STORAGE_BUFFER>());
    pass._boBias->allocate(_desc.numOutputPlanes, pass._vecBias.data());
    float* destBias = (float*) pass._boBias->map(GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);

    SNN_LOGD("Bias:%s:%d \n", __FUNCTION__, __LINE__);

    for (size_t i = 0; i < _desc.biases.size(); i++) {
        *(destBias + i) = (float) _desc.biases[i];
        // SNN_LOGD("%zu:%f\n",i, _desc.biases[i]);
    }

    pass._boBias->unmap();
    pass.ssboMap = {{4, pass._boBias->getId()}};

    if (_desc.useBatchNormalization) {
        float* bnDataDest;
        std::vector<float> bnDataVector;
        std::string bnString;
        pass._vecBeta.resize(_desc.numOutputPlanes);
        pass._bnBeta.reset(new gl::BufferObject<GL_SHADER_STORAGE_BUFFER>());
        pass._bnBeta->allocate(_desc.numOutputPlanes, pass._vecBeta.data());
        bnDataDest   = (float*) pass._bnBeta->map(GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        bnString     = "beta";
        bnDataVector = _desc.batchNormalization.at(bnString);
        SNN_LOGD("Beta:%s:%d \n", __FUNCTION__, __LINE__);
        /*
        for (size_t i = 0; i < bnDataVector.size(); i++) {
           *(bnDataDest + i) = bnDataVector[i];
           // SNN_LOGD("%zu:%f\n",i, bnDataVector[i]);
        }
        */
        memcpy(bnDataDest, bnDataVector.data(), _desc.numOutputPlanes * 4);
        pass._bnBeta->unmap();
        pass.ssboMap[5] = pass._bnBeta->getId();

        pass._vecGamma.resize(_desc.numOutputPlanes);
        pass._bnGamma.reset(new gl::BufferObject<GL_SHADER_STORAGE_BUFFER>());
        pass._bnGamma->allocate(_desc.numOutputPlanes, pass._vecGamma.data());
        bnDataDest   = (float*) pass._bnGamma->map(GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        bnString     = "gamma";
        bnDataVector = _desc.batchNormalization.at(bnString);
        SNN_LOGD("Gamma:%s:%d \n", __FUNCTION__, __LINE__);
        /*
        for (size_t i = 0; i < bnDataVector.size(); i++) {
            *(bnDataDest + i) = bnDataVector[i];
            // SNN_LOGD("%zu:%f\n",i, bnDataVector[i]);
        }
        */
        memcpy(bnDataDest, bnDataVector.data(), _desc.numOutputPlanes * 4);
        pass._bnGamma->unmap();
        pass.ssboMap[6] = pass._bnGamma->getId();

        pass._vecMean.resize(_desc.numOutputPlanes);
        pass._bnMean.reset(new gl::BufferObject<GL_SHADER_STORAGE_BUFFER>());
        pass._bnMean->allocate(_desc.numOutputPlanes, pass._vecMean.data());
        bnDataDest   = (float*) pass._bnMean->map(GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        bnString     = "movingMean";
        bnDataVector = _desc.batchNormalization.at(bnString);
        SNN_LOGD("Mean:%s:%d \n", __FUNCTION__, __LINE__);
        /*
        for (size_t i = 0; i < bnDataVector.size(); i++) {
            *(bnDataDest + i) = bnDataVector[i];
            // SNN_LOGD("%zu:%f\n",i, bnDataVector[i]);
        }
        */
        memcpy(bnDataDest, bnDataVector.data(), _desc.numOutputPlanes * 4);
        pass._bnMean->unmap();
        pass.ssboMap[7] = pass._bnMean->getId();

        pass._vecVariance.resize(_desc.numOutputPlanes);
        pass._bnVariance.reset(new gl::BufferObject<GL_SHADER_STORAGE_BUFFER>());
        pass._bnVariance->allocate(_desc.numOutputPlanes, pass._vecVariance.data());
        bnDataDest = (float*) pass._bnVariance->map(GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        bnString   = "movingVariance";
        SNN_LOGD("Variance:%s:%d \n", __FUNCTION__, __LINE__);
        bnDataVector = _desc.batchNormalization.at(bnString);
        /*
        for (size_t i = 0; i < bnDataVector.size(); i++) {
            *(bnDataDest + i) = bnDataVector[i];
            // SNN_LOGD("%zu:%f\n",i, bnDataVector[i]);
        }
        */
        memcpy(bnDataDest, bnDataVector.data(), _desc.numOutputPlanes * 4);
        pass._bnVariance->unmap();
        pass.ssboMap[8] = pass._bnVariance->getId();
    }

    return ret;
}

InferenceGraph::Transform Conv2DLayer::getOutputScaleDimAdjustment() const {
    uint32_t offset[4];
    this->getPaddingOffset(offset);
    float scale       = 1 / static_cast<float>(_desc.stride);
    float translation = 0.0f;
    if (_desc.kernelSize % 2 != 0) {
        translation = 1 + (static_cast<float>(offset[0] + offset[1]) - static_cast<float>(_desc.kernelSize)) / static_cast<float>(_desc.stride);
    } else {
        translation = 1 + (static_cast<float>(offset[0] + offset[1] - 1) - static_cast<float>(_desc.kernelSize)) / static_cast<float>(_desc.stride);
    }
    return {0, scale, scale, translation, translation};
}
