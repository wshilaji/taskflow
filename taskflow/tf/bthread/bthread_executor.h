#pragma once

#include "ecm/taskflow/bthread/bthread_notification.h"
#include "ecm/taskflow/bthread/bthread_schedule.h"

namespace ecm::taskflow {

class BThreadExecutor {
public:
  void Submit(std::function<Status()> func);
  Status WaitAll();

private:
  struct Context {
    Status status;
    BThreadNotification notification;
  };

  BThreadSchedule schedule_;
  std::vector<std::unique_ptr<Context>> ctxs_;
};

} // namespace ecm::taskflow