//==============================================================================
//
//  @@-COPYRIGHT-START-@@
//
//  Copyright (c) 2021, Qualcomm Innovation Center, Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:
//
//  1. Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//  2. Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//  3. Neither the name of the copyright holder nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
//  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
//  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
//  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
//  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
//  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
//  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
//  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//  POSSIBILITY OF SUCH DAMAGE.
//
//  SPDX-License-Identifier: BSD-3-Clause
//
//  @@-COPYRIGHT-END-@@
//
//==============================================================================

#include "QcQuantizePerChannelParamOp.hpp"
#include "AimetOpUtils.h"
#include <iostream>
#include <type_traits>
#include <algorithm>
#include <cstdint>
#include <math.h>
#include <stdexcept>
#include <stdlib.h>
#include <thread>
#include <vector>

#define EIGEN_USE_THREADS
using namespace tensorflow;
using namespace std;
using namespace gtl;

REGISTER_OP("QcQuantizePerChannelParam")
    .Input("in_tensor: T")     // list of input tensors (weights/activations)
    .Input("op_mode: int32")   //{'ANALYSIS', 'ACTIVE', 'PASSTHROUGH'}")
    .Input("tensor_quantizer_reference: int64")
    .Input("encoding_min: double")
    .Input("encoding_max: double")
    .Input("bit_width: int8")
    .Input("use_symmetric_encoding: bool")
    .Input("axis: int32")
    .Output("out_tensor: T")   // list of output tensors (weights/activations)

    .Attr("T: {float} = DT_FLOAT")   // attr 'T' specifies which template instantiation of op to use, default float
    .Doc(R"doc(QcQuantize Per Channel for Param custom op.)doc")
    .SetShapeFn([](::tensorflow::shape_inference::InferenceContext* c) {
        c->set_output(0, c->input(0));
        return Status::OK();
    });




template <typename D, typename T>
void modeSpecificAction(const D& d, const T* inTensor, size_t count, T* outTensor,
                        const uint64* tensorQuantizerRef, const int32* opMode,
                        const double* min, const double* max, const int8* bw,
                        const bool* useSymEncoding)
{
    bool useCuda = false;
    if (std::is_same<D, GPUDevice>::value)
    {
        useCuda = true;
    }

    // Note that all of the pointers to data here could either be pointing to CPU memory or GPU memory
    // We first copy everything to CPU memory and then use them
    auto tensorQuantizerRefHost = copyLiteralToHost<uint64>(d, tensorQuantizerRef);
    auto opModeHost = copyLiteralToHost<int32>(d, opMode);
    auto encodingMin = copyLiteralToHost<double>(d, min);
    auto encodingMax = copyLiteralToHost<double>(d, max);
    auto tensorQuantizer = reinterpret_cast<DlQuantization::TensorQuantizerOpFacade*>(tensorQuantizerRefHost);
    auto opModeEnum = static_cast<const DlQuantization::TensorQuantizerOpMode>(opModeHost);
    auto bitwidth = copyLiteralToHost<int8>(d, bw);
    auto useSymmetricEncoding = copyLiteralToHost<bool>(d, useSymEncoding);

    switch (opModeEnum)
    {
    case DlQuantization::TensorQuantizerOpMode::oneShotQuantizeDequantize:
    {

        tensorQuantizer->updateStats(inTensor, count, useCuda);

        DlQuantization::TfEncoding initial_encoding = tensorQuantizer->computeEncoding(bitwidth, useSymmetricEncoding);
        tensorQuantizer->quantizeDequantize(inTensor, count, outTensor, initial_encoding.min, initial_encoding.max,
                                            bitwidth, useCuda);

        break;
    }

    case DlQuantization::TensorQuantizerOpMode::updateStats:
    {
        tensorQuantizer->updateStats(inTensor, count, useCuda);
        copyInputTensorsToOutputTensors(d, inTensor, count, outTensor);
        break;
    }
    case DlQuantization::TensorQuantizerOpMode::quantizeDequantize:
    {
        tensorQuantizer->quantizeDequantize(inTensor, count, outTensor, encodingMin, encodingMax, bitwidth, useCuda);
        break;
    }
    case DlQuantization::TensorQuantizerOpMode::passThrough:
    {
        copyInputTensorsToOutputTensors(d, inTensor, count, outTensor);
        break;
    }
    default:
    {
        assert(0);
    }
    }

}

// OpKernel definition.
// 'Device is templated on the type of device.
// template parameter <T> is the datatype of the tensors.
template <typename Device, typename T>
class QcQuantizePerChannelParamOp : public OpKernel
{
public:
    explicit QcQuantizePerChannelParamOp(OpKernelConstruction* context) : OpKernel(context)
    {
    }

    void Compute(OpKernelContext* context) override
    {
        // Consume input tensor
        const Tensor& inTensor = context->input(0);
        // Get shape of input tensor by iterating over each dimension
        int numDimensionsTensor = inTensor.shape().dims();
        std::cout<<" \n dimersions ";
        std::vector<int> shapeVector;
        for(int axis=0; axis< numDimensionsTensor; axis++)
        {
            shapeVector.push_back(inTensor.shape().dim_size(axis));
            std::cout<<inTensor.shape().dim_size(axis)<<" ";
        }

        // Read axis for per channel quantization
        const Tensor* axisTensor;
        OP_REQUIRES_OK(context, context->input("axis", &axisTensor));
        const int32* axis = axisTensor->flat<int32>().data();

        // Get shape along axis for per channel quantization
        int channelShape = shapeVector[*axis];

        // Read the op_mode
        const Tensor* opModeTensor;
        OP_REQUIRES_OK(context, context->input("op_mode", &opModeTensor));
        const int32* opMode = opModeTensor->flat<int32>().data();

        // Read the tensor quantizer ref
        const Tensor* quantizerRefTensor;
        OP_REQUIRES_OK(context, context->input("tensor_quantizer_reference", &quantizerRefTensor));
        uint64* quantizerAddr = (uint64*) quantizerRefTensor->flat<int64>().data();

        // Read the encoding_min
        const Tensor* encodingMinTensor;
        OP_REQUIRES_OK(context, context->input("encoding_min", &encodingMinTensor));
        const double* encodingMin = encodingMinTensor->flat<double>().data();

        // Read the encoding_max
        const Tensor* encodingMaxTensor;
        OP_REQUIRES_OK(context, context->input("encoding_max", &encodingMaxTensor));
        const double* encodingMax = encodingMaxTensor->flat<double>().data();

        // read bitwidth
        const Tensor* bitwidthTensor;
        OP_REQUIRES_OK(context, context->input("bit_width", &bitwidthTensor));
        const int8* bitwidth = bitwidthTensor->flat<int8>().data();

        // use symmetric encoding
        const Tensor* useSymmetricEncodingTensor;
        OP_REQUIRES_OK(context, context->input("use_symmetric_encoding", &useSymmetricEncodingTensor));
        auto useSymmetricEncoding = useSymmetricEncodingTensor->flat<bool>().data();

        // allocate output tensors
        Tensor* outTensor = nullptr;
        OP_REQUIRES_OK(context, context->allocate_output(0, inTensor.shape(), &outTensor));

        // Performs per layer quantization
        // For parameters in convolution layers or linear layers
        // TODO: Add depthwise layer and transposed conv2d
        if(numDimensionsTensor == 4 or numDimensionsTensor == 2)
        {
            // K x K x I x O -> N x O
            auto inTensorTwoDim = inTensor.flat_inner_dims<float, 2>();
            auto outTensorTwoDim = outTensor->flat_inner_dims<float, 2>();
            // For linear layers
            int numElements = shapeVector[0];
            // For conv layers
            if (numDimensionsTensor == 4)
                numElements = numElements * shapeVector[1] * shapeVector[2];

            for(int8 channel=0; channel < channelShape; channel++)
            {
                 Tensor temp1, temp2;
                 OP_REQUIRES_OK(context, context->allocate_temp(DT_FLOAT, TensorShape({numElements, 2}), &temp1));
                 OP_REQUIRES_OK(context, context->allocate_temp(DT_FLOAT, TensorShape({numElements, 2}), &temp2));
                 temp1.tensor<float, 2>().chip<0>(0) = inTensorTwoDim.chip<1>(channel);
                 auto inpData = temp1.flat<float>().data();
                 auto outData = temp2.flat<float>().data();

                 modeSpecificAction(context->eigen_device<Device>(), inpData, numElements,
                                    outData, quantizerAddr++, opMode, encodingMin++,
                                    encodingMax++, bitwidth, useSymmetricEncoding);
                 outTensorTwoDim.chip<1>(channel) = temp2.tensor<float, 2>().chip<0>(0);
            }
        }
        else if(numDimensionsTensor == 1)
        {
            // Per channel quantization for Bias
            int numElements = 1;
            auto inTensorFlat = inTensor.flat<T>().data();
            auto outTensorFlat = outTensor->flat<T>().data();
            for(int8 channel=0; channel < channelShape; channel++)
            {
                 modeSpecificAction(context->eigen_device<Device>(), inTensorFlat++, numElements,
                                    outTensorFlat++, quantizerAddr++, opMode, encodingMin++,
                                    encodingMax++, bitwidth, useSymmetricEncoding);
            }
        }


        }
};


#define REGISTER_CPU(T) \
    REGISTER_KERNEL_BUILDER(Name("QcQuantizePerChannelParam").Device(DEVICE_CPU).TypeConstraint<T>("T"), QcQuantizePerChannelParamOp<CPUDevice, T>);

REGISTER_CPU(float);

// Register the GPU kernels.

#ifdef GOOGLE_CUDA
#define REGISTER_GPU(T) \
    REGISTER_KERNEL_BUILDER(Name("QcQuantizePerChannelParam").Device(DEVICE_GPU).TypeConstraint<T>("T"), QcQuantizePerChannelParamOp<GPUDevice, T>);
REGISTER_GPU(float);

#endif   // GOOGLE_CUDA