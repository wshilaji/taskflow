#include "ecm/taskflow/bthread/bthread_executor.h"

namespace ecm::taskflow {

void BThreadExecutor::Submit(std::function<Status()> func) {
  ctxs_.emplace_back(std::make_unique<Context>());
  auto ctx = ctxs_.back().get();

  schedule_.Submit([func, ctx]() {
    ctx->status = func();
    ctx->notification.Notify();
  });
}

Status BThreadExecutor::WaitAll() {
  for (auto &ctx : ctxs_) {
    ctx->notification.Wait();
  }
  for (auto &ctx : ctxs_) {
    if (!ctx->status.ok()) {
      return ctx->status;
    }
  }
  return OkStatus();
}

} // namespace ecm::taskflow