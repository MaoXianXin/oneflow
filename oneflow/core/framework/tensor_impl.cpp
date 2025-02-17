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
#include <type_traits>
#include "oneflow/api/foreign_lock_helper.h"
#include "oneflow/core/common/blocking_counter.h"
#include "oneflow/core/framework/instructions_builder.h"
#include "oneflow/core/framework/tensor_impl.h"
#include "oneflow/core/framework/tensor.h"
#include "oneflow/core/job/parallel_desc.h"
#include "oneflow/core/job/sbp_parallel.cfg.h"
#include "oneflow/core/framework/device.h"
#include "oneflow/core/framework/dtype.h"
#include "oneflow/core/eager/eager_blob_object.h"
#include "oneflow/core/framework/vm_local_dep_object.h"
#include "oneflow/core/vm/vm_util.h"
#include "oneflow/core/operator/operator.h"
#include "oneflow/core/control/global_process_ctx.h"

namespace oneflow {
namespace one {

Maybe<Tensor> TensorImpl::acc_grad() const {
  CHECK_NOTNULL_OR_RETURN(autograd_meta_);
  return autograd_meta_->acc_grad();
}

Maybe<TensorArg> TensorImpl::current_grad() const {
  CHECK_NOTNULL_OR_RETURN(autograd_meta_);
  return autograd_meta_->current_grad();
}

Maybe<void> TensorImpl::set_acc_grad(const std::shared_ptr<Tensor>& grad) {
  CHECK_NOTNULL_OR_RETURN(autograd_meta_);
  autograd_meta_->set_acc_grad(grad);
  return Maybe<void>::Ok();
}

Maybe<Tensor> TensorImpl::mut_acc_grad() {
  CHECK_NOTNULL_OR_RETURN(autograd_meta_);
  return autograd_meta_->mut_acc_grad();
}

Maybe<void> TensorImpl::set_retain_grad(bool retain_grad) {
  CHECK_NOTNULL_OR_RETURN(autograd_meta_);
  autograd_meta_->set_retain_grad(retain_grad);
  return Maybe<void>::Ok();
}

namespace {

std::shared_ptr<const MirroredTensorMeta> NewDefaultMirroredTensorMeta() {
  const auto& shape = std::make_shared<Shape>();
  const auto& dtype = DataType::kInvalidDataType;
  return std::make_shared<MirroredTensorMeta>(shape, dtype, Symbol<Device>());
}

}  // namespace

Maybe<MirroredTensorImpl> LazyMirroredTensorImpl::detach() const {
  auto detached_impl = std::make_shared<LazyMirroredTensorImpl>(tensor_meta_, false, true);
  return std::shared_ptr<MirroredTensorImpl>(detached_impl);
}

EagerMirroredTensorImpl::EagerMirroredTensorImpl()
    : MirroredTensorImpl(NewDefaultMirroredTensorMeta(), false, false) {}

EagerMirroredTensorImpl::EagerMirroredTensorImpl(
    const std::shared_ptr<const MirroredTensorMeta>& tensor_meta, bool requires_grad, bool is_leaf)
    : MirroredTensorImpl(tensor_meta, requires_grad, is_leaf) {}

EagerMirroredTensorImpl::~EagerMirroredTensorImpl() {}

EagerMirroredTensorImpl::EagerMirroredTensorImpl(
    const std::shared_ptr<const MirroredTensorMeta>& tensor_meta,
    std::shared_ptr<TensorStorage> tensor_storage, bool requires_grad, bool is_leaf)
    : MirroredTensorImpl(tensor_meta, requires_grad, is_leaf), tensor_storage_(tensor_storage) {}

Maybe<void> EagerMirroredTensorImpl::UpdateTensorStorage() {
  const auto& eager_blob_object = eager_blob_object_;
  tensor_storage_ = std::make_shared<TensorStorage>(eager_blob_object->tensor_buffer());
  const auto& parallel_desc = this->device()->parallel_desc_ptr();
  tensor_storage_->set_releaser_hook(
      [eager_blob_object, parallel_desc](const std::shared_ptr<vm::TensorBuffer>&) {
        CHECK_JUST(PhysicalRun([&](InstructionsBuilder* builder) -> Maybe<void> {
          JUST(builder->ReleaseTensor(eager_blob_object, parallel_desc));
          return Maybe<void>::Ok();
        }));
      });
  return Maybe<void>::Ok();
}

Maybe<VmLocalDepObject> EagerMirroredTensorImpl::compute_local_dep_object() const {
  return JUST(eager_blob_object())->compute_local_dep_object();
}

Maybe<void> EagerMirroredTensorImpl::InitEagerBlobObject(
    const std::shared_ptr<MemoryCase>& mem_case) {
  const auto& tensor_device = device();
  CHECK_OR_RETURN(static_cast<bool>(tensor_device));
  const auto& mut_shape = std::const_pointer_cast<Shape>(tensor_meta()->shape_ptr());
  const auto& eager_blob_object = std::make_shared<vm::EagerBlobObject>(
      mem_case, mut_shape, dtype(), std::make_shared<vm::TensorBuffer>(),
      tensor_device->parallel_desc_ptr());
  JUST(set_eager_blob_object(eager_blob_object));
  return Maybe<void>::Ok();
}

Maybe<void> EagerMirroredTensorImpl::InitEagerBlobObjectAndTensorStorage(
    const std::shared_ptr<vm::EagerBlobObject>& eager_blob_object,
    const std::shared_ptr<TensorStorage>& tensor_storage) {
  CHECK_OR_RETURN(eager_blob_object->tensor_buffer() == tensor_storage->buffer());
  eager_blob_object_ = eager_blob_object;
  tensor_storage_ = tensor_storage;
  return Maybe<void>::Ok();
}

Maybe<void> EagerMirroredTensorImpl::set_eager_blob_object(
    std::shared_ptr<vm::EagerBlobObject> eager_blob_object) {
  eager_blob_object_ = eager_blob_object;
  CHECK_OR_RETURN(eager_blob_object_->blob_desc().shape_ptr().get()
                  == tensor_meta()->shape_ptr().get());
  CHECK_OR_RETURN(eager_blob_object_->blob_desc().data_type() == tensor_meta()->dtype());
  JUST(UpdateTensorStorage());
  return Maybe<void>::Ok();
}

const std::shared_ptr<const Shape>& EagerMirroredTensorImpl::shape() const {
  if (!eager_blob_object_) { return tensor_meta()->shape_ptr(); }
  if (eager_blob_object_->is_shape_synced()) { return eager_blob_object_->blob_desc().shape_ptr(); }

  std::atomic<bool> synced(false);

  CHECK_JUST(PhysicalRun([&](InstructionsBuilder* builder) -> Maybe<void> {
    JUST(builder->AccessBlobByCallback(
        this, [&synced](uint64_t) { synced = true; }, "const"));
    return Maybe<void>::Ok();
  }));

  Global<ForeignLockHelper>::Get()->WithScopedRelease([&synced]() {
    // spin wait
    while (!synced) {}
  });

  eager_blob_object_->set_is_shape_synced(true);
  return eager_blob_object_->blob_desc().shape_ptr();
}

Maybe<MirroredTensorImpl> EagerMirroredTensorImpl::detach() const {
  auto detached_impl =
      std::make_shared<EagerMirroredTensorImpl>(tensor_meta_, tensor_storage_, false, true);
  detached_impl->eager_blob_object_ = eager_blob_object_;
  return std::shared_ptr<MirroredTensorImpl>(detached_impl);
}

bool MirroredTensorMeta::operator==(const MirroredTensorMeta& other) const {
  // It's correct to ignore is_dynamic_ field.
  return *this->shape_ptr() == *other.shape_ptr() && this->dtype() == other.dtype()
         && *this->device() == *other.device();
}

size_t MirroredTensorMeta::CalcHashValue() const {
  // It's correct to ignore is_dynamic_ field.
  return std::hash<Shape>()(*shape_ptr()) ^ std::hash<DataType>()(dtype())
         ^ std::hash<Device>()(*device());
}

bool ConsistentTensorMeta::operator==(const ConsistentTensorMeta& other) const {
  // It's correct to ignore is_dynamic_ field.
  return *this->shape_ptr() == *other.shape_ptr() && this->dtype() == other.dtype()
         && this->parallel_distribution() == other.parallel_distribution()
         && this->parallel_desc() == other.parallel_desc();
}

size_t ConsistentTensorMeta::CalcHashValue() const {
  return std::hash<Shape>()(*shape_ptr()) ^ std::hash<DataType>()(dtype())
         ^ std::hash<Symbol<cfg::ParallelDistribution>>()(parallel_distribution())
         ^ std::hash<Symbol<ParallelDesc>>()(parallel_desc());
}

EagerConsistentTensorImpl::EagerConsistentTensorImpl(
    Symbol<ConsistentTensorMeta> consistent_tensor_meta, bool requires_grad, bool is_leaf,
    const std::shared_ptr<MirroredTensor>& cur_rank_phy_tensor)
    : ConsistentTensorImpl(consistent_tensor_meta, cur_rank_phy_tensor->requires_grad(),
                           cur_rank_phy_tensor->is_leaf()),
      cur_rank_phy_tensor_(cur_rank_phy_tensor) {}

/*static*/ Maybe<EagerConsistentTensorImpl> EagerConsistentTensorImpl::New(
    const std::shared_ptr<MirroredTensor>& cur_rank_phy_tensor,
    Symbol<cfg::ParallelDistribution> parallel_distribution, Symbol<ParallelDesc> parallel_desc) {
  CHECK_OR_RETURN(!cur_rank_phy_tensor->is_lazy());
  {
    int64_t machine_id = 0;
    int64_t device_id = 0;
    GlobalProcessCtx::GetCurrentMachineIdAndDeviceId(&machine_id, &device_id);
    const auto& device = JUST(Device::ThreadLocalGetOrNew(parallel_desc->device_tag(), device_id));
    const auto& cur_rank_phy_device = JUST(cur_rank_phy_tensor->device());
    CHECK_OR_RETURN(*device == *cur_rank_phy_device)
        << "only LocalTensors on current rank Device can be casted to ConsistentTensor";
  }
  const auto& shape =
      JUST(GetLogicalShape(*cur_rank_phy_tensor->shape(), *parallel_distribution, *parallel_desc));
  const auto& dtype = cur_rank_phy_tensor->dtype();
  Symbol<ConsistentTensorMeta> consistent_tensor_meta(
      ConsistentTensorMeta(shape, dtype, parallel_distribution, parallel_desc));
  return std::shared_ptr<EagerConsistentTensorImpl>(
      new EagerConsistentTensorImpl(consistent_tensor_meta, cur_rank_phy_tensor->requires_grad(),
                                    cur_rank_phy_tensor->is_leaf(), cur_rank_phy_tensor));
}

/*static*/ Maybe<EagerConsistentTensorImpl> EagerConsistentTensorImpl::New(
    Symbol<ConsistentTensorMeta> consistent_tensor_meta, bool requires_grad, bool is_leaf) {
  const auto& parallel_desc = consistent_tensor_meta->parallel_desc();
  int64_t parallel_id = -1;
  const auto& device = JUST(parallel_desc->GetDevice4CurrentProcessCtx(&parallel_id));
  using TensorImpl = EagerConsistentTensorImpl;
  TensorImpl::NewMethod NewImpl =
      (device ? &TensorImpl::NewWithPhyTensor : &TensorImpl::NewWithoutPhyTensor);
  return NewImpl(consistent_tensor_meta, device, parallel_id, requires_grad, is_leaf);
}

/*static*/ Maybe<EagerConsistentTensorImpl> EagerConsistentTensorImpl::NewWithPhyTensor(
    Symbol<ConsistentTensorMeta> consistent_tensor_meta, Symbol<Device> device, int64_t parallel_id,
    bool requires_grad, bool is_leaf) {
  const auto& shape = consistent_tensor_meta->shape_ptr();
  const auto& dtype = consistent_tensor_meta->dtype();
  const auto& parallel_distribution = consistent_tensor_meta->parallel_distribution();
  const auto& parallel_desc = consistent_tensor_meta->parallel_desc();
  const auto& cur_rank_phy_shape =
      JUST(GetPhysicalShape(*shape, *parallel_distribution, *parallel_desc, parallel_id));
  const auto& cur_rank_phy_tensor_meta =
      std::make_shared<MirroredTensorMeta>(cur_rank_phy_shape, dtype, device);
  auto cur_rank_phy_tensor_impl =
      std::make_shared<EagerMirroredTensorImpl>(cur_rank_phy_tensor_meta, requires_grad, is_leaf);
  JUST(cur_rank_phy_tensor_impl->InitEagerBlobObject(device->mem_case()));
  const auto& cur_rank_phy_tensor = std::make_shared<MirroredTensor>(cur_rank_phy_tensor_impl);
  auto* tensor_impl =
      new EagerConsistentTensorImpl(consistent_tensor_meta, cur_rank_phy_tensor->requires_grad(),
                                    cur_rank_phy_tensor->is_leaf(), cur_rank_phy_tensor);
  return std::shared_ptr<EagerConsistentTensorImpl>(tensor_impl);
}

/*static*/ Maybe<EagerConsistentTensorImpl> EagerConsistentTensorImpl::NewWithoutPhyTensor(
    Symbol<ConsistentTensorMeta> consistent_tensor_meta, Symbol<Device> device, int64_t parallel_id,
    bool requires_grad, bool is_leaf) {
  auto* tensor_impl = new EagerConsistentTensorImpl(consistent_tensor_meta, requires_grad, is_leaf,
                                                    std::shared_ptr<MirroredTensor>());
  return std::shared_ptr<EagerConsistentTensorImpl>(tensor_impl);
}

}  // namespace one
}  // namespace oneflow
