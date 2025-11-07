#include "elaborate.hpp"

#include <limits>
#include <sstream>
#include <string>
#include <string_view>

#include "slang/ast/Symbol.h"
#include "slang/ast/SemanticFacts.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/ast/types/Type.h"

namespace wolf_sv {

namespace {

std::string deriveSymbolPath(const slang::ast::Symbol& symbol) {
    std::string path = symbol.getHierarchicalPath();
    if (!path.empty()) {
        return path;
    }
    if (!symbol.name.empty()) {
        return std::string(symbol.name);
    }
    return "<anonymous>";
}

int64_t clampBitWidth(uint64_t width, ElaborateDiagnostics* diagnostics,
                      const slang::ast::Symbol& symbol) {
    if (width == 0) {
        if (diagnostics) {
            diagnostics->nyi(symbol, "Port has indeterminate width; treating as 1-bit placeholder");
        }
        return 1;
    }

    constexpr auto maxValue = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    if (width > maxValue) {
        if (diagnostics) {
            diagnostics->nyi(symbol,
                             "Port width exceeds GRH limit; clamping to int64_t::max width");
        }
        return std::numeric_limits<int64_t>::max();
    }
    return static_cast<int64_t>(width);
}

void handleUnsupportedPort(const slang::ast::Symbol& symbol, std::string_view description,
                            ElaborateDiagnostics* diagnostics) {
    if (diagnostics) {
        std::string message = "Unsupported port form: ";
        message.append(description);
        diagnostics->nyi(symbol, message);
    }
}

} // namespace

void ElaborateDiagnostics::todo(const slang::ast::Symbol& symbol, std::string message) {
    add(ElaborateDiagnosticKind::Todo, symbol, std::move(message));
}

void ElaborateDiagnostics::nyi(const slang::ast::Symbol& symbol, std::string message) {
    add(ElaborateDiagnosticKind::NotYetImplemented, symbol, std::move(message));
}

void ElaborateDiagnostics::add(ElaborateDiagnosticKind kind, const slang::ast::Symbol& symbol,
                               std::string message) {
    ElaborateDiagnostic diagnostic{
        .kind = kind,
        .message = std::move(message),
        .originSymbol = deriveSymbolPath(symbol),
        .location = symbol.location.valid() ? std::optional(symbol.location) : std::nullopt};
    messages_.push_back(std::move(diagnostic));
}

Elaborate::Elaborate(ElaborateDiagnostics* diagnostics, ElaborateOptions options) :
    diagnostics_(diagnostics), options_(options) {}

grh::Netlist Elaborate::convert(const slang::ast::RootSymbol& root) {
    grh::Netlist netlist;

    for (const slang::ast::InstanceSymbol* topInstance : root.topInstances) {
        if (!topInstance) {
            continue;
        }

        if (!topInstance->isModule()) {
            if (diagnostics_) {
                diagnostics_->nyi(*topInstance, "Only module instances are supported as top level");
            }
            continue;
        }

        bool newlyCreated = false;
        grh::Graph* graph = materializeGraph(*topInstance, netlist, newlyCreated);
        if (!graph) {
            continue;
        }

        if (newlyCreated) {
            populatePorts(*topInstance, *graph);
        }
        netlist.markAsTop(graph->name());
        if (newlyCreated) {
            emitModulePlaceholder(*topInstance, *graph);
        }

        if (diagnostics_) {
            diagnostics_->todo(topInstance->body,
                               "Module body elaboration not implemented; emitted placeholder");
        }
    }

    return netlist;
}

grh::Graph* Elaborate::materializeGraph(const slang::ast::InstanceSymbol& instance,
                                        grh::Netlist& netlist, bool& wasCreated) {
    const auto& definition = instance.body.getDefinition();
    std::string graphName;
    if (!definition.name.empty()) {
        graphName.assign(definition.name);
    } else if (!instance.name.empty()) {
        graphName.assign(instance.name);
    } else {
        graphName = instance.getHierarchicalPath();
        if (graphName.empty()) {
            graphName = "_anonymous_module";
        }
    }

    if (auto* existing = netlist.findGraph(graphName)) {
        wasCreated = false;
        return existing;
    }

    grh::Graph& graph = netlist.createGraph(graphName);
    wasCreated = true;
    return &graph;
}

void Elaborate::populatePorts(const slang::ast::InstanceSymbol& instance, grh::Graph& graph) {
    const auto& body = instance.body;

    for (const slang::ast::Symbol* portSymbol : body.getPortList()) {
        if (!portSymbol) {
            continue;
        }

        if (const auto* port = portSymbol->as_if<slang::ast::PortSymbol>()) {
            if (port->isNullPort) {
                handleUnsupportedPort(*port, "null ports are not supported", diagnostics_);
                continue;
            }

            if (port->name.empty()) {
                handleUnsupportedPort(*port, "anonymous ports are not supported", diagnostics_);
                continue;
            }

            const slang::ast::Type& type = port->getType();
            if (!type.isFixedSize() || !type.isBitstreamType()) {
                if (diagnostics_) {
                    std::ostringstream oss;
                    oss << "Port type is not a fixed-size bitstream (type: " << type.toString()
                        << "); using placeholder width";
                    diagnostics_->nyi(*port, oss.str());
                }
            }

            int64_t width = clampBitWidth(type.getBitstreamWidth(), diagnostics_, *port);
            bool isSigned = type.isSigned();

            std::string portName(port->name);
            grh::Value& value = graph.createValue(portName, width, isSigned);

            switch (port->direction) {
            case slang::ast::ArgumentDirection::In:
                graph.bindInputPort(portName, value);
                break;
            case slang::ast::ArgumentDirection::Out:
                graph.bindOutputPort(portName, value);
                break;
            case slang::ast::ArgumentDirection::InOut:
            case slang::ast::ArgumentDirection::Ref:
                handleUnsupportedPort(*port,
                                      std::string("direction ") +
                                          std::string(slang::ast::toString(port->direction)),
                                      diagnostics_);
                // Leave as internal signal placeholder.
                break;
            default:
                handleUnsupportedPort(*port, "unknown direction", diagnostics_);
                break;
            }
            continue;
        }

        if (const auto* multi = portSymbol->as_if<slang::ast::MultiPortSymbol>()) {
            handleUnsupportedPort(*multi, "multi-port aggregations", diagnostics_);
            continue;
        }

        if (const auto* iface = portSymbol->as_if<slang::ast::InterfacePortSymbol>()) {
            handleUnsupportedPort(*iface, "interface ports", diagnostics_);
            continue;
        }

        handleUnsupportedPort(*portSymbol, "unhandled symbol kind", diagnostics_);
    }
}

void Elaborate::emitModulePlaceholder(const slang::ast::InstanceSymbol& instance,
                                      grh::Graph& graph) {
    if (!options_.emitPlaceholders) {
        return;
    }

    std::string opName = "_module_placeholder";
    if (placeholderCounter_ > 0) {
        opName.append("_");
        opName.append(std::to_string(placeholderCounter_));
    }
    ++placeholderCounter_;

    grh::Operation& op = graph.createOperation(grh::OperationKind::kBlackbox, opName);

    const auto& definition = instance.body.getDefinition();
    std::string moduleName = !definition.name.empty() ? std::string(definition.name)
                                                      : std::string(instance.name);
    if (moduleName.empty()) {
        moduleName = "anonymous_module";
    }

    op.setAttribute("module_name", moduleName);
    op.setAttribute("status", std::string("TODO: module body elaboration pending"));
}

} // namespace wolf_sv
