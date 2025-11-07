#pragma once

#include "grh.hpp"

/** \file elaborate.hpp
 *  \brief Entry points for converting slang AST into the GRH representation.
 *
 *  Stage 2 focuses on wiring up a minimal pipeline that can traverse the slang
 *  AST, populate GRH graph placeholders, and surface diagnostic messages for
 *  unimplemented features.
 */

#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "slang/text/SourceLocation.h"

namespace slang::ast {
class InstanceSymbol;
class InstanceArraySymbol;
class InstanceBodySymbol;
class ValueSymbol;
class Expression;
class RootSymbol;
class Symbol;
} // namespace slang::ast

namespace wolf_sv {

/// Diagnostic categories emitted by the elaboration pipeline.
enum class ElaborateDiagnosticKind {
    Todo,
    NotYetImplemented
};

/// A diagnostic message generated during elaboration.
struct ElaborateDiagnostic {
    ElaborateDiagnosticKind kind;
    std::string message;
    std::string originSymbol;
    std::optional<slang::SourceLocation> location;
};

/// Collects diagnostic messages generated during elaboration.
class ElaborateDiagnostics {
public:
    void todo(const slang::ast::Symbol& symbol, std::string message);
    void nyi(const slang::ast::Symbol& symbol, std::string message);

    const std::vector<ElaborateDiagnostic>& messages() const noexcept { return messages_; }
    bool empty() const noexcept { return messages_.empty(); }

private:
    void add(ElaborateDiagnosticKind kind, const slang::ast::Symbol& symbol,
             std::string message);

    std::vector<ElaborateDiagnostic> messages_;
};

/// Options controlling the elaboration pipeline behaviour.
struct ElaborateOptions {
    /// When true, create placeholder operations describing the module body.
    bool emitPlaceholders = true;
};

/// Elaborates slang AST into GRH representation.
class Elaborate {
public:
    explicit Elaborate(ElaborateDiagnostics* diagnostics = nullptr,
                       ElaborateOptions options = {});

    /// Convert the provided slang AST root symbol into a GRH netlist.
    grh::Netlist convert(const slang::ast::RootSymbol& root);

private:
    grh::Graph* materializeGraph(const slang::ast::InstanceSymbol& instance,
                                 grh::Netlist& netlist, bool& wasCreated);
    void populatePorts(const slang::ast::InstanceSymbol& instance, grh::Graph& graph);
    void emitModulePlaceholder(const slang::ast::InstanceSymbol& instance, grh::Graph& graph);
    void convertInstanceBody(const slang::ast::InstanceSymbol& instance, grh::Graph& graph,
                             grh::Netlist& netlist);
    void processInstanceArray(const slang::ast::InstanceArraySymbol& array, grh::Graph& graph,
                              grh::Netlist& netlist);
    void createInstanceOperation(const slang::ast::InstanceSymbol& childInstance,
                                 grh::Graph& parentGraph, grh::Graph& targetGraph);
    grh::Value* ensureValueForSymbol(const slang::ast::ValueSymbol& symbol, grh::Graph& graph);
    grh::Value* resolveConnectionValue(const slang::ast::Expression& expr, grh::Graph& graph,
                                       const slang::ast::Symbol* origin);
    std::string makeUniqueOperationName(grh::Graph& graph, std::string baseName);
    void registerValueForSymbol(const slang::ast::Symbol& symbol, grh::Value& value);

    ElaborateDiagnostics* diagnostics_;
    ElaborateOptions options_;
    std::size_t placeholderCounter_ = 0;
    std::size_t instanceCounter_ = 0;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, grh::Graph*> graphByBody_;
    std::unordered_set<const slang::ast::InstanceBodySymbol*> processedBodies_;
    std::unordered_map<const slang::ast::Symbol*, grh::Value*> valueCache_;
    std::unordered_map<std::string, std::size_t> graphNameUsage_;
};

} // namespace wolf_sv
