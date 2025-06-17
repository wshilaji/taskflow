#pragma once

#include "bthread_notification.h"
#include "bthread_schedule.h"
#include "core/status.h"

namespace abel {

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
