#pragma once

#include <iostream>
#include <sstream>
#include <exception>

#include "../utility/stream.hpp"

namespace tf {

// node-specific states
struct NSTATE {

  using underlying_type = int;

  constexpr static underlying_type NONE           = 0x00000000;  
  constexpr static underlying_type CONDITIONED    = 0x10000000;   // 表示处于条件状态   始终是高位不变 低位置+1
                                                                  // nstate = (0 + 1) | 0x10000000 = 1 | 0x10000000 = 0x10000001
                                                                  // nstate = (0x10000001 + 1) | 0x10000000 = 0x10000002 | 0x10000000 = 0x10000002
                                                                  // nstate = (0x10000002 + 1) | 0x10000000 = 0x10000003 | 0x10000000 = 0x10000003
  constexpr static underlying_type PREEMPTED      = 0x20000000;  // 表示被抢占状态
  constexpr static underlying_type RETAIN_SUBFLOW = 0x40000000;  // 表示保持子流状态。
  constexpr static underlying_type JOINED_SUBFLOW = 0x80000000; // 表示已加入子流状态

  // mask to isolate state bits - non-state bits store # weak dependents
  constexpr static underlying_type MASK        = 0xF0000000;
};

using nstate_t = NSTATE::underlying_type;

// exception-specific states
struct ESTATE {
  
  using underlying_type = int;  
  
  constexpr static underlying_type NONE      = 0x00000000; 
  constexpr static underlying_type EXCEPTION = 0x10000000; // 表示发生异常。
  constexpr static underlying_type CANCELLED = 0x20000000; // 表示已取消。
  constexpr static underlying_type ANCHORED  = 0x40000000;  // 表示已固定状态。anchor
};

using estate_t = ESTATE::underlying_type;

// async-specific states
struct ASTATE {
  
  using underlying_type = int;

  constexpr static underlying_type UNFINISHED = 0; // 表示未完成状态
  constexpr static underlying_type LOCKED     = 1; // 表示已锁定状态。
  constexpr static underlying_type FINISHED   = 2; // 表示已完成状态。
};

using astate_t = ASTATE::underlying_type;

// Procedure: throw_re
// Throws runtime error under a given error code.
template <typename... ArgsT>
//void throw_se(const char* fname, const size_t line, Error::Code c, ArgsT&&... args) {
void throw_re(const char* fname, const size_t line, ArgsT&&... args) {
  std::ostringstream oss;
  oss << "[" << fname << ":" << line << "] ";
  //ostreamize(oss, std::forward<ArgsT>(args)...);
  (oss << ... << args);
#ifdef TF_DISABLE_EXCEPTION_HANDLING
  std::cerr << oss.str();
  std::terminate();
#else
  throw std::runtime_error(oss.str());
#endif
}

}  // ------------------------------------------------------------------------

#define TF_THROW(...) tf::throw_re(__FILE__, __LINE__, __VA_ARGS__);

