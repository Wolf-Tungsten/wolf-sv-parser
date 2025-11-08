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
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "slang/text/SourceLocation.h"

namespace slang::ast {
class InstanceSymbol;
class InstanceArraySymbol;
class InstanceBodySymbol;
class ValueSymbol;
class Type;
class GenerateBlockSymbol;
class GenerateBlockArraySymbol;
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

/// Captures a flattened field inside a memoized signal.
struct SignalMemoField {
    std::string path;
    int64_t msb = 0;
    int64_t lsb = 0;
    bool isSigned = false;
};

/// Captures a memoized signal entry discovered during elaboration.
struct SignalMemoEntry {
    const slang::ast::ValueSymbol* symbol = nullptr;
    const slang::ast::Type* type = nullptr;
    int64_t width = 0;
    bool isSigned = false;
    std::vector<SignalMemoField> fields;
};

/// Elaborates slang AST into GRH representation.
class Elaborate {
public:
    explicit Elaborate(ElaborateDiagnostics* diagnostics = nullptr,
                       ElaborateOptions options = {});

    /// Convert the provided slang AST root symbol into a GRH netlist.
    grh::Netlist convert(const slang::ast::RootSymbol& root);

    /// Returns memoized net declarations for the provided module body.
    std::span<const SignalMemoEntry>
    peekNetMemo(const slang::ast::InstanceBodySymbol& body) const;

    /// Returns memoized register declarations for the provided module body.
    std::span<const SignalMemoEntry>
    peekRegMemo(const slang::ast::InstanceBodySymbol& body) const;

private:
    grh::Graph* materializeGraph(const slang::ast::InstanceSymbol& instance,
                                 grh::Netlist& netlist, bool& wasCreated);
    void populatePorts(const slang::ast::InstanceSymbol& instance,
                       const slang::ast::InstanceBodySymbol& body, grh::Graph& graph);
    void emitModulePlaceholder(const slang::ast::InstanceSymbol& instance, grh::Graph& graph);
    void convertInstanceBody(const slang::ast::InstanceSymbol& instance, grh::Graph& graph,
                             grh::Netlist& netlist);
    void processInstanceArray(const slang::ast::InstanceArraySymbol& array, grh::Graph& graph,
                              grh::Netlist& netlist);
    void processGenerateBlock(const slang::ast::GenerateBlockSymbol& block, grh::Graph& graph,
                              grh::Netlist& netlist);
    void processGenerateBlockArray(const slang::ast::GenerateBlockArraySymbol& array,
                                   grh::Graph& graph, grh::Netlist& netlist);
    void processInstance(const slang::ast::InstanceSymbol& childInstance, grh::Graph& parentGraph,
                         grh::Netlist& netlist);
    void createInstanceOperation(const slang::ast::InstanceSymbol& childInstance,
                                 grh::Graph& parentGraph, grh::Graph& targetGraph);
    grh::Value* ensureValueForSymbol(const slang::ast::ValueSymbol& symbol, grh::Graph& graph);
    grh::Value* resolveConnectionValue(const slang::ast::Expression& expr, grh::Graph& graph,
                                       const slang::ast::Symbol* origin);
    std::string makeUniqueOperationName(grh::Graph& graph, std::string baseName);
    void registerValueForSymbol(const slang::ast::Symbol& symbol, grh::Value& value);
    void collectSignalMemos(const slang::ast::InstanceBodySymbol& body);

    ElaborateDiagnostics* diagnostics_;
    ElaborateOptions options_;
    std::size_t placeholderCounter_ = 0;
    std::size_t instanceCounter_ = 0;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, grh::Graph*> graphByBody_;
    std::unordered_set<const slang::ast::InstanceBodySymbol*> processedBodies_;
    std::unordered_map<const slang::ast::Symbol*, grh::Value*> valueCache_;
    std::unordered_map<std::string, std::size_t> graphNameUsage_;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, std::vector<SignalMemoEntry>>
        netMemo_;
    std::unordered_map<const slang::ast::InstanceBodySymbol*, std::vector<SignalMemoEntry>>
        regMemo_;
};

} // namespace wolf_sv
