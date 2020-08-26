# /usr/bin/env python3.5
# -*- mode: python -*-
# =============================================================================
#  @@-COPYRIGHT-START-@@
#  
#  Copyright (c) 2019, Qualcomm Innovation Center, Inc. All rights reserved.
#  
#  Redistribution and use in source and binary forms, with or without 
#  modification, are permitted provided that the following conditions are met:
#  
#  1. Redistributions of source code must retain the above copyright notice, 
#     this list of conditions and the following disclaimer.
#  
#  2. Redistributions in binary form must reproduce the above copyright notice, 
#     this list of conditions and the following disclaimer in the documentation 
#     and/or other materials provided with the distribution.
#  
#  3. Neither the name of the copyright holder nor the names of its contributors 
#     may be used to endorse or promote products derived from this software 
#     without specific prior written permission.
#  
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
#  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
#  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
#  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
#  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
#  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
#  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
#  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  POSSIBILITY OF SUCH DAMAGE.
#  
#  SPDX-License-Identifier: BSD-3-Clause
#  
#  @@-COPYRIGHT-END-@@
# =============================================================================

import copy
import unittest
import numpy as np
import torch
import torch.nn as nn

from aimet_common.defs import QuantScheme
import aimet_torch.bias_correction
import aimet_torch.layer_selector
from aimet_torch import bias_correction
from aimet_torch.quantsim import QuantParams
from aimet_torch.examples.mobilenet import MobileNetV2
from aimet_torch import batch_norm_fold
from aimet_torch.examples.imagenet_dataloader import ImageNetDataLoader


def evaluate(model, early_stopping_iterations, use_cuda):
    """
    :param model: model to be evaluated
    :param early_stopping_iterations: if None, data loader will iterate over entire validation data
    :return: dummy ouput
    """
    random_input = torch.rand(1, 3, 224, 224)

    return model(random_input)


class TestBiasCorrection(unittest.TestCase):

    def test_bias_correction_empirical(self):

        torch.manual_seed(10)
        model = MobileNetV2().to(torch.device('cpu'))
        model.eval()
        batch_norm_fold.fold_all_batch_norms(model, (1, 3, 224, 224))
        model_copy = copy.deepcopy(model)
        model.eval()
        model_copy.eval()

        image_dir = './data/tiny-imagenet-200'
        image_size = 224
        batch_size = 1
        num_workers = 1

        data_loader = ImageNetDataLoader(image_dir, image_size, batch_size, num_workers)
        params = QuantParams(weight_bw=4, act_bw=4, round_mode="nearest",
                             quant_scheme=QuantScheme.post_training_tf)
        bias_correction.correct_bias(model.to(device="cuda"), params, 1, data_loader.train_loader, 1, layers_to_ignore=[model.features[0][0]])

        self.assertTrue(np.allclose(model.features[0][0].bias.detach().cpu().numpy(),
                                    model_copy.features[0][0].bias.detach().cpu().numpy()))

        self.assertFalse(np.allclose(model.features[1].conv[0].bias.detach().cpu().numpy(),
                                     model_copy.features[1].conv[0].bias.detach().cpu().numpy()))

        # To check if wrappers got removed
        self.assertTrue(isinstance(model.features[11].conv[0], nn.Conv2d))

    def test_bias_correction_hybrid(self):

        torch.manual_seed(10)

        model = MobileNetV2().to(torch.device('cpu'))
        model.eval()
        module_prop_list = aimet_torch.bias_correction.find_all_conv_bn_with_activation(model,
                                                                                        input_shape=(1, 3, 224, 224))
        batch_norm_fold.fold_all_batch_norms(model, (1, 3, 224, 224))
        model_copy = copy.deepcopy(model)
        model.eval()
        model_copy.eval()

        image_dir = './data/tiny-imagenet-200'
        image_size = 224
        batch_size = 1
        num_workers = 1

        data_loader = ImageNetDataLoader(image_dir, image_size, batch_size, num_workers)
        params = QuantParams(weight_bw=4, act_bw=4, round_mode="nearest",
                             quant_scheme=QuantScheme.post_training_tf
                            )

        bias_correction.correct_bias(model.to(device="cuda"), params, 1, data_loader.train_loader, 1,
                                     module_prop_list,
                                     False)

        self.assertTrue(np.allclose(model.features[0][0].bias.detach().cpu().numpy(),
                                    model_copy.features[0][0].bias.detach().cpu().numpy()))

        self.assertTrue(np.allclose(model.features[1].conv[0].bias.detach().cpu().numpy(),
                                     model_copy.features[1].conv[0].bias.detach().cpu().numpy()))

        # To check if wrappers got removed
        self.assertTrue(isinstance(model.features[11].conv[0], nn.Conv2d))

