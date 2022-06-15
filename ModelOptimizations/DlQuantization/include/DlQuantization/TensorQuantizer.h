//==============================================================================
//
//  @@-COPYRIGHT-START-@@
//
//  Copyright (c) 2020 - 2022, Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef AIMET_TENSOR_QUANTIZER_H
#define AIMET_TENSOR_QUANTIZER_H

#include <memory>

#include <DlQuantization/IQuantizationEncodingAnalyzer.hpp>
#include <DlQuantization/ITensorQuantizationSim.h>
#include <DlQuantization/Quantization.hpp>
#include <DlQuantization/QuantizerFactory.hpp>
#include "DlQuantization/TensorQuantizerOpFacade.h"


namespace DlQuantization
{
/**
 * This class models a quantizer for a tensor. An example is a quantizer for a param or a output tensor from a layer
 * Encapsulates computing an encoding for this tensor, as well as quantizing-dequantizing tensor values
 */
class TensorQuantizer : public TensorQuantizerOpFacade
{
public:

    /**
    * Constructor
    * @param quantScheme Quantization scheme (e.g. TF-Enhanced)
    * @param roundingMode Rounding mode to use during quantization
    */
    TensorQuantizer(QuantizationMode quantScheme, RoundingMode roundingMode);

    /**
     * Reset stats being collected to compute encoding
     */
    void resetEncodingStats();

    /**
     * Update stats being collected to compute encoding
     * @param tensor Tensor to update the stats with
     * @param tensorSize Size of the tensor (number of tensor elements)
     * @param useCuda If true, the tensor is assumed to be in CUDA memory
     */
    void updateStats(const float* tensor, std::size_t tensorSize, bool useCuda) override;

    /**
     * Compute the encoding for this tensor using stats collected so far
     * @param bitwidth to be used
     * @param flag to indicate symmetric/asymmetric encoding is to be used
     */
    TfEncoding computeEncoding(unsigned int bitwidth, bool useSymmetricEncoding) override;

    /**
     * @brief Calculate an encoding entirely from the data provided
     *
     * Don't remember anything (forget the statistical data and forget the
     * encoding).
     */
    void computeEncodingFromData(uint8_t bw, const float* data, size_t count, TfEncoding& encoding,
                                 ComputationMode cpuGpuMode, bool useSymmetricEncodings,
                                 bool useUnsignedSymmetric, bool useStrictSymmetric);

    /**
     * Convert a tensor from float to quantized int and back to float
     * @param input Input tensor
     * @param tensorSize Size of the input tensor (number of tensor elements)
     * @param output Output tensor
     * @param encodingMin minimum value of encoding range
     * @param encodingMax maximum value of encoding range
     * @param bitwidth to be used
     * @param useCuda If true, both the input and output tensors are assumed to be in CUDA memory
     */
    void quantizeDequantize(const float* input, std::size_t tensorSize, float* output,
                            double encodingMin, double encodingMax, unsigned int bitwidth, bool useCuda) override;

    /**
    * sets quantScheme and creates new encoding analyzer instance
    * @param quantScheme Quantization scheme (e.g. TF-Enhanced)
    */
    void setQuantScheme(QuantizationMode quantScheme);

    /**
    * gets quantScheme configured for this Tensor Quantizer
    * @return quantScheme as QuantizationMode
    */
    QuantizationMode getQuantScheme();

    /**
    * gets strict symmetric flag configured for this Tensor Quantizer
    * @return quantScheme as QuantizationMode
    */
    bool getStrictSymmetric();

   /**
   * sets strict symmetric flag
   * @param bool, True if strict symmetric, False otherwise
   */
    void setStrictSymmetric(bool useStrictSymmetric);

    /**
    * gets unsigned symmetric flag config for this Tensor Quantizer
    * @return bool, True if unsigned symmetric mode, False otherwise
    */
    bool getUnsignedSymmetric();

   /**
   * sets unsigned symmetric flag
   * @param bool, True or False
   */
    void setUnsignedSymmetric(bool useUnsignedsymmetric);

    /**
   * Returns a histogram that represents a PDF of tensor values seen by this encoding analyzer so far
   * @return Histogram of statistics. The histogram returned is a vector of buckets. Each bucket is a tuple of
   * two values - the float value representing the left edge of the bucket and a PDF of the values in this bucket
   * relative to all the values seen across all buckets
   */
    std::vector<std::tuple<double, double>> getStatsHistogram();

    RoundingMode roundingMode;      ///< Rounding mode to use during quantization
    bool isEncodingValid;           ///< Is encoding valid

private:
    QuantizationMode _quantScheme;   ///< Quantization scheme (e.g TF-Enhanced)
    bool _useStrictSymmetric;
    bool _useUnsignedSymmetric;
    bool _validStats;
    std::unique_ptr<IQuantizationEncodingAnalyzer<float>> _encodingAnalyzer;
    std::unique_ptr<ITensorQuantizationSim<float>> _tensorQuantizationSim;
};


}   // namespace DlQuantization

#endif   // AIMET_TENSOR_QUANTIZER_H
