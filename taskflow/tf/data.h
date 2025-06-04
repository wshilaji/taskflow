#pragma once

#include "absl/container/flat_hash_set.h"
#include "absl/types/span.h"
#include "ecm/core/json.h"
#include "google/protobuf/message.h"

namespace ecm::taskflow {

class Data {
public:
  explicit Data(const std::string &type) : type_(type) {}
  virtual ~Data() = default;
  const std::string &Type() const { return type_; }
  virtual std::string DebugString() const { return R"("class<Data>")"; } // 当""中有特殊字符 \ <之类的可以无需使用转义字符

private:
  std::string type_;
};

struct StringData : Data {
  StringData() : Data("string") {}
  std::string DebugString() const override { return format(R"("{}")", *data); }
  std::string *data = nullptr;
};

struct BoolData : public Data {
  BoolData() : Data("bool") {}
  std::string DebugString() const override { return data ? "true" : "false"; }
  bool data = false;
};

template <class T> struct ArrayData : public Data {
  explicit ArrayData(const std::string &type) : Data(type) {}
  std::string DebugString() const override {
    return format("[{}]", absl::StrJoin(data, ","));
  }
  std::vector<T> data;
};

struct Int64ArrayData : public ArrayData<int64_t> {
  Int64ArrayData() : ArrayData("array<int64>") {}
};
struct FloatArrayData : public ArrayData<float> {
  FloatArrayData() : ArrayData("array<float>") {}
};
struct StringArrayData : public ArrayData<std::string> {
  StringArrayData() : ArrayData("array<string>") {}
};

template <class T> struct RepeatedData : public Data {
  explicit RepeatedData(const std::string &type) : Data(type) {}
  std::string DebugString() const override {
    return format("[{}]", absl::StrJoin(*data, ","));
  }
  google::protobuf::RepeatedField<T> *data = nullptr; // allocated on arena
};

struct RepeatedInt64Data : public RepeatedData<int64_t> {
  RepeatedInt64Data() : RepeatedData("repeated<int64>") {}
};
struct RepeatedFloatData : public RepeatedData<float> {
  RepeatedFloatData() : RepeatedData("repeated<float>") {}
};

template <class T> struct RepeatedPtrData : public Data {
  explicit RepeatedPtrData(const std::string &type) : Data(type) {}
  google::protobuf::RepeatedPtrField<T> *data = nullptr; // allocated on arena
};
struct RepeatedStringData : public RepeatedPtrData<std::string> {
  RepeatedStringData() : RepeatedPtrData("repeated<string>") {}
  std::string DebugString() const override {
    return format("[{}]", absl::StrJoin(*data, ","));
  }
};

template <class T> struct SpanData : public Data {
  explicit SpanData(const std::string &type) : Data(type) {}
  std::string DebugString() const override {
    return format("[{}]", absl::StrJoin(data, ","));
  }
  absl::Span<T> data;
};

struct Int64SpanData : public SpanData<int64_t> {
  Int64SpanData() : SpanData("span<int64>") {}
};
struct FloatSpanData : public SpanData<float> {
  FloatSpanData() : SpanData("span<float>") {}
};
struct StringSpanData : public SpanData<std::string> {
  StringSpanData() : SpanData("span<string>") {}
};

struct ConstInt64SpanData : public SpanData<const int64_t> {
  ConstInt64SpanData() : SpanData("span<const int64>") {}
};
struct ConstFloatSpanData : public SpanData<const float> {
  ConstFloatSpanData() : SpanData("span<const float>") {}
};
struct ConstStringSpanData : public SpanData<const std::string> {
  ConstStringSpanData() : SpanData("span<const string>") {}
};

template <class T> struct SetData : public Data {
  explicit SetData(const std::string &type) : Data(type) {}
  std::string DebugString() const override {
    return format("[{}]", absl::StrJoin(data, ","));
  }
  absl::flat_hash_set<T> data;
};

struct Int64SetData : public SetData<int64_t> {
  Int64SetData() : SetData("set<int64>") {}
};
struct FloatSetData : public SetData<float> {
  FloatSetData() : SetData("set<float>") {}
};
struct StringSetData : public SetData<std::string> {
  StringSetData() : SetData("set<string>") {}
};

} // namespace ecm::taskflow
