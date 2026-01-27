#ifndef WOLF_SV_CONVERT_HPP
#define WOLF_SV_CONVERT_HPP

#include "grh.hpp"
#include "slang/text/SourceLocation.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace slang {
class Compilation;
} // namespace slang

namespace slang::ast {
class RootSymbol;
class InstanceBodySymbol;
class Symbol;
} // namespace slang::ast

namespace wolf_sv_parser {

using ValueId = grh::ir::ValueId;
using OperationId = grh::ir::OperationId;
using SymbolId = grh::ir::SymbolId;

enum class ConvertDiagnosticKind {
    Todo,
    Error,
    Warning
};

struct ConvertDiagnostic {
    ConvertDiagnosticKind kind;
    std::string message;
    std::string originSymbol;
    std::optional<slang::SourceLocation> location;
};

class ConvertDiagnostics {
public:
    void todo(const slang::ast::Symbol& symbol, std::string message);
    void error(const slang::ast::Symbol& symbol, std::string message);
    void warn(const slang::ast::Symbol& symbol, std::string message);
    void todo(const slang::SourceLocation& location, std::string message,
              std::string originSymbol = {});
    void error(const slang::SourceLocation& location, std::string message,
               std::string originSymbol = {});
    void warn(const slang::SourceLocation& location, std::string message,
              std::string originSymbol = {});

    void setOnError(std::function<void()> callback) { onError_ = std::move(callback); }
    const std::vector<ConvertDiagnostic>& messages() const noexcept { return messages_; }
    bool empty() const noexcept { return messages_.empty(); }
    bool hasError() const noexcept { return hasError_; }

private:
    void add(ConvertDiagnosticKind kind, const slang::ast::Symbol& symbol,
             std::string message);
    void add(ConvertDiagnosticKind kind, std::string originSymbol,
             std::optional<slang::SourceLocation> location, std::string message);

    std::vector<ConvertDiagnostic> messages_;
    bool hasError_ = false;
    std::function<void()> onError_;
};

class ConvertAbort final : public std::exception {
public:
    const char* what() const noexcept override { return "convert aborted"; }
};

enum class ConvertLogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Off = 5
};

struct ConvertLogEvent {
    ConvertLogLevel level;
    std::string tag;
    std::string message;
};

class ConvertLogger {
public:
    using Sink = std::function<void(const ConvertLogEvent&)>;

    void setLevel(ConvertLogLevel level) noexcept { level_ = level; }
    void enable() noexcept { enabled_ = true; }
    void disable() noexcept { enabled_ = false; }
    void setSink(Sink sink) { sink_ = std::move(sink); }

    void allowTag(std::string_view tag);
    void clearTags();
    bool enabled(ConvertLogLevel level, std::string_view tag) const noexcept;
    void log(ConvertLogLevel level, std::string_view tag, std::string_view message);

private:
    bool enabled_ = false;
    ConvertLogLevel level_ = ConvertLogLevel::Warn;
    std::unordered_set<std::string> tags_{};
    Sink sink_{};
};

struct ConvertOptions {
    bool abortOnError = true;
    bool enableLogging = false;
    ConvertLogLevel logLevel = ConvertLogLevel::Warn;
};

struct ConvertContext {
    const slang::Compilation* compilation = nullptr;
    const slang::ast::RootSymbol* root = nullptr;
    ConvertOptions options{};
    ConvertDiagnostics* diagnostics = nullptr;
    ConvertLogger* logger = nullptr;
};

enum class PortDirection {
    Input,
    Output,
    Inout
};

enum class SignalKind {
    Net,
    Variable,
    Memory,
    Port
};

enum class ControlDomain {
    Combinational,
    Sequential,
    Latch,
    Unknown
};

struct PortInfo {
    std::string name;
    PortDirection direction = PortDirection::Input;
    int32_t width = 0;
    bool isSigned = false;
    std::string inName;
    std::string outName;
    std::string oeName;
};

struct SignalInfo {
    std::string name;
    SignalKind kind = SignalKind::Net;
    int32_t width = 0;
    bool isSigned = false;
    int64_t memoryRows = 0;
    std::vector<int32_t> packedDims;
    std::vector<int32_t> unpackedDims;
};

struct RWOp {
    std::string target;
    ControlDomain domain = ControlDomain::Unknown;
    bool isWrite = false;
};

struct MemoryPortInfo {
    std::string memory;
    bool isRead = false;
    bool isWrite = false;
    bool isMasked = false;
    bool isSync = false;
    bool hasReset = false;
};

struct InstanceInfo {
    std::string instanceName;
    std::string moduleName;
    bool isBlackbox = false;
};

struct ModulePlan {
    const slang::ast::InstanceBodySymbol* body = nullptr;
    std::string symbol;
    std::vector<PortInfo> ports;
    std::vector<SignalInfo> signals;
    std::vector<RWOp> rwOps;
    std::vector<MemoryPortInfo> memPorts;
    std::vector<InstanceInfo> instances;
};

class ModulePlanner {
public:
    explicit ModulePlanner(ConvertContext& context) : context_(context) {}

    ModulePlan plan(const slang::ast::InstanceBodySymbol& body);

private:
    ConvertContext& context_;
};

class GraphAssembler {
public:
    explicit GraphAssembler(ConvertContext& context, grh::ir::Netlist& netlist)
        : context_(context), netlist_(netlist) {}

    grh::ir::Graph& build(const ModulePlan& plan);

private:
    ConvertContext& context_;
    grh::ir::Netlist& netlist_;
    std::size_t nextAnonymousId_ = 0;
};

class ConvertDriver {
public:
    explicit ConvertDriver(ConvertOptions options = {});

    grh::ir::Netlist convert(const slang::ast::RootSymbol& root);

    ConvertDiagnostics& diagnostics() noexcept { return diagnostics_; }
    ConvertLogger& logger() noexcept { return logger_; }

private:
    ConvertOptions options_{};
    ConvertDiagnostics diagnostics_{};
    ConvertLogger logger_{};
};

} // namespace wolf_sv_parser

#endif // WOLF_SV_CONVERT_HPP
