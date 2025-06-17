#pragma once

#include "bthread/bthread.h"

namespace abel {
class Schedule {
public:
  virtual ~Schedule() {}

  virtual void Submit(std::function<void()> func) = 0;

private:
};

class BThreadSchedule : public Schedule {
public:
  struct Context {
    std::function<void()> func;
  };

  void Submit(std::function<void()> func) override {
    auto ctx = new Context;
    ctx->func = func;
    bthread_t tid;
    auto rc = bthread_start_background(
        &tid, nullptr,
        [](void *arg) -> void * {
          auto ctx = reinterpret_cast<Context *>(arg);
          ctx->func();
          delete ctx;
          return nullptr;
        },
        ctx);
    if (rc != 0) {
      // start brpc failed, fallback in current thread
      func();
      delete ctx;
    }
  }
};

} // namespace ecm::taskflow
