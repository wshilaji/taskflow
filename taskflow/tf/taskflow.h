#pragma once
#include <vector>
#include <memory>
#include <functional>
#include <butil/logging.h>
#include <atomic>
#include "bthread/bthread.h"
#include "bthread/countdown_event.h"
#include <bthread/butex.h>  // BRPC 公开的头文件

namespace tf {

class TaskFlow;
class Subflow;
enum NodeState {
    NORMAL = 0,
    CONDITIONED = 1 << 0
};

class Task {
public:
  using ConditionFn = std::function<std::vector<int>()>;
  using SubflowWork = std::function<void(Subflow&)>;
  
  template<typename F>
  Task(const std::string& name, F&& func, TaskFlow* flow)
      : name_(name), func_(std::forward<F>(func)), flow_(flow) {}

  void succeed(Task* next) {
    successors_.push_back(next);
    next->depend_count_++;
  }

  void precede(Task* next) {
    successors_.push_back(next);
    next->dependents_.push_back(this);
  }

  void name(const std::string& name) {
    name_ = name ;
  }

  void SetCondition(ConditionFn cond) {
    cond_ = std::move(cond);
    is_conditional_ = true;
  }
  bool IsConditioner() const { return is_conditional_; }

  void set_subflow_work(SubflowWork&& work) {
    subflow_work_ = std::move(work);
    is_subflow_ = true;
  }

  bool is_subflow() const { return is_subflow_; }

  void SetUpJoinCounter() {
    size_t c = 0;
    for (auto dep : dependents_) {
      if (dep->IsConditioner()) {
        state_.fetch_or(NodeState::CONDITIONED, std::memory_order_relaxed);
      } else {
        c++;
      }
    }
    depend_count_.store(c, std::memory_order_relaxed);
  }
  // Function: num_strong_dependents
  inline size_t NumStrongDependents() const {
    size_t n = 0;
    for(size_t i=0; i<dependents_.size(); i++) {
      if(!dependents_[i]->IsConditioner()) {
        n++;
      }
    }
    return n;
  }
  inline size_t NumDependents() const {
    return dependents_.size();
  }
  // 添加子流关联方法（非拥有指针）
  void link_subflow(std::shared_ptr<Subflow> subflow) {
    link_subflow_ = subflow; is_subflow_ = true;
  }

  Task& setEasyTask(bool easy) {
    easy_task_ = easy;
    return *this;
  }

  std::shared_ptr<Subflow> linked_subflow() const { return link_subflow_; }

  void Invoke();
  void SubmitToBthread(Task* task);
  static void* TaskExecutor(void* arg);

private:
  friend class TaskFlow;

  static void* BthreadFn(void* arg);

  std::string name_;
  std::function<void()> func_;
  TaskFlow* flow_;
  std::shared_ptr<Subflow> link_subflow_ ;  // 非拥有指针，仅表示关联关系
  
  bool is_conditional_ = false;
  ConditionFn cond_;

  SubflowWork subflow_work_;
  bool is_subflow_ = false;
  bool easy_task_ = false;
  std::atomic<int> depend_count_{0};
  std::atomic<int> state_ {0}; 
  // 双向关系维护
  std::vector<Task*> successors_;
  std::vector<Task*> dependents_;  // 前驱结点
};


class TopologyCountdown {
public:
  TopologyCountdown(int initial_count = 1) {
    if (initial_count < 0) {
      LOG(FATAL) << "Invalid initial_count=" << initial_count;
      abort();
    }
    _butex = bthread::butex_create_checked<int>();
    *_butex = initial_count;
    _wait_was_invoked = false;
  }
  ~TopologyCountdown() {}

  // 动态增加计数（允许在 wait() 后调用）
  void add_count(int v) {
    if (v <= 0) {
      LOG_IF(ERROR, v < 0) << "Invalid count=" << v;
      return;
    }
    ((butil::atomic<int>*)_butex)->fetch_add(v, butil::memory_order_release);
  }
  void signal(int sig) {
    // Have to save _butex, *this is probably defreferenced by the wait thread
    // which sees fetch_sub
    void* const saved_butex = _butex;
    const int prev = ((butil::atomic<int>*)_butex)
        ->fetch_sub(sig, butil::memory_order_release);
    // DON'T touch *this ever after
    if (prev > sig) {
      return;
    }
    LOG_IF(ERROR, prev < sig) << "Counter is over decreased";
    bthread::butex_wake_all(saved_butex);
  }
  int wait() {
    _wait_was_invoked = true;
    for (;;) {
      const int seen_counter =
        ((butil::atomic<int>*)_butex)->load(butil::memory_order_acquire);
      if (seen_counter <= 0) {
        return 0;
      }
      if (bthread::butex_wait(_butex, seen_counter, NULL) < 0 &&
        errno != EWOULDBLOCK && errno != EINTR) {
        return errno;
      }
    }
  }
  void reset(int v) {
    if (v < 0) {
      LOG(ERROR) << "Invalid count=" << v;
      return;
    }
    const int prev_counter =
      ((butil::atomic<int>*)_butex)
           ->exchange(v, butil::memory_order_release);
    LOG_IF(ERROR, _wait_was_invoked && prev_counter)
      << "Invoking reset() while count=" << prev_counter;
    _wait_was_invoked = false;
  }

private:
  int *_butex;// BRPC 轻量级同步原语
  bool _wait_was_invoked;
};

class TaskFlow {
public:
  explicit TaskFlow(const std::string& name);
  ~TaskFlow();

  template<typename F>
  Task* AddTask(const std::string& name, F&& func) {
    tasks_.emplace_back(new Task(name, std::forward<F>(func), this));
    return tasks_.back().get();
  }
  template<typename F>
  Task* emplace(F&& func) {
      return AddTask("anonymous", std::forward<F>(func));
  }
  Task* emplace(const std::string& name, F&& func) {
      return AddTask(name, std::forward<F>(func));
  }

  template<typename F, typename C>
  Task* AddConditionTask(const std::string& name, F&& func, C&& cond) {
    auto* t = AddTask(name, std::forward<F>(func));
    t->SetCondition(std::forward<C>(cond));
    return t;
  }
  template<typename F>
  Task* emplace_subflow(const std::string& name, F&& func) {
    auto* task = AddTask(name, []{});
    task->set_subflow_work(std::forward<F>(func));
    return task;
  }
  // 添加子流
  std::shared_ptr<Subflow> AddSubflow(std::string name) {
    auto sf = std::make_shared<Subflow>(std::move(name), this);
    subflows_.push_back(sf);
    return sf;
  }


  std::string ToDebugString() const ;
  void ToDebugStringInternal(std::stringstream& ss, const std::string& parent_cluster) const;
  TaskFlow& Run();
  TaskFlow& Wait();

private:
  friend class Task;
  friend class Subflow;
  //TaskFlow(const TaskFlow&) = delete;
  TaskFlow& operator=(const TaskFlow&) = delete;
  std::string name_;
  std::vector<std::unique_ptr<Task>> tasks_;
  bool is_running_ = false;
  TopologyCountdown completion_;
  // 存储子流的值（而非指针）
  std::vector<std::shared_ptr<Subflow>> subflows_;
};

class Subflow : public TaskFlow {
public:
  Subflow(const std::string& name, TaskFlow* parent) : TaskFlow(name), parent_(parent), joined_(false) {}
  void join() {
    if(!joined_) {
      this->Run().Wait(); // 执行并等待子流完成
      joined_ = true;
    }
  }
  bool joinable() const { return !joined_; }
  TaskFlow* parent() const { return parent_; }
private:
    friend class Task;
    friend class TaskFlow;

    TaskFlow* parent_;
    bool joined_ = false;
};

} // namespace lazyframe
