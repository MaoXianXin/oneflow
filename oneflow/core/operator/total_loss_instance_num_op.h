#ifndef ONEFLOW_CORE_OPERATOR_TOTAL_LOSS_INSTANCE_NUM_OP_H_
#define ONEFLOW_CORE_OPERATOR_TOTAL_LOSS_INSTANCE_NUM_OP_H_

#include "oneflow/core/operator/cwise_op.h"

namespace oneflow {

class TotalLossInstanceNumOp final : public CWiseOp {
 public:
  OF_DISALLOW_COPY_AND_MOVE(TotalLossInstanceNumOp);
  TotalLossInstanceNumOp() = default;
  ~TotalLossInstanceNumOp() = default;

  void VirtualInitFromOpConf() override;
  const PbMessage& GetCustomizedConf() const override;
  bool NeedInBlobWhenBackward() const override { return false; }
  bool NeedOutBlobWhenBackward() const override { return false; }
  void VirtualInferBlobDescs(std::function<BlobDesc*(const std::string&)> GetBlobDesc4BnInOp,
                             const ParallelContext* parallel_ctx) const override;

 private:
  bool IsInputBlobAllowedModelSplit(const std::string& ibn) const override { return false; }
  void GetSbpSignatures(std::vector<std::unique_ptr<const SbpSignature>>*) const override;
};

}  // namespace oneflow

#endif  // ONEFLOW_CORE_OPERATOR_TOTAL_LOSS_INSTANCE_NUM_OP_H_
