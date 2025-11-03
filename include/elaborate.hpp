#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace slang::ast {
class RootSymbol;
class InstanceSymbol;
class InstanceBodySymbol;
}  // namespace slang::ast

namespace wolf_sv {

class Netlist;
class Graph;

class Elaborate {
public:
  Elaborate();
  ~Elaborate();

  Elaborate(const Elaborate&) = delete;
  Elaborate& operator=(const Elaborate&) = delete;

  std::shared_ptr<Netlist> convert(const slang::ast::RootSymbol& root);

private:
  using BodyGraphMap =
      std::unordered_map<const slang::ast::InstanceBodySymbol*, Graph*>;

  std::mutex mutex_;
  std::weak_ptr<Netlist> latestNetlist_;
  Netlist& getNetlist();
  BodyGraphMap instanceBodyGraphs_;

  std::vector<const slang::ast::InstanceSymbol*> instanceStack_;
  std::vector<Graph*> graphStack_;

  void processTopModule(const slang::ast::InstanceSymbol& instance);
  void processInstance(const slang::ast::InstanceSymbol& instance);
  void processInstanceBody();
  void processInstanceConnection();
};

}  // namespace wolf_sv
