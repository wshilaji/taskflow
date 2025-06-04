#pragma once

#include "absl/strings/str_join.h"
#include "core/status.h"
#include "json2pb/json_to_pb.h"
#include "json2pb/pb_to_json.h"

namespace ecm {

inline std::string ToJsonString(const google::protobuf::Message &proto) {
  std::string json_data;
  // omit error check
  json2pb::ProtoMessageToJson(proto, &json_data);
  return json_data;
}

template <typename T>
std::string ToJsonString(const google::protobuf::RepeatedField<T> &proto) {
  return format("[{}]", absl::StrJoin(proto, ","));
}

template <typename T>
std::string ToJsonString(const google::protobuf::RepeatedPtrField<T> &proto) {
  return format("[{}]",
                absl::StrJoin(proto, ",", [](std::string *out, const T &item) {
                  out->append(ToJsonString(item));
                }));
}

inline Status FromJsonString(google::protobuf::Message *proto,
                             const std::string &json_data) {
  std::string error;
  if (!json2pb::JsonToProtoMessage(json_data, proto, &error)) {
    return ECM_ERROR(StatusCode::kInternal, "parse json `{}` failed: {}",
                     json_data, error);
  }
  return OkStatus();
}

} // namespace ecm