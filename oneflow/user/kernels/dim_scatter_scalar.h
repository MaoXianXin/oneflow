#ifndef ONEFLOW_USER_KERNELS_DIM_SCATTER_SCALAR_H_
#define ONEFLOW_USER_KERNELS_DIM_SCATTER_SCALAR_H_
#include "oneflow/core/device/device_context.h"
#include "oneflow/core/ndarray/xpu_util.h"
#include "oneflow/core/common/nd_index_offset_helper.h"
#include "oneflow/core/framework/framework.h"
#include "oneflow/core/common/data_type.h"

namespace oneflow{

namespace user_op{

constexpr int kDimGatherMaxDimCount = 8;

template<typename T>
using DimOpIndexNdHelper = NdIndexOffsetHelper<T, kDimGatherMaxDimCount>;

template<typename IN_T, typename IDX_T>
OF_DEVICE_FUNC void ScatterScalarUpdateFunctor(const DimOpIndexNdHelper<IDX_T>& idx_nd_helper,
                              const DimOpIndexNdHelper<IDX_T>& output_nd_helper, const int ndim,
                              const int64_t elem_cnt, const int32_t dim, int64_t upper_bound, const IDX_T* index,
                              const IN_T src, IN_T* output) {
  XPU_1D_KERNEL_LOOP(idx_offset, elem_cnt) {
    IDX_T coordinate[kDimGatherMaxDimCount] = {0};

    idx_nd_helper.OffsetToNdIndex(idx_offset, coordinate, ndim); // idx_offset -> ijk
    IDX_T idx_elem = index[idx_offset];
    if(idx_elem>=upper_bound){
    #if __CUDA_ARCH__
      __trap(); 
    #else
      std::cout<<"The index element "<<idx_elem<<" is out of bounds for dimension "<<dim<<" with size "<<upper_bound<<std::endl;
      throw Error::CheckFailedError();
    #endif 
    }
    coordinate[dim] = idx_elem;
    IDX_T output_offset = output_nd_helper.NdIndexToOffset(coordinate, ndim);
    *(output+output_offset) = src;
  }
}

} // namespace user op
} // namespace oneflow 

#endif // ONEFLOW_USER_KERNELS_DIM_SCATTER_SCALAR_H_
