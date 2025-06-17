#include "taskflow.h"

namespace tf {


void Task::Invoke() {
  // 执行任务函数
  if (is_subflow_) {  //TODO  如果是retain() 用shared_ptr来保存下，如果是非retain() 直接一个栈变量 sf 运行
    // 在父流中永久存储子流
    auto sf = flow_->AddSubflow("subflow_of_" + name_);
    this->link_subflow(sf); // 建立双向关联
    subflow_work_(*sf); // 用户在此函数中构建子流
    if(sf->joinable()) {// 自动join未显式join的子流
      sf->join(); // 内部会调用Run().Wait()
    }
  } else {
    func_();
  }

  if((state_.load(std::memory_order_relaxed) & NodeState::CONDITIONED)) { //之所以在加一次是为了有while loop的情况。要将结点的入度还原回来
    depend_count_.fetch_add(NumStrongDependents(), std::memory_order_relaxed);
  } else {
    depend_count_.fetch_add(NumDependents(), std::memory_order_relaxed);
  }

  if (is_conditional_ && cond_) {
    // 条件分支：只执行选中的后继任务
    auto selected = cond_();
    for (int idx : selected) {
      if (idx >= 0 && idx < successors_.size()) {
        Task* next = successors_[idx];
        // 重置依赖计数并立即提交
        flow_->completion_.add_count(1); // 动态增加选中任务的计数
        next->depend_count_.store(0, std::memory_order_relaxed);
        SubmitToBthread(next);
      }
    }
  } else {
    // 普通分支：执行所有后继任务
    for (Task* next : successors_) {
      if (next->depend_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        flow_->completion_.add_count(1); // 动态增加选中任务的计数
        SubmitToBthread(next);
      }
    }
  }

  flow_->completion_.signal(1);
}

void* Task::TaskExecutor(void* arg) {
    Task* task = static_cast<Task*>(arg);
    task->Invoke();
    return nullptr;
}

void Task::SubmitToBthread(Task* task) {
  if (task->easy_task_) {
    task->Invoke();
  } else {
    bthread_t tid;
    if (bthread_start_background(&tid, nullptr, &TaskExecutor, task) != 0) {
        task->Invoke();
    }
  }
}

TaskFlow::TaskFlow(const std::string& name) 
  : name_(name), completion_(0) {}

TaskFlow::~TaskFlow() {
  Wait();
  tasks_.clear();
  subflows_.clear();
}

TaskFlow& TaskFlow::Run() {
  completion_.reset(0); // 初始化为 0 这里不要初始化成tasks_.size()
  // 找出所有初始任务(没有依赖的)
  for (auto& task : tasks_) {
    task->SetUpJoinCounter();
    if (task->dependents_.empty()) { //这里不要用 task->depend_count_ == 0 条件结点有前驱但是0 
      completion_.add_count(1); // 动态增加初始任务 
      task->SubmitToBthread(task.get());
    }
  }
  is_running_ = true;
  return *this;
}

TaskFlow& TaskFlow::Wait() {
  if (!is_running_) {
    return *this;
  }
  completion_.wait();
  is_running_ = false;
  return *this;
}
//dot -Tpng while.dot -o while.png
std::string TaskFlow::ToDebugString() const {
  std::stringstream ss;
  ss << "digraph G {\n";
  ss << "  rankdir=LR;\n";
  ss << "  compound=true;\n";  // 允许子图连接
  // 打印所有任务和子流
  ToDebugStringInternal(ss, "");
  ss << "}\n";
  return ss.str();
}

void TaskFlow::ToDebugStringInternal(std::stringstream& ss,
                                   const std::string& parent_cluster) const {
   // 当前流的集群定义
  std::string cluster_name = "cluster_" + name_;
  ss << "  subgraph " << cluster_name << " {\n";
  ss << "    label=\"" << name_ << "\";\n";
  ss << "    style=filled;\n";
  ss << "    color=lightgrey;\n";

  // 1. 打印当前流任务
  for (const auto& task : tasks_) {
    ss << "    \"" << task->name_ << "\"";
    if (task->IsConditioner()) {
      ss << " [shape=diamond, style=filled, fillcolor=lightblue]";
    } else if (task->dependents_.empty()) {
      ss << " [shape=box, style=filled, fillcolor=greenyellow]";
    } else if (task->is_subflow()) {
      ss << " [shape=ellipse, style=filled, fillcolor=orange]";
    }
    ss << ";\n";
  }
  ss << "  }\n";
  // 2. 打印任务依赖（主流程内部连接）
  for (const auto& task : tasks_) {
    for (auto succ : task->successors_) {
      // 仅当满足以下条件时才跳过连接：
      // - 当前任务是子流入口任务
      // - 后继任务是该子流的内部任务
      bool should_skip = false;
      if (task->is_subflow() && task->link_subflow_) {
        auto& subflow_tasks = task->link_subflow_->tasks_;
        should_skip = std::any_of(
          subflow_tasks.begin(),
          subflow_tasks.end(),
          [succ](const auto& t) { return t->name_ == succ->name_; }
        );
      }

      if (!should_skip) {
        ss << "  \"" << task->name_ << "\" -> \"" << succ->name_ << "\"";
        if (task->IsConditioner()) {
          ss << " [style=dashed]";
        }
        ss << ";\n";
      }
    }
  }

  // 3. 处理子流连接（保持不变）
  for (const auto& sf : subflows_) {
    std::string entry_node;
    for (const auto& task : tasks_) {
      if (task->is_subflow() && task->link_subflow_ == sf) {
        entry_node = task->name_;
        break;
      }
    }

    sf->ToDebugStringInternal(ss, cluster_name);

    if (!entry_node.empty() && !sf->tasks_.empty()) {
      // 主流程到子流的入口连接（关键！）
      ss << "  \"" << entry_node << "\" -> \""
         << sf->tasks_.front()->name_ << "\""
         << " [lhead=cluster_" << sf->name_ << "];\n";

      // 子流到主流程的出口连接
      for (auto succ : sf->tasks_.back()->successors_) {
        bool is_external = std::none_of(
          sf->tasks_.begin(),
          sf->tasks_.end(),
          [succ](const auto& t) { return t->name_ == succ->name_; }
        );
        if (is_external) {
          ss << "  \"" << sf->tasks_.back()->name_ << "\" -> \""
             << succ->name_ << "\" [ltail=cluster_" << sf->name_ << "];\n";
        }
      }
    }
  }

}

} // namespace 
