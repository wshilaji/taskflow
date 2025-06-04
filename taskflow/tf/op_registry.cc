#include "ecm/taskflow/op_registry.h"

namespace ecm::taskflow {

OpRegistry *OpRegistry::Default() {
  static OpRegistry registry;
  return &registry;
}

} // namespace ecm::taskflow