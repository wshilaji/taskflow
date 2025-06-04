#include "ecm/taskflow/op_kernel.h"

namespace ecm::taskflow {

Status ParseNArgs(OpKernelConstruction *ctx, std::vector<Input> *result,
                  const std::vector<Input> &args) {
  for (auto &arg : args) {
    std::vector<std::string> type_parts = absl::StrSplit(arg.type, '*');
    ECM_RETURN_CHECK(type_parts.size() == 1 || type_parts.size() == 2);

    if (type_parts.size() == 1) {
      result->push_back(arg);
    } else {
      // N
      auto N_name = type_parts[0];
      absl::StripAsciiWhitespace(&N_name);

      int N;
      ECM_RETURN_IF_ERROR(ctx->GetIntAttr(&N, N_name));

      // real type
      auto type = type_parts[1];
      absl::StripAsciiWhitespace(&type);

      // expand to N name_<i>
      for (int i = 0; i < N; i++) {
        result->emplace_back(type, format("{}_{}", arg.name, i));
      }
    }
  }
  return OkStatus();
}

Status OpKernelConstruction::GetAttr(std::string *value,
                                     const std::string &name) const {
  auto it = node_def->attrs().find(name);
  if (it == node_def->attrs().end()) {
    return ECM_ERROR(StatusCode::kNotFound, "attr {} not found in node {}",
                     name, node_def->name());
  }
  *value = it->second;
  return OkStatus();
}

Status
OpKernelConstruction::GetAttrOrDefault(std::string *value,
                                       const std::string &name,
                                       const std::string &default_val) const {
  auto it = node_def->attrs().find(name);
  if (it == node_def->attrs().end()) {
    *value = default_val;
    return OkStatus();
  }
  *value = it->second;
  return OkStatus();
}

Status OpKernelConstruction::GetFloatAttr(float *result,
                                          const std::string &name) const {
  std::string str_val;
  ECM_RETURN_IF_ERROR(GetAttr(&str_val, name));
  if (!absl::SimpleAtof(str_val, result)) {
    return ECM_ERROR(StatusCode::kInvalidArgument,
                     "node {} GetFloatAttr {} {} failed", node_def->name(),
                     name, str_val);
  }
  return OkStatus();
}

Status OpKernelConstruction::GetFloatAttrOrDefault(float *result,
                                                   const std::string &name,
                                                   float default_val) const {
  std::string str_val;
  if (!GetAttr(&str_val, name).ok()) {
    *result = default_val;
    return OkStatus();
  }
  if (!absl::SimpleAtof(str_val, result)) {
    return ECM_ERROR(StatusCode::kInvalidArgument,
                     "node {} GetFloatAttr {} {} failed", node_def->name(),
                     name, str_val);
  }
  return OkStatus();
}

OpKernel::OpKernel(OpKernelConstruction *ctx, const std::vector<Input> &inputs,
                   const std::vector<Input> &outputs) {
  ECM_OP_REQUIRE_OK(ctx, ParseNArgs(ctx, &inputs_, inputs));
  ECM_OP_REQUIRE_OK(ctx, ParseNArgs(ctx, &outputs_, outputs));
}

} // namespace ecm::taskflow