# Copyright (C) 2020 - 2022 OPPO. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
from config.formatConfig.supportedFormats import SUPPORTED_FORMATS
from config.frameworkConfig.supportedFrameworks import SUPPORTED_COMPATIBLE_FRAMEWORKS
from convertProcessor.processorConfig import processConfig
from convertProcessor.processorConfig.processConfig import COMPATIBLE_FRAMEWORK
from layers.supportedLayers import layerHelper
from layers.supportedLayers.layer import Layer


class Lambda(Layer):
    layerInfo = dict()

    def handleLayer(self, **kwargs):
        layername = kwargs['layername']
        layerinfo = kwargs['layerinfo']
        # print(layerinfo)
        self.layerInfo = layerinfo

    def getConvertedJSONForLayer(self):
        if COMPATIBLE_FRAMEWORK == SUPPORTED_COMPATIBLE_FRAMEWORKS.SNN:
            if processConfig.getCurrentFormat() == SUPPORTED_FORMATS.H5ToJson:
                self.getSNNCompatibleJSONFromH5()

    def getSNNCompatibleJSONFromH5(self):
        layerJSON = dict()
        layername = self.layerInfo['config']['name']
        layerJSON['name'] = layername
        layerJSON['type'] = self.layerInfo['class_name']
        layerJSON['function'] = self.layerInfo['config']['function']
        layerJSON['function_type'] = self.layerInfo['config']['function_type']
        layerJSON['module'] = self.layerInfo['config']['module']
        layerJSON['output_shape'] = self.layerInfo['config']['output_shape']
        layerJSON['output_shape_type'] = self.layerInfo['config']['output_shape_type']
        layerJSON['output_shape_module'] = self.layerInfo['config']['output_shape_module']
        self.addInbounds(layerJSON)
        layerHelper.layersToJSON[layername] = layerJSON
        # print(layerJSON)

    def addInbounds(self, layerJSON):
        layerJSON['inbounds'] = []
        if processConfig.getCurrentFormat() == SUPPORTED_FORMATS.H5ToJson:
            currentInbounds = self.layerInfo['inbound_nodes']
            for inboundList in currentInbounds:
                for inbound in inboundList:
                    inboundLayername = inbound[0]
                    layerJSON['inbounds'].append(inboundLayername)

            # print(layerJSON['inbounds'])

