#include "elaborate.hpp"

#include <iostream>
#include <memory>
#include <mutex>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <sstream>
#include <iomanip>

#include "graph.hpp"
#include "slang/ast/Symbol.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/types/Type.h"

namespace wolf_sv {

Elaborate::Elaborate() = default;
Elaborate::~Elaborate() = default;

std::shared_ptr<Netlist> Elaborate::convert(const slang::ast::RootSymbol& root) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto netlist = std::make_shared<Netlist>();
  latestNetlist_ = netlist;
  instanceBodyGraphs_.clear();
  instanceStack_.clear();
  graphStack_.clear();

  for (const auto* instance : root.topInstances) {
    if (!instance || !instance->isModule()) {
      continue;
    }
    
    processTopModule(*instance);
    
  }

  return netlist;
}

Netlist& Elaborate::getNetlist() {
  if(auto locked = latestNetlist_.lock()){
    return *locked;
  } else {
    throw std::runtime_error("latest netlist has expired; call convert() before accessing it");
  }
}

void Elaborate::processTopModule(const slang::ast::InstanceSymbol& instance) {
  processInstance(instance);
}

void Elaborate::processInstance(const slang::ast::InstanceSymbol& instance) {
  instanceStack_.push_back(&instance);
  // step1: 处理 instanceBody，处理结果压入graphStack
  processInstanceBody();
  // step2：graph 已经创建好并放在栈顶，processInstanceConnection 在栈顶创建连接关系
  processInstanceConnection();
  graphStack_.pop_back();
  instanceStack_.pop_back();
}

void Elaborate::processInstanceBody() {
  // 解析 instanceBody，将解析后的 graph 压入栈顶
  const slang::ast::InstanceSymbol& instance = *(instanceStack_.back());
  Netlist& netlist = getNetlist();
  const auto* canonical = instance.getCanonicalBody();
  const auto* key = canonical ? canonical : &instance.body;

  if (!key) {
    throw std::logic_error("instance body pointer must not be null");
  }

  const auto it = instanceBodyGraphs_.find(key);
  if (it != instanceBodyGraphs_.end()) {
    // 这个 instanceBody 已经解析过，压栈返回即可
    graphStack_.push_back(it->second);
    return;
  }

  // 执行到此处，当前 InstanceBody 尚未解析过，创建一个新的图，压栈
  std::ostringstream nameBuilder;
  auto definitionName = instance.getDefinition().name;
  if (!definitionName.empty()) {
    nameBuilder << definitionName;
  }
  else if (!instance.name.empty()) {
    nameBuilder << instance.name;
  }
  else {
    nameBuilder << "anonymous";
  }
  nameBuilder << "@body_"
              << std::hex << reinterpret_cast<std::uintptr_t>(key);
  Graph& graph = netlist.createGraph(nameBuilder.str());
  instanceBodyGraphs_.emplace(key, &graph);
  graphStack_.push_back(&graph);

  // TODO：遍历 instance.body，对每种 symbol 进行分类处理

}

void Elaborate::processInstanceConnection() {
  if(instanceStack_.size() != graphStack_.size()){
    throw std::runtime_error("instanceStack size does not match graphStack");
  }
  if(graphStack_.size() < 2) {
    return;
  }

  // TODO: 处理连接关系
}

}  // namespace wolf_sv
