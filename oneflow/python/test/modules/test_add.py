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


def _test_add_forward(test_case, shape, device):
    x = flow.Tensor(np.random.randn(*shape), device=flow.device(device))
    y = flow.Tensor(np.random.randn(*shape), device=flow.device(device))
    of_out = flow.add(x, y)
    np_out = np.add(x.numpy(), y.numpy())
    test_case.assertTrue(np.allclose(of_out.numpy(), np_out, 1e-4, 1e-4))

    x = 5
    y = flow.Tensor(np.random.randn(*shape), device=flow.device(device))
    of_out = flow.add(x, y)
    np_out = np.add(x, y.numpy())
    test_case.assertTrue(np.allclose(of_out.numpy(), np_out, 1e-4, 1e-4))

    x = flow.Tensor(np.random.randn(*shape), device=flow.device(device))
    y = 5
    of_out = flow.add(x, y)
    np_out = np.add(x.numpy(), y)
    test_case.assertTrue(np.allclose(of_out.numpy(), np_out, 1e-4, 1e-4))

    x = flow.Tensor(np.random.randn(*shape), device=flow.device(device))
    y = flow.Tensor(np.array([5.0]), device=flow.device(device))
    of_out = flow.add(x, y)
    np_out = np.add(x.numpy(), y.numpy())
    test_case.assertTrue(np.allclose(of_out.numpy(), np_out, 1e-4, 1e-4))

    x = flow.Tensor(np.random.randn(1, 1), device=flow.device(device))
    y = flow.Tensor(np.random.randn(*shape), device=flow.device(device))
    of_out = flow.add(x, y)
    np_out = np.add(x.numpy(), y.numpy())
    test_case.assertTrue(np.allclose(of_out.numpy(), np_out, 1e-4, 1e-4))


def _test_add_backward(test_case, shape, device):
    x = 5
    y = flow.Tensor(
        np.random.randn(*shape), requires_grad=True, device=flow.device(device)
    )
    of_out = flow.add(x, y).sum()
    of_out.backward()
    test_case.assertTrue(np.allclose(y.grad.numpy(), np.ones(shape=shape), 1e-4, 1e-4))


def _test_inplace_add(test_case, shape, device):
    np_x = np.random.randn(*shape)

    # scalar add
    of_x = flow.Tensor(
        np_x, dtype=flow.float32, device=flow.device(device), requires_grad=True
    )
    of_x_inplace = of_x + 1
    id_old = id(of_x_inplace)
    of_x_inplace.add_(5)
    test_case.assertEqual(id_old, id(of_x_inplace))
    np_out = np_x + 1 + 5
    test_case.assertTrue(np.allclose(of_x_inplace.numpy(), np_out, 1e-5, 1e-5))

    of_x_inplace = of_x_inplace.sum()
    of_x_inplace.backward()
    test_case.assertTrue(np.allclose(of_x.grad.numpy(), np.ones(shape), 1e-5, 1e-5))

    # element add
    of_x = flow.Tensor(
        np_x, dtype=flow.float32, device=flow.device(device), requires_grad=True
    )
    of_y = flow.Tensor(
        np.random.randn(*shape), device=flow.device(device), requires_grad=False
    )
    of_x_inplace = of_x + 1
    id_old = id(of_x_inplace)
    of_x_inplace.add_(of_y)
    test_case.assertEqual(id_old, id(of_x_inplace))
    np_out = np_x + 1 + of_y.numpy()
    test_case.assertTrue(np.allclose(of_x_inplace.numpy(), np_out, 1e-5, 1e-5))

    of_x_inplace = of_x_inplace.sum()
    of_x_inplace.backward()
    test_case.assertTrue(np.allclose(of_x.grad.numpy(), np.ones(shape), 1e-5, 1e-5))

    # tensor += other
    of_x = flow.Tensor(
        np_x, dtype=flow.float32, device=flow.device(device), requires_grad=True
    )
    of_y = flow.Tensor(
        np.random.randn(*shape), device=flow.device(device), requires_grad=False
    )
    of_x_inplace = of_x + 1
    id_old = id(of_x_inplace)
    of_x_inplace += of_y
    test_case.assertEqual(id_old, id(of_x_inplace))
    np_out = np_x + 1 + of_y.numpy()
    test_case.assertTrue(np.allclose(of_x_inplace.numpy(), np_out, 1e-5, 1e-5))

    of_x_inplace = of_x_inplace.sum()
    of_x_inplace.backward()
    test_case.assertTrue(np.allclose(of_x.grad.numpy(), np.ones(shape), 1e-5, 1e-5))

    # add_scalar_by_tensor
    of_x = flow.Tensor(
        np_x, dtype=flow.float32, device=flow.device(device), requires_grad=True
    )
    of_y = flow.Tensor(np.array([5.0]), device=flow.device(device), requires_grad=False)
    of_x_inplace = of_x + 1
    id_old = id(of_x_inplace)
    of_x_inplace.add_(of_y)
    test_case.assertEqual(id_old, id(of_x_inplace))
    np_out = np_x + 6
    test_case.assertTrue(np.allclose(of_x_inplace.numpy(), np_out, 1e-5, 1e-5))

    of_x_inplace = of_x_inplace.sum()
    of_x_inplace.backward()
    test_case.assertTrue(np.allclose(of_x.grad.numpy(), np.ones(shape), 1e-5, 1e-5))

    # broadcast_add
    of_x = flow.Tensor(
        np_x, dtype=flow.float32, device=flow.device(device), requires_grad=True
    )
    np_y = np.random.randn(*shape[:-1], 1)
    of_y = flow.Tensor(np_y, device=flow.device(device), requires_grad=False)
    of_x_inplace = of_x + 1
    id_old = id(of_x_inplace)
    of_x_inplace.add_(of_y)
    test_case.assertEqual(id_old, id(of_x_inplace))
    np_out = np_x + 1 + np_y
    test_case.assertTrue(np.allclose(of_x_inplace.numpy(), np_out, 1e-5, 1e-5))

    of_x_inplace = of_x_inplace.sum()
    of_x_inplace.backward()
    test_case.assertTrue(np.allclose(of_x.grad.numpy(), np.ones(shape), 1e-5, 1e-5))


@unittest.skipIf(
    not flow.unittest.env.eager_execution_enabled(),
    ".numpy() doesn't work in lazy mode",
)
class TestAddModule(flow.unittest.TestCase):
    def test_add(test_case):
        arg_dict = OrderedDict()
        arg_dict["test_fun"] = [
            _test_add_forward,
            _test_add_backward,
            _test_inplace_add,
        ]
        arg_dict["shape"] = [(2, 3), (2, 3, 4), (2, 3, 4, 5)]
        arg_dict["device"] = ["cpu", "cuda"]
        for arg in GenArgList(arg_dict):
            arg[0](test_case, *arg[1:])


if __name__ == "__main__":
    unittest.main()
