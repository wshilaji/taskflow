#pragma once

#include <functional>
#include <map>
#include <string>

namespace ecm::taskflow {

struct OpKernelConstruction;
class OpKernel;

using OpKernelCreateFunc = std::function<OpKernel *(OpKernelConstruction *)>;

class OpRegistry {
public:
  static OpRegistry *Default();

  OpKernel *Create(const std::string &name, OpKernelConstruction *ctx) const {
    auto it = ops_map_.find(name);
    if (it == ops_map_.end()) {
      return nullptr;
    }
    return it->second(ctx);
  }

  void Register(const std::string &name, OpKernelCreateFunc fn) {
    ops_map_[name] = fn;
  }

private:
  std::map<std::string, OpKernelCreateFunc> ops_map_;
};

struct OpRegistar {
  OpRegistar(const std::string &name, OpKernelCreateFunc fn) {
    OpRegistry::Default()->Register(name, fn);
  }
};

} // namespace ecm::taskflow