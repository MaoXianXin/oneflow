"""
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
"""
import unittest
from collections import OrderedDict

import numpy as np

import oneflow.experimental as flow
from test_util import GenArgList


def _test_bernoulli(test_case, shape):
    input_arr = np.ones(shape)
    x = flow.Tensor(input_arr, device=flow.device("cpu"))
    y = flow.bernoulli(x)
    test_case.assertTrue(np.allclose(y.numpy(), x.numpy()))


def _test_bernoulli_with_generator(test_case, shape):
    generator = flow.Generator()
    generator.manual_seed(0)
    x = flow.Tensor(np.random.rand(*shape), device=flow.device("cpu"))
    y_1 = flow.bernoulli(x, generator=generator)
    y_1.numpy()  # sync
    generator.manual_seed(0)
    y_2 = flow.bernoulli(x, generator=generator)
    test_case.assertTrue(np.allclose(y_1.numpy(), y_2.numpy()))


@unittest.skipIf(
    not flow.unittest.env.eager_execution_enabled(),
    ".numpy() doesn't work in lazy mode",
)
class TestBernoulli(flow.unittest.TestCase):
    def test_bernoulli(test_case):
        arg_dict = OrderedDict()
        arg_dict["test_functions"] = [
            _test_bernoulli,
        ]
        arg_dict["shape"] = [(2, 3), (2, 3, 4), (2, 3, 4, 5)]
        for arg in GenArgList(arg_dict):
            arg[0](test_case, *arg[1:])


if __name__ == "__main__":
    unittest.main()
