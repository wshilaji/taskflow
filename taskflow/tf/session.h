#pragma once

#include <string>

#include "ecm/taskflow/data.h"
#include "ecm/taskflow/frame_state.h"
#include "google/protobuf/arena.h"

namespace ecm::taskflow {

class Session {
public:
  virtual ~Session() = default;

  virtual void Reset() {
    ResetData();

    resource_manager_ = nullptr;
    exp_data_ = nullptr;
    arena_ = nullptr;
  }

  virtual void ResetData() {
    outputs_.clear();
    frame_states_.clear();
    data_map_.clear();
  }

  // 一般为场景名，用来做 metrics 打点
  const std::string &Name() const { return name_; }
  void SetName(const std::string &name) { name_ = name; }

  // 可以携带 request id、uid 等信息，用来将错误日志串起来
  const std::string &Id() const { return id_; }
  void SetId(const std::string &id) { id_ = id; }

  // @name: $input:name
  void SetInput(const std::string &name, std::unique_ptr<Data> data) {
    data_map_[name] = std::move(data);
  }

  // @outputs: {"output_node"}
  void SetOutputs(const std::vector<std::string> &outputs) {
    outputs_ = outputs;
  }

  // @name: "output_node:out_name"
  template <class T> Status GetOutput(const std::string &name, T **output) {
    auto d_it = data_map_.find(name);
    if (d_it == data_map_.end()) {
      return ECM_ERROR(StatusCode::kNotFound, "output {} not found", name);
    }
    *output = dynamic_cast<T *>(d_it->second.get());
    if (!*output) {
      return ECM_ERROR(StatusCode::kFailedPrecondition, "type cast failed");
    }
    return OkStatus();
  }

  void SetResourceManager(void *mgr) { resource_manager_ = mgr; }
  template <class T> T *GetResourceManager() { return (T *)resource_manager_; }

  void SetExpData(void *data) { exp_data_ = data; }
  template <class T> T *GetExpData() { return (T *)exp_data_; }

  void SetArena(google::protobuf::Arena *arena) { arena_ = arena; }
  google::protobuf::Arena *Arena() { return arena_; }

  std::string DebugString(const std::string &name) const {
    auto it = data_map_.find(name);
    if (it == data_map_.end()) {
      return "not found";
    }
    return it->second->DebugString();
  }

private:
  friend class OpKernel;
  friend class Executor;

  std::string name_;
  std::string id_;
  std::vector<std::string> outputs_;
  std::vector<std::unique_ptr<FrameState>> frame_states_; // 拓扑还是帧
  std::map<std::string, std::unique_ptr<Data>> data_map_;

  // pointers
  void *resource_manager_ = nullptr;
  void *exp_data_ = nullptr;
  google::protobuf::Arena *arena_ = nullptr;
};

} // namespace ecm::taskflow
