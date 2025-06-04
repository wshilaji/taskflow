#pragma once

namespace ecm::taskflow {

struct Notification {
  virtual ~Notification() {}

  virtual Notification *New() = 0;

  virtual void Wait() = 0;

  virtual void Notify() = 0;

  virtual bool HasBeenNotified() const = 0;
};

} // namespace ecm::taskflow