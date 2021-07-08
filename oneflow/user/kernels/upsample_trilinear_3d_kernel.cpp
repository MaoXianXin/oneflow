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
#include "oneflow/core/kernel/new_kernel_util.h"
#include "oneflow/core/common/nd_index_offset_helper.h"
#include "oneflow/user/kernels/upsample_kernel.h"

namespace oneflow {

namespace {

template<typename T>
static void UpsampleTrilinear3DForward(const int64_t elem_cnt, const T* in_dptr,
                                       NdIndexOffsetHelper<int64_t, 5> in_helper,
                                       NdIndexOffsetHelper<int64_t, 5> out_helper,
                                       const int64_t in_depth, const int64_t in_height,
                                       const int64_t in_width, const float scale_d,
                                       const float scale_h, const float scale_w,
                                       const bool align_corners, T* out_dptr) {
  for (int64_t index = 0; index < elem_cnt; ++index) {
    int64_t n, c, d, h, w;
    out_helper.OffsetToNdIndex(index, n, c, d, h, w);
    const T rdepth = GetAreaPixelScale(in_depth, d, align_corners, scale_d);
    const T rheight = GetAreaPixelScale(in_height, h, align_corners, scale_h);
    const T rwidth = GetAreaPixelScale(in_width, w, align_corners, scale_w);

    const T t1r = GetAreaPixel(rdepth, d, align_corners);
    const int64_t t1 = t1r;
    const int64_t t1p = (t1 < in_depth - 1) ? 1 : 0;
    const T t1lambda = t1r - t1;
    const T t0lambda = static_cast<T>(1.) - t1lambda;

    const T h1r = GetAreaPixel(rheight, h, align_corners);
    const int64_t h1 = h1r;
    const int64_t h1p = (h1 < in_height - 1) ? 1 : 0;
    const T h1lambda = h1r - h1;
    const T h0lambda = static_cast<T>(1.) - h1lambda;

    const T w1r = GetAreaPixel(rwidth, w, align_corners);
    const int64_t w1 = w1r;
    const int64_t w1p = (w1 < in_width - 1) ? 1 : 0;
    const T w1lambda = w1r - w1;
    const T w0lambda = static_cast<T>(1.) - w1lambda;

    const T* pos1 = &in_dptr[in_helper.NdIndexToOffset(n, c, t1, h1, w1)];

    out_dptr[index] =
        t0lambda
            * (h0lambda * (w0lambda * pos1[0] + w1lambda * pos1[w1p])
               + h1lambda
                     * (w0lambda * pos1[h1p * in_width] + w1lambda * pos1[h1p * in_width + w1p]))
        + t1lambda
              * (h0lambda
                     * (w0lambda * pos1[t1p * in_height * in_width]
                        + w1lambda * pos1[t1p * in_height * in_width + w1p])
                 + h1lambda
                       * (w0lambda * pos1[t1p * in_height * in_width + h1p * in_width]
                          + w1lambda * pos1[t1p * in_height * in_width + h1p * in_width + w1p]));
  }
}

template<typename T>
static void UpsampleTrilinear3DBackward(const int64_t elem_cnt, const T* dy_dptr,
                                        NdIndexOffsetHelper<int64_t, 5> dy_helper,
                                        NdIndexOffsetHelper<int64_t, 5> dx_helper,
                                        const int64_t in_depth, const int64_t in_height,
                                        const int64_t in_width, const float scale_d,
                                        const float scale_h, const float scale_w,
                                        const bool align_corners, T* dx_dptr) {
  for (int64_t index = 0; index < elem_cnt; ++index) {
    int64_t n, c, d, h, w;
    dy_helper.OffsetToNdIndex(index, n, c, d, h, w);
    // const int64_t dx_h = GetNearestInputIndex(h, scale_h, in_height);
    // const int64_t dx_w = GetNearestInputIndex(w, scale_w, in_width);
    // *(dx_dptr + dx_helper.NdIndexToOffset(n, c, d, dx_h, dx_w)) += dy_dptr[index];
  }
}

}  // namespace

template<typename T>
class UpsampleTrilinear3DCPUKernel final : public user_op::OpKernel {
 public:
  UpsampleTrilinear3DCPUKernel() = default;
  ~UpsampleTrilinear3DCPUKernel() = default;

 private:
  void Compute(user_op::KernelComputeContext* ctx) const override {
    const user_op::Tensor* x_blob = ctx->Tensor4ArgNameAndIndex("x", 0);
    user_op::Tensor* y_blob = ctx->Tensor4ArgNameAndIndex("y", 0);
    const float depth_scale = ctx->Attr<float>("depth_scale");
    const float height_scale = ctx->Attr<float>("height_scale");
    const float width_scale = ctx->Attr<float>("width_scale");
    const bool align_corners = ctx->Attr<bool>("align_corners");
    const int64_t elem_cnt = y_blob->shape().elem_cnt();
    NdIndexOffsetHelper<int64_t, 5> in_helper(x_blob->shape().At(0), x_blob->shape().At(1),
                                              x_blob->shape().At(2), x_blob->shape().At(3),
                                              x_blob->shape().At(4));
    NdIndexOffsetHelper<int64_t, 5> out_helper(y_blob->shape().At(0), y_blob->shape().At(1),
                                               y_blob->shape().At(2), y_blob->shape().At(3),
                                               y_blob->shape().At(4));
    UpsampleTrilinear3DForward<T>(elem_cnt, x_blob->dptr<T>(), in_helper, out_helper,
                                  x_blob->shape().At(2), x_blob->shape().At(3),
                                  x_blob->shape().At(4), 1.f / depth_scale, 1.f / height_scale,
                                  1.f / width_scale, align_corners, y_blob->mut_dptr<T>());
  }
  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

template<typename T>
class UpsampleLinearGrad3DCPUKernel final : public user_op::OpKernel {
 public:
  UpsampleLinearGrad3DCPUKernel() = default;
  ~UpsampleLinearGrad3DCPUKernel() = default;

 private:
  void Compute(user_op::KernelComputeContext* ctx) const override {
    user_op::Tensor* dx_blob = ctx->Tensor4ArgNameAndIndex("dx", 0);
    if (dx_blob == nullptr) { return; }
    Memset<DeviceType::kCPU>(ctx->device_ctx(), dx_blob->mut_dptr<T>(), 0,
                             dx_blob->shape().elem_cnt() * sizeof(T));
    const user_op::Tensor* dy_blob = ctx->Tensor4ArgNameAndIndex("dy", 0);
    const float depth_scale = ctx->Attr<float>("depth_scale");
    const float height_scale = ctx->Attr<float>("height_scale");
    const float width_scale = ctx->Attr<float>("width_scale");
    const bool align_corners = ctx->Attr<bool>("align_corners");
    const int64_t elem_cnt = dy_blob->shape().elem_cnt();
    NdIndexOffsetHelper<int64_t, 5> dy_helper(dy_blob->shape().At(0), dy_blob->shape().At(1),
                                              dy_blob->shape().At(2), dy_blob->shape().At(3),
                                              dy_blob->shape().At(4));
    NdIndexOffsetHelper<int64_t, 5> dx_helper(dx_blob->shape().At(0), dx_blob->shape().At(1),
                                              dx_blob->shape().At(2), dx_blob->shape().At(3),
                                              dx_blob->shape().At(4));
    UpsampleTrilinear3DBackward<T>(elem_cnt, dy_blob->dptr<T>(), dy_helper, dx_helper,
                                   dx_blob->shape().At(2), dx_blob->shape().At(3),
                                   dx_blob->shape().At(4), 1.f / depth_scale, 1.f / height_scale,
                                   1.f / width_scale, align_corners, dx_blob->mut_dptr<T>());
  }
  bool AlwaysComputeWhenAllOutputsEmpty() const override { return false; }
};

#define REGISTER_UPSAMPNEAREST3D_CPU_KERNEL(dtype)                                     \
  REGISTER_USER_KERNEL("upsample_trilinear_3d")                                        \
      .SetCreateFn<UpsampleTrilinear3DCPUKernel<dtype>>()                              \
      .SetIsMatchedHob((user_op::HobDeviceTag() == "cpu")                              \
                       & (user_op::HobDataType("y", 0) == GetDataType<dtype>::value)); \
  REGISTER_USER_KERNEL("upsample_trilinear_3d_grad")                                   \
      .SetCreateFn<UpsampleLinearGrad3DCPUKernel<dtype>>()                             \
      .SetIsMatchedHob((user_op::HobDeviceTag() == "cpu")                              \
                       & (user_op::HobDataType("dx", 0) == GetDataType<dtype>::value));

REGISTER_UPSAMPNEAREST3D_CPU_KERNEL(float)
REGISTER_UPSAMPNEAREST3D_CPU_KERNEL(double)

}  // namespace oneflow
