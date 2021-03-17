/*
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
*/
#include "oneflow/core/framework/framework.h"
#include "oneflow/user/kernels/cumsum_kernel_util.h"

namespace oneflow {

namespace user_op {

template<typename IN_T>
struct CumsumFunctor<DeviceType::kCPU, IN_T> final {
  void operator()(DeviceCtx* ctx, int32_t instance_num, int32_t instance_size, int32_t post,
                  const bool exclusive, const bool reverse, const IN_T* input, IN_T* output) {
    DoCumsum<IN_T>(instance_num, instance_size, post, exclusive, reverse, input, output);
  }
};

OF_PP_SEQ_PRODUCT_FOR_EACH_TUPLE(INSTANTIATE_CUMSUM_FUNCTOR, (DeviceType::kCPU),
                                 CUMSUM_DATA_TYPE_CPU_SEQ);

}  // namespace user_op
}  // namespace oneflow
