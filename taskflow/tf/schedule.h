#pragma once

#include <functional>

#include "ecm/core/status.h"

namespace ecm::taskflow {

class Schedule {
public:
  virtual ~Schedule() {}

  virtual void Submit(std::function<void()> func) = 0;

private:
};

} // namespace ecm::taskflow