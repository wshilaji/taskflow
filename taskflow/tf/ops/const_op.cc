#include "absl/strings/str_split.h"
#include "ecm/taskflow/data.h"
#include "ecm/taskflow/op_kernel.h"

namespace ecm::taskflow {

struct ConstArrayInt64Op : public OpKernel {

  std::vector<int64_t> const_nums;

  ConstArrayInt64Op(OpKernelConstruction *ctx)
      : OpKernel(ctx, {},
                 {
                     {"span<const int64>", "output"},
                 }) {
    std::string input_string;
    ECM_OP_REQUIRE_OK(ctx, ctx->GetAttr(&input_string, "const_nums"));

    std::vector<absl::string_view> string_nums =
        absl::StrSplit(input_string, ',');
    for (auto string_num : string_nums) {
      int64_t number;
      ECM_OP_REQUIRE(ctx, absl::SimpleAtoi(string_num, &number));
      const_nums.push_back(number);
    }
  }

  Status Compute(OpKernelContext *ctx) override {
    ConstInt64SpanData *output;
    ECM_RETURN_IF_ERROR(
        AllocOutput<ConstInt64SpanData>(ctx, "output", &output));

    output->data = absl::MakeConstSpan(const_nums);

    return OkStatus();
  }
};

ECM_REGISTER_KERNEL("ConstArrayInt64Op", ConstArrayInt64Op);

struct ConstArrayFloatOp : public OpKernel {

  std::vector<float> const_nums;

  ConstArrayFloatOp(OpKernelConstruction *ctx)
      : OpKernel(ctx, {},
                 {
                     {"span<const float>", "output"},
                 }) {
    std::string input_string;
    ECM_OP_REQUIRE_OK(ctx, ctx->GetAttr(&input_string, "const_nums"));

    if (input_string.empty()) {
      return;
    }

    std::vector<absl::string_view> string_nums =
        absl::StrSplit(input_string, ',');
    for (auto string_num : string_nums) {
      float number;
      ECM_OP_REQUIRE(ctx, absl::SimpleAtof(string_num, &number));
      const_nums.push_back(number);
    }
  }

  Status Compute(OpKernelContext *ctx) override {
    ConstFloatSpanData *output;
    ECM_RETURN_IF_ERROR(
        AllocOutput<ConstFloatSpanData>(ctx, "output", &output));

    output->data = absl::MakeConstSpan(const_nums);

    return OkStatus();
  }
};

ECM_REGISTER_KERNEL("ConstArrayFloatOp", ConstArrayFloatOp);

} // namespace ecm::taskflow