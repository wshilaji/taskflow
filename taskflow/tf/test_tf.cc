#include <bits/stdc++.h>
#include "taskflow.h"
#include <common/version/version.h>
#include <common/main/main.h>

using namespace tf;

int Main(const std::vector<const char*>& args) {
  // 修正1：提供TaskFlow名称
  tf::TaskFlow flow("TestFlow");

  auto* A = flow.AddTask("A", []{
      std::cout << "Task A\n";
  });

  // 修正2&3：明确lambda返回类型
  auto* cond = flow.AddConditionTask("Cond",
      []() { std::cout << " cond check\n"; },  
      []() -> std::vector<int> { std::cout << " select first\n"; return {1}; });  // 选择第一个后继

  auto* B = flow.AddTask("B", []{
      std::cout << "Task B\n";
  });
  auto* C = flow.AddTask("C", []{
      std::cout << "Task C\n";
  });
  auto* D = flow.AddTask("D", []{
      std::cout << "Task D\n";
  });
  

  // 建立任务关系
  A->precede(cond);
  cond->precede(B);
  cond->precede(C);
  C->precede(D);

  //A->precede(B);
  //B->precede(C);
  std::cout << flow.ToDebugString() << std::endl;

  flow.Run();
  flow.Wait();
  return 0;
}
int Main01(const std::vector<const char*>& args) {
    tf::TaskFlow main_flow("main_flow");

    // 创建子流任务
    auto subflow_task = main_flow.emplace_subflow("SubflowTask",
        [](tf::Subflow& sf) {
            auto a = sf.AddTask("SubflowTaskA",[](){
                LOG(INFO) << "Subflow task A";
            });

            auto b = sf.AddTask("SubflowTaskB",[](){
                LOG(INFO) << "Subflow task B";
            });

            b->precede(a);

            // 可选择显式调用join或自动join
            // sf.join();
        });

    // 主流程任务
    auto main_task = main_flow.AddTask("MainTask",[](){
        LOG(INFO) << "Main task";
    });

    // 建立依赖关系
    subflow_task->precede(main_task);

    // 执行流程
    main_flow.Run().Wait();

    std::cout << main_flow.ToDebugString() << std::endl;

    return 0;
}
BUSINESS_REGISTER_INITIALIZER(&Main, RANK_MAIN);
