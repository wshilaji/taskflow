#pragma once

#include "ecm/core/status.h"
#include "ecm/taskflow/executor.h"

namespace ecm::taskflow {

class Loader {
public:
  Status Init(const std::string &graph_path);

  Executor *Get(const std::string &name) const {
    auto it = index_.find(name);
    if (it == index_.end()) {
      return nullptr;
    }
    return it->second.get();
  }

private:
  std::map<std::string, std::unique_ptr<Executor>> index_;
};

} // namespace ecm::taskflow