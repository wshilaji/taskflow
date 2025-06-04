#pragma once

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "ecm/core/status.h"
#include "ecm/taskflow/graph.pb.h"
#include "ecm/taskflow/op_registry.h"
#include "ecm/taskflow/session.h"

namespace ecm::taskflow {

class Session;

struct OpKernelConstruction {
  const NodeProto *node_def = nullptr;
  Status status;

  Status GetAttr(std::string *value, const std::string &name) const;

  Status GetAttrOrDefault(std::string *value, const std::string &name,
                          const std::string &default_val) const;

  template <class T>
  Status GetIntAttr(T *result, const std::string &name) const {
    std::string str_val;
    ECM_RETURN_IF_ERROR(GetAttr(&str_val, name));
    if (!absl::SimpleAtoi(str_val, result)) {
      return ECM_ERROR(StatusCode::kInvalidArgument,
                       "node {} GetIntAttr {} {} failed", node_def->name(),
                       name, str_val);
    }
    return OkStatus();
  }

  template <typename T>
  Status GetIntAttr(absl::flat_hash_map<std::string, T> *result,
                    const std::string &name) const {
    std::string str_val;
    ECM_RETURN_IF_ERROR(GetAttr(&str_val, name));
    T v;
    for (auto &s : absl::StrSplit(str_val, ',')) {
      if (s.empty()) {
        continue;
      }
      std::vector<std::string> parts = absl::StrSplit(s, ':');
      absl::StripAsciiWhitespace(&parts[0]);
      if (parts[0].empty() || parts.size() < 2 ||
          !absl::SimpleAtoi(parts[1], &v)) {
        return ECM_ERROR(StatusCode::kInvalidArgument,
                         "invalid conf {} in node {} attr {}", s,
                         node_def->name(), name);
      }
      result->emplace(parts[0], v);
    }
    return OkStatus();
  }

  template <typename T>
  Status GetIntAttrOrDefault(absl::flat_hash_map<std::string, T> *result,
                             const std::string &name) const {
    auto s = GetIntAttr(result, name);
    if (absl::IsNotFound(s)) {
      return OkStatus();
    }
    return s;
  }

  template <class T>
  Status GetIntAttrOrDefault(T *result, const std::string &name,
                             T default_val) const {
    std::string str_val;
    if (!GetAttr(&str_val, name).ok()) {
      *result = default_val;
      return OkStatus();
    }
    if (!absl::SimpleAtoi(str_val, result)) {
      return ECM_ERROR(StatusCode::kInvalidArgument,
                       "node {} GetIntAttr {} {} failed", node_def->name(),
                       name, str_val);
    }
    return OkStatus();
  }

  Status GetFloatAttr(float *result, const std::string &name) const;

  Status GetFloatAttrOrDefault(float *result, const std::string &name,
                               float default_val) const;

  template <typename T>
  Status GetIntListAttr(std::vector<T> *result, const std::string &name) const {
    std::string str_val;
    ECM_RETURN_IF_ERROR(GetAttr(&str_val, name));
    T v;
    for (auto &s : absl::StrSplit(str_val, ',')) {
      if (s.empty()) {
        continue;
      }
      if (!absl::SimpleAtoi(s, &v)) {
        return ECM_ERROR(StatusCode::kInvalidArgument,
                         "invalid conf {} in node {} attr {}", s,
                         node_def->name(), name);
      }
      result->emplace_back(v);
    }
    return OkStatus();
  }

  template <typename T>
  Status GetIntListAttrOrDefault(std::vector<T> *result,
                                 const std::string &name,
                                 std::vector<T> &&default_val) const {
    auto s = GetIntListAttr(result, name);
    if (!s.ok()) {
      if (s.code() == StatusCode::kNotFound) {
        *result = default_val;
        return OkStatus();
      } else {
        return s;
      }
    }
    return OkStatus();
  }
};

struct OpKernelContext {
  const NodeProto *node_def = nullptr;
  Session *session = nullptr;

  template <class T, class... Args> T *CreateMessage(Args &&...args) {
    return google::protobuf::Arena::CreateMessage<T>(session->Arena(), args...);
  }
  template <class T, class... Args> T *Create(Args &&...args) {
    return google::protobuf::Arena::Create<T>(session->Arena(), args...);
  }
  // Create an array of object type T on the arena *without* invoking the
  // constructor of T. If `arena` is null, then the return value should be freed
  // with `delete[] x;` (or `::operator delete[](x);`).
  // To ensure safe uses, this function checks at compile time
  // (when compiled as C++11) that T is trivially default-constructible and
  // trivially destructible.
  template <class T> T *CreateArray(size_t num_elements) {
    return google::protobuf::Arena::CreateArray<T>(session->Arena(),
                                                   num_elements);
  }
  template <class T> google::protobuf::RepeatedField<T> *CreateRepeatedField() {
    return CreateMessage<google::protobuf::RepeatedField<T>>();
  }
  template <class T>
  google::protobuf::RepeatedPtrField<T> *CreateRepeatedPtrField() {
    return CreateMessage<google::protobuf::RepeatedPtrField<T>>();
  }

  template <class T> T *GetResourceManager() {
    return session->GetResourceManager<T>();
  }
  template <class T> T *GetExpData() { return session->GetExpData<T>(); }
};

/*

1. normal input
    {"type_name", "input_name"}

2. list input
    {"N * type_name ", "input_name"}

    N is a number defined in node attrs

    attrs: {
        "N": "3"
    }

    input names will be input_0, input_1, input_2
 */
struct Input {
  Input(const std::string &type, const std::string &name)
      : type(type), name(name) {}

  std::string type;
  std::string name;
};

class OpKernel {
public:
  OpKernel(OpKernelConstruction *ctx, const std::vector<Input> &inputs,
           const std::vector<Input> &outputs);

  virtual ~OpKernel() {}

  virtual bool IsExpensive() const { return false; }

  // 当 EnableFallback() 为 true 时，必须要实现 Fallback 函数
  virtual bool EnableFallback() const { return false; }

  virtual Status Compute(OpKernelContext *ctx) = 0;

  // Called if EnableFallback() and Compute(ctx) failed
  virtual Status Fallback(OpKernelContext *ctx) {
    return Status(
        StatusCode::kUnimplemented,
        "EnableFallback() is true, but Fallback() is not implemented");
  }

  const std::vector<Input> &Inputs() const { return inputs_; }
  const std::vector<Input> &Outputs() const { return outputs_; }

  template <class T>
  Status GetInput(OpKernelContext *ctx, const std::string &name,
                  const T **result) {
    auto i_it =
        std::find_if(inputs_.begin(), inputs_.end(),
                     [&name](auto &input) { return input.name == name; });
    if (i_it == inputs_.end()) {
      return ECM_ERROR(StatusCode::kNotFound, "input {} not found for node {}",
                       name, ctx->node_def->name());
    }
    auto i = i_it - inputs_.begin();
    auto &input_node = ctx->node_def->input(i);
    auto d_it = ctx->session->data_map_.find(input_node);
    if (d_it == ctx->session->data_map_.end()) {
      return ECM_ERROR(StatusCode::kNotFound, "input {} not found for node {}",
                       input_node, ctx->node_def->name());
    }
    *result = dynamic_cast<T *>(d_it->second.get());
    if (!*result) {
      return ECM_ERROR(StatusCode::kFailedPrecondition,
                       "cast input {} type failed for node {}", name,
                       ctx->node_def->name());
    }
    return OkStatus();
  }

  template <class T>
  Status AllocOutput(OpKernelContext *ctx, const std::string &name,
                     T **result) {
    auto i_it =
        std::find_if(outputs_.begin(), outputs_.end(),
                     [&name](auto &input) { return name == input.name; });
    if (i_it == outputs_.end()) {
      return ECM_ERROR(StatusCode::kNotFound, "output {} not found for node {}",
                       name, ctx->node_def->name());
    }
    auto output_node = format("{}:{}", ctx->node_def->name(), i_it->name);
    auto d_it = ctx->session->data_map_.find(output_node);
    if (d_it == ctx->session->data_map_.end()) {
      return ECM_ERROR(StatusCode::kNotFound, "output {} not found for node {}",
                       output_node, ctx->node_def->name());
    }
    if (d_it->second == nullptr) {
      d_it->second.reset(new T);
    }
    *result = dynamic_cast<T *>(d_it->second.get());
    if (!*result) {
      return ECM_ERROR(StatusCode::kFailedPrecondition,
                       "cast input {} type failed for node {}", name,
                       ctx->node_def->name());
    }
    return OkStatus();
  }

protected:
  std::vector<Input> inputs_;
  std::vector<Input> outputs_;
};

#define ECM_REGISTER_KERNEL(name, class)                                       \
  ECM_REGISTER_KERNEL_UNIQ_HELPER(__COUNTER__, name, class);

#define ECM_REGISTER_KERNEL_UNIQ_HELPER(counter, name, class)                  \
  ECM_REGISTER_KERNEL_UNIQ(counter, name, class);

#define ECM_REGISTER_KERNEL_UNIQ(counter, name, class)                         \
  static ecm::taskflow::OpRegistar ecm_op_registar_##counter##_helper(         \
      name, [](auto ctx) -> class * { return new class(ctx); });

} // namespace ecm::taskflow

#define ECM_OP_REQUIRE_OK(ctx, expr)                                           \
  ctx->status = expr;                                                          \
  if (!ctx->status.ok())                                                       \
    return;
#define ECM_OP_REQUIRE(ctx, expr)                                              \
  if (!(expr)) {                                                               \
    ctx->status = ECM_ERROR(ecm::StatusCode::kFailedPrecondition,              \
                            "check `" ECM_STR2(expr) "` failed");              \
    return;                                                                    \
  }
