#pragma once

#include <cassert>

#include "bthread/condition_variable.h"
#include "bthread/mutex.h"
#include "ecm/taskflow/notification.h"

namespace ecm::taskflow {

class BThreadNotification : public Notification {
public:
  BThreadNotification() : notified_(false) {}
  ~BThreadNotification() {
    // In case the notification is being used to synchronize its own deletion,
    // force any prior notifier to leave its critical section before the object
    // is destroyed.
    std::unique_lock<bthread::Mutex> l(mu_);
  }

  Notification *New() override { return new BThreadNotification; }

  void Wait() override {
    if (!HasBeenNotified()) {
      std::unique_lock<bthread::Mutex> l(mu_);
      while (!HasBeenNotified()) {
        cv_.wait(l);
      }
    }
  }

  void Notify() override {
    std::unique_lock<bthread::Mutex> l(mu_);
    assert(!HasBeenNotified());
    notified_.store(true, std::memory_order_release);
    cv_.notify_all();
  }

  bool HasBeenNotified() const override {
    return notified_.load(std::memory_order_acquire);
  }

private:
  bthread::Mutex mu_;
  bthread::ConditionVariable cv_;
  std::atomic_bool notified_;
};

} // namespace ecm::taskflow