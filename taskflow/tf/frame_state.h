#pragma once

#include <atomic>

#include "ecm/core/status.h"
#include "ecm/taskflow/data.h"
#include "ecm/taskflow/notification.h"

namespace ecm::taskflow {

struct Node;

struct FrameState {
  bool IsReady() { return ready.load(std::memory_order_acquire); }
  bool SetReady() {
    bool not_ready = false;
    return ready.compare_exchange_strong(not_ready, true,
                                         std::memory_order_acq_rel);
  }

  // schduled to ready
  std::atomic_bool ready = false;

  // done
  bool is_inline = false;
  std::unique_ptr<Notification> sched_done;
  std::unique_ptr<Notification> node_done;
  Status status;

  // stats
  const Node *node = nullptr;
  int64_t cost_ms = 0;
};

} // namespace ecm::taskflow