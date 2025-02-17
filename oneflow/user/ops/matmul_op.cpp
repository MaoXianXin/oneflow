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

namespace oneflow {

namespace {

Maybe<void> InferTensorDesc4Matmul(user_op::InferContext* ctx) {
  bool transpose_a = ctx->Attr<bool>("transpose_a");
  bool transpose_b = ctx->Attr<bool>("transpose_b");

  const user_op::TensorDesc& a = ctx->InputTensorDesc("a", 0);
  const user_op::TensorDesc& b = ctx->InputTensorDesc("b", 0);
  CHECK_EQ_OR_RETURN(a.shape().NumAxes(), b.shape().NumAxes());
  CHECK_GE_OR_RETURN(a.shape().NumAxes(), 2);
  size_t num_axes = a.shape().NumAxes();

  if (num_axes > 2) {
    for (int i = 0; i < num_axes - 2; ++i) { CHECK_EQ_OR_RETURN(a.shape().At(i), b.shape().At(i)); }
  }

  user_op::TensorDesc* out = ctx->OutputTensorDesc("out", 0);

  *ctx->OutputShape("out", 0) = ctx->InputShape("a", 0);
  *ctx->OutputIsDynamic("out", 0) = ctx->InputIsDynamic("a", 0);

  int64_t m, n, k;  // tensor a (no trans): m*k, tensor b (no trans): k*n
  if (!transpose_a) {
    m = a.shape().At(num_axes - 2);
    k = a.shape().At(num_axes - 1);
  } else {
    m = a.shape().At(num_axes - 1);
    k = a.shape().At(num_axes - 2);
  }
  if (!transpose_b) {
    CHECK_EQ_OR_RETURN(k, b.shape().At(num_axes - 2));
    n = b.shape().At(num_axes - 1);
  } else {
    CHECK_EQ_OR_RETURN(k, b.shape().At(num_axes - 1));
    n = b.shape().At(num_axes - 2);
  }
  out->mut_shape()->Set(num_axes - 2, m);
  out->mut_shape()->Set(num_axes - 1, n);
  if (ctx->has_input("_add_to_output", 0)) {
    const auto& add_to_output = ctx->InputTensorDesc("_add_to_output", 0);
    CHECK_EQ_OR_RETURN(add_to_output.shape(), out->shape());
  }
  return Maybe<void>::Ok();
}

Maybe<void> InferDataType4Matmul(user_op::InferContext* ctx) {
  const DataType& dtype = ctx->InputDType("a", 0);
  CHECK_EQ_OR_RETURN(ctx->InputDType("b", 0), dtype);
  if (ctx->has_input("_add_to_output", 0)) {
    CHECK_EQ_OR_RETURN(ctx->InputDType("_add_to_output", 0), dtype);
  }
  *ctx->OutputDType("out", 0) = dtype;
  return Maybe<void>::Ok();
}

void GenBackwardOpConf4Matmul(const std::string& op_type_name, const user_op::UserOpWrapper& op,
                              user_op::AddOpFn AddOp) {
  const bool transpose_a = op.attr<bool>("transpose_a");
  const bool transpose_b = op.attr<bool>("transpose_b");
  const double alpha = op.attr<double>("alpha");
  auto HandleGradOp = [&](user_op::UserOpConfWrapper&& grad_op,
                          std::string&& input_arg_name) -> void {
    op.BindGradTensorWithOpInput(grad_op.output("out", 0), input_arg_name, 0);
    AddOp(grad_op);
  };

  if (op.NeedGenGradTensor4OpInput("a", 0)) {
    if (transpose_a) {
      user_op::UserOpConfWrapper grad_a_op =
          user_op::UserOpConfWrapperBuilder(op.op_name() + "_grad_a")
              .Op(op_type_name)
              .Input("a", op.input("b", 0))
              .Input("b", op.GetGradTensorWithOpOutput("out", 0))
              .Output("out")
              .Attr<bool>("transpose_a", transpose_b)
              .Attr<bool>("transpose_b", true)
              .Attr<double>("alpha", alpha)
              .Build();
      HandleGradOp(std::move(grad_a_op), "a");
    } else {
      user_op::UserOpConfWrapper grad_a_op =
          user_op::UserOpConfWrapperBuilder(op.op_name() + "_grad_a")
              .Op(op_type_name)
              .Input("a", op.GetGradTensorWithOpOutput("out", 0))
              .Input("b", op.input("b", 0))
              .Output("out")
              .Attr<bool>("transpose_a", false)
              .Attr<bool>("transpose_b", !transpose_b)
              .Attr<double>("alpha", alpha)
              .Build();
      HandleGradOp(std::move(grad_a_op), "a");
    }
  }
  if (op.NeedGenGradTensor4OpInput("b", 0)) {
    if (transpose_b) {
      user_op::UserOpConfWrapper grad_b_op =
          user_op::UserOpConfWrapperBuilder(op.op_name() + "_grad_b")
              .Op(op_type_name)
              .Input("a", op.GetGradTensorWithOpOutput("out", 0))
              .Input("b", op.input("a", 0))
              .Output("out")
              .Attr<bool>("transpose_a", true)
              .Attr<bool>("transpose_b", transpose_a)
              .Attr<double>("alpha", alpha)
              .Build();
      HandleGradOp(std::move(grad_b_op), "b");
    } else {
      user_op::UserOpConfWrapper grad_b_op =
          user_op::UserOpConfWrapperBuilder(op.op_name() + "_grad_b")
              .Op(op_type_name)
              .Input("a", op.input("a", 0))
              .Input("b", op.GetGradTensorWithOpOutput("out", 0))
              .Output("out")
              .Attr<bool>("transpose_a", !transpose_a)
              .Attr<bool>("transpose_b", false)
              .Attr<double>("alpha", alpha)
              .Build();
      HandleGradOp(std::move(grad_b_op), "b");
    }
  }
}

}  // namespace

REGISTER_USER_OP("matmul")
    .Input("a")
    .Input("b")
    .OptionalInput("_add_to_output")
    .Output("out")
    .Attr<bool>("transpose_a", false)
    .Attr<bool>("transpose_b", false)
    .Attr<double>("alpha", 1.0)
    .SetTensorDescInferFn(InferTensorDesc4Matmul)
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      // (m, k_a) * (k_b, n) where k_a == k_b
      int32_t m_axis = -1;
      int32_t k_a_axis = -1;
      int32_t k_b_axis = -1;
      int32_t n_axis = -1;
      if (ctx->Attr<bool>("transpose_a")) {
        m_axis = 1;
        k_a_axis = 0;
      } else {
        m_axis = 0;
        k_a_axis = 1;
      }
      if (ctx->Attr<bool>("transpose_b")) {
        k_b_axis = 1;
        n_axis = 0;
      } else {
        k_b_axis = 0;
        n_axis = 1;
      }
      std::vector<user_op::OpArg> out_and_add_to_output_args;
      out_and_add_to_output_args.emplace_back("out", 0);
      if (ctx->user_op_conf().has_input("_add_to_output", 0)) {
        out_and_add_to_output_args.emplace_back("_add_to_output", 0);
      }
      ctx->NewBuilder()
          .Split(user_op::OpArg("a", 0), m_axis)
          .Broadcast(user_op::OpArg("b", 0))
          .Split(out_and_add_to_output_args, 0)
          .Build();
      ctx->NewBuilder()
          .Broadcast(user_op::OpArg("a", 0))
          .Split(user_op::OpArg("b", 0), n_axis)
          .Split(out_and_add_to_output_args, 1)
          .Build();
      ctx->NewBuilder()
          .Split(user_op::OpArg("a", 0), k_a_axis)
          .Split(user_op::OpArg("b", 0), k_b_axis)
          .PartialSum(out_and_add_to_output_args)
          .Build();
      ctx->NewBuilder()
          .PartialSum(user_op::OpArg("a", 0))
          .Broadcast(user_op::OpArg("b", 0))
          .PartialSum(out_and_add_to_output_args)
          .Build();
      ctx->NewBuilder()
          .Broadcast(user_op::OpArg("a", 0))
          .PartialSum(user_op::OpArg("b", 0))
          .PartialSum(out_and_add_to_output_args)
          .Build();
      return Maybe<void>::Ok();
    })
    .SetDataTypeInferFn(InferDataType4Matmul);

REGISTER_USER_OP_GRAD("matmul").SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op,
                                                          user_op::AddOpFn AddOp) {
  return GenBackwardOpConf4Matmul("matmul", op, AddOp);
});

REGISTER_USER_OP("batch_matmul")
    .Input("a")
    .Input("b")
    .OptionalInput("_add_to_output")
    .Output("out")
    .Attr<bool>("transpose_a", false)
    .Attr<bool>("transpose_b", false)
    .Attr<double>("alpha", 1.0)
    .SetTensorDescInferFn(InferTensorDesc4Matmul)
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      const user_op::TensorDesc& a_tensor = ctx->LogicalTensorDesc4InputArgNameAndIndex("a", 0);
      std::vector<user_op::OpArg> out_and_add_to_output_args;
      out_and_add_to_output_args.emplace_back("out", 0);
      if (ctx->user_op_conf().has_input("_add_to_output", 0)) {
        out_and_add_to_output_args.emplace_back("_add_to_output", 0);
      }
      FOR_RANGE(int64_t, i, 0, a_tensor.shape().NumAxes() - 2) {
        ctx->NewBuilder().Split(ctx->inputs(), i).Split(out_and_add_to_output_args, i).Build();
      }
      return Maybe<void>::Ok();
    })
    .SetDataTypeInferFn(InferDataType4Matmul);

REGISTER_USER_OP_GRAD("batch_matmul")
    .SetGenBackwardOpConfFn([](const user_op::UserOpWrapper& op, user_op::AddOpFn AddOp) {
      return GenBackwardOpConf4Matmul("batch_matmul", op, AddOp);
    });

REGISTER_USER_OP("broadcast_matmul")
    .Input("a")
    .Input("b")
    .OptionalInput("_add_to_output")
    .Output("out")
    .Attr<bool>("transpose_a", false)
    .Attr<bool>("transpose_b", false)
    .Attr<double>("alpha", 1.0)
    .SetDataTypeInferFn(InferDataType4Matmul)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      bool transpose_a = ctx->Attr<bool>("transpose_a");
      bool transpose_b = ctx->Attr<bool>("transpose_b");

      const user_op::TensorDesc& a = ctx->InputTensorDesc("a", 0);
      const user_op::TensorDesc& b = ctx->InputTensorDesc("b", 0);
      user_op::TensorDesc* out = ctx->OutputTensorDesc("out", 0);

      // NOTE: support broadcast b to a for now
      // TODO(zwx): support broadcast a to b
      CHECK_GT_OR_RETURN(a.shape().NumAxes(), b.shape().NumAxes());
      CHECK_EQ_OR_RETURN(b.shape().NumAxes(), 2);
      // NOTE: don't support transpose_a for now
      CHECK_OR_RETURN(!transpose_a);

      DimVector out_dim_vec(a.shape().NumAxes() - 1);
      FOR_RANGE(int64_t, i, 0, out_dim_vec.size()) { out_dim_vec[i] = a.shape().At(i); }
      int64_t k = a.shape().At(a.shape().NumAxes() - 1);
      int64_t n = -1;
      if (!transpose_b) {
        CHECK_EQ_OR_RETURN(k, b.shape().At(b.shape().NumAxes() - 2));
        n = b.shape().At(b.shape().NumAxes() - 1);
      } else {
        CHECK_EQ_OR_RETURN(k, b.shape().At(b.shape().NumAxes() - 1));
        n = b.shape().At(b.shape().NumAxes() - 2);
      }
      out_dim_vec.push_back(n);
      *out->mut_shape() = Shape(out_dim_vec);

      if (ctx->has_input("_add_to_output", 0)) {
        const user_op::TensorDesc& add_to_output = ctx->InputTensorDesc("_add_to_output", 0);
        CHECK_EQ_OR_RETURN(add_to_output.shape(), out->shape());
      }

      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      // (b, m, k) * (k, n) when transpose_b is false
      // (b, m, k) * (n, k) when transpose_b is true
      bool transpose_a = ctx->Attr<bool>("transpose_a");
      bool transpose_b = ctx->Attr<bool>("transpose_b");
      CHECK_OR_RETURN(!transpose_a);

      const auto& a_shape = ctx->LogicalTensorDesc4InputArgNameAndIndex("a", 0).shape();
      int32_t k_a_axis = a_shape.NumAxes() - 1;
      int32_t k_b_axis = -1;
      int32_t n_axis = -1;
      if (transpose_b) {
        k_b_axis = 1;
        n_axis = 0;
      } else {
        k_b_axis = 0;
        n_axis = 1;
      }

      std::vector<user_op::OpArg> out_and_add_to_output_args;
      out_and_add_to_output_args.emplace_back("out", 0);
      if (ctx->user_op_conf().has_input("_add_to_output", 0)) {
        out_and_add_to_output_args.emplace_back("_add_to_output", 0);
      }

      // S(b or m axis) x B -> S(b or m axis)
      for (int64_t i = 0; i < a_shape.NumAxes() - 1; ++i) {
        ctx->NewBuilder()
            .Split(user_op::OpArg("a", 0), i)
            .Broadcast(user_op::OpArg("b", 0))
            .Split(out_and_add_to_output_args, i)
            .Build();
      }
      // B x S(n_axis) -> S(n_axis)
      ctx->NewBuilder()
          .Broadcast(user_op::OpArg("a", 0))
          .Split(user_op::OpArg("b", 0), n_axis)
          .Split(out_and_add_to_output_args, a_shape.NumAxes() - 1)
          .Build();
      // S(a_k_axis) x S(b_k_axis) -> P
      ctx->NewBuilder()
          .Split(user_op::OpArg("a", 0), k_a_axis)
          .Split(user_op::OpArg("b", 0), k_b_axis)
          .PartialSum(out_and_add_to_output_args)
          .Build();
      // P x B -> P
      ctx->NewBuilder()
          .PartialSum(user_op::OpArg("a", 0))
          .Broadcast(user_op::OpArg("b", 0))
          .PartialSum(out_and_add_to_output_args)
          .Build();
      // B x P -> P
      ctx->NewBuilder()
          .Broadcast(user_op::OpArg("a", 0))
          .PartialSum(user_op::OpArg("b", 0))
          .PartialSum(out_and_add_to_output_args)
          .Build();
      return Maybe<void>::Ok();
    });

REGISTER_USER_OP("broadcast_matmul_grad_b")
    .Input("a")
    .Input("b")
    .OptionalInput("_add_to_output")
    .Output("out")
    .Attr<double>("alpha", 1.0)
    .SetDataTypeInferFn(InferDataType4Matmul)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      const user_op::TensorDesc& a = ctx->InputTensorDesc("a", 0);
      const user_op::TensorDesc& b = ctx->InputTensorDesc("b", 0);
      user_op::TensorDesc* out = ctx->OutputTensorDesc("out", 0);

      CHECK_EQ_OR_RETURN(a.shape().NumAxes(), b.shape().NumAxes());
      for (int i = 0; i < a.shape().NumAxes() - 1; ++i) {
        CHECK_EQ_OR_RETURN(a.shape().At(i), b.shape().At(i));
      }

      *out->mut_shape() =
          Shape({a.shape().At(a.shape().NumAxes() - 1), b.shape().At(b.shape().NumAxes() - 1)});

      if (ctx->has_input("_add_to_output", 0)) {
        const user_op::TensorDesc& add_to_output = ctx->InputTensorDesc("_add_to_output", 0);
        CHECK_EQ_OR_RETURN(add_to_output.shape(), out->shape());
      }

      return Maybe<void>::Ok();
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      const auto& a_shape = ctx->LogicalTensorDesc4InputArgNameAndIndex("a", 0).shape();
      int64_t last_axis = a_shape.NumAxes() - 1;

      std::vector<user_op::OpArg> out_and_add_to_output_args;
      out_and_add_to_output_args.emplace_back("out", 0);
      if (ctx->user_op_conf().has_input("_add_to_output", 0)) {
        out_and_add_to_output_args.emplace_back("_add_to_output", 0);
      }

      // S(b or m axis) x S(b or m axis) -> P
      for (int64_t i = 0; i < last_axis; ++i) {
        ctx->NewBuilder()
            .Split(user_op::OpArg("a", 0), i)
            .Split(user_op::OpArg("b", 0), i)
            .PartialSum(out_and_add_to_output_args)
            .Build();
      }

      // (b, m, k) * (b, m, n) -> (k, n) [transpose a]
      // S(k) x B -> S(0) or B x S(n) -> S(1)
      // (b, m, n) * (b, m, k) -> (n, k) [transpose a]
      // S(n) x B -> S(0) or B x S(k) -> S(1)
      ctx->NewBuilder()
          .Split(user_op::OpArg("a", 0), last_axis)
          .Broadcast(user_op::OpArg("b", 0))
          .Split(out_and_add_to_output_args, 0)
          .Build();
      ctx->NewBuilder()
          .Broadcast(user_op::OpArg("a", 0))
          .Split(user_op::OpArg("b", 0), last_axis)
          .Split(out_and_add_to_output_args, 1)
          .Build();

      return Maybe<void>::Ok();
    });

REGISTER_USER_OP_GRAD("broadcast_matmul")
    .SetBackwardOpConfGenFn([](user_op::BackwardOpConfContext* ctx) -> void {
      bool transpose_a = ctx->FwOp().attr<bool>("transpose_a");
      bool transpose_b = ctx->FwOp().attr<bool>("transpose_b");
      double alpha = ctx->FwOp().attr<double>("alpha");
      CHECK(!transpose_a);

      std::string a_grad_op_name = ctx->FwOp().op_name() + "_a_grad";
      ctx->DefineOp(a_grad_op_name,
                    [&](user_op::BackwardOpBuilder& builder) -> user_op::UserOpConfWrapper {
                      return builder.OpTypeName("broadcast_matmul")
                          .InputBind("a", ctx->FwOp().output_grad("out", 0))
                          .InputBind("b", ctx->FwOp().input("b", 0))
                          .Attr<bool>("transpose_a", transpose_a)
                          .Attr<bool>("transpose_b", !transpose_b)
                          .Attr<double>("alpha", alpha)
                          .Output("out")
                          .Build();
                    });

      ctx->FwOp().InputGradBind(user_op::OpArg("a", 0), [&]() -> const std::string& {
        return ctx->GetOp(a_grad_op_name).output("out", 0);
      });

      std::string b_grad_op_name = ctx->FwOp().op_name() + "_b_grad";
      ctx->DefineOp(b_grad_op_name,
                    [&](user_op::BackwardOpBuilder& builder) -> user_op::UserOpConfWrapper {
                      if (!transpose_b) {
                        return builder.OpTypeName("broadcast_matmul_grad_b")
                            .InputBind("a", ctx->FwOp().input("a", 0))
                            .InputBind("b", ctx->FwOp().output_grad("out", 0))
                            .Attr<double>("alpha", alpha)
                            .Output("out")
                            .Build();
                      } else {
                        return builder.OpTypeName("broadcast_matmul_grad_b")
                            .InputBind("a", ctx->FwOp().output_grad("out", 0))
                            .InputBind("b", ctx->FwOp().input("a", 0))
                            .Attr<double>("alpha", alpha)
                            .Output("out")
                            .Build();
                      }
                    });

      ctx->FwOp().InputGradBind(user_op::OpArg("b", 0), [&]() -> const std::string& {
        return ctx->GetOp(b_grad_op_name).output("out", 0);
      });
    });

}  // namespace oneflow
