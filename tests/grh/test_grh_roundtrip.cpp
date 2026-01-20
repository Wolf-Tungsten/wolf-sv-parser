#include "grh.hpp"

#include <iostream>
#include <optional>
#include <string>

using namespace wolf_sv::grh;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[grh_roundtrip_tests] " << message << '\n';
        return 1;
    }

    bool compareSrcLoc(const std::optional<SrcLoc> &lhs, const std::optional<SrcLoc> &rhs)
    {
        if (lhs.has_value() != rhs.has_value())
        {
            return false;
        }
        if (!lhs)
        {
            return true;
        }
        return lhs->file == rhs->file &&
               lhs->line == rhs->line &&
               lhs->column == rhs->column &&
               lhs->endLine == rhs->endLine &&
               lhs->endColumn == rhs->endColumn;
    }

    std::string compareViews(const ir::GraphView &lhs, const ir::GraphView &rhs)
    {
        auto lhsValues = lhs.values();
        auto rhsValues = rhs.values();
        if (lhsValues.size() != rhsValues.size())
        {
            return "value count mismatch";
        }
        for (std::size_t i = 0; i < lhsValues.size(); ++i)
        {
            if (lhsValues[i] != rhsValues[i])
            {
                return "value order mismatch";
            }
        }

        auto lhsOps = lhs.operations();
        auto rhsOps = rhs.operations();
        if (lhsOps.size() != rhsOps.size())
        {
            return "operation count mismatch";
        }
        for (std::size_t i = 0; i < lhsOps.size(); ++i)
        {
            if (lhsOps[i] != rhsOps[i])
            {
                return "operation order mismatch";
            }
        }

        auto lhsInPorts = lhs.inputPorts();
        auto rhsInPorts = rhs.inputPorts();
        if (lhsInPorts.size() != rhsInPorts.size())
        {
            return "input port count mismatch";
        }
        for (std::size_t i = 0; i < lhsInPorts.size(); ++i)
        {
            if (lhsInPorts[i].name != rhsInPorts[i].name || lhsInPorts[i].value != rhsInPorts[i].value)
            {
                return "input port mismatch";
            }
        }

        auto lhsOutPorts = lhs.outputPorts();
        auto rhsOutPorts = rhs.outputPorts();
        if (lhsOutPorts.size() != rhsOutPorts.size())
        {
            return "output port count mismatch";
        }
        for (std::size_t i = 0; i < lhsOutPorts.size(); ++i)
        {
            if (lhsOutPorts[i].name != rhsOutPorts[i].name || lhsOutPorts[i].value != rhsOutPorts[i].value)
            {
                return "output port mismatch";
            }
        }

        for (std::size_t i = 0; i < lhsOps.size(); ++i)
        {
            auto opId = lhsOps[i];
            if (lhs.opKind(opId) != rhs.opKind(opId))
            {
                return "opKind mismatch";
            }
            if (lhs.opSymbol(opId) != rhs.opSymbol(opId))
            {
                return "opSymbol mismatch";
            }
            if (!compareSrcLoc(lhs.opSrcLoc(opId), rhs.opSrcLoc(opId)))
            {
                return "opSrcLoc mismatch";
            }

            auto lhsOperands = lhs.opOperands(opId);
            auto rhsOperands = rhs.opOperands(opId);
            if (lhsOperands.size() != rhsOperands.size())
            {
                return "opOperands size mismatch";
            }
            for (std::size_t j = 0; j < lhsOperands.size(); ++j)
            {
                if (lhsOperands[j] != rhsOperands[j])
                {
                    return "opOperands mismatch";
                }
            }

            auto lhsResults = lhs.opResults(opId);
            auto rhsResults = rhs.opResults(opId);
            if (lhsResults.size() != rhsResults.size())
            {
                return "opResults size mismatch";
            }
            for (std::size_t j = 0; j < lhsResults.size(); ++j)
            {
                if (lhsResults[j] != rhsResults[j])
                {
                    return "opResults mismatch";
                }
            }

            auto lhsAttrs = lhs.opAttrs(opId);
            auto rhsAttrs = rhs.opAttrs(opId);
            if (lhsAttrs.size() != rhsAttrs.size())
            {
                return "opAttrs size mismatch";
            }
            for (std::size_t j = 0; j < lhsAttrs.size(); ++j)
            {
                if (lhsAttrs[j].key != rhsAttrs[j].key || lhsAttrs[j].value != rhsAttrs[j].value)
                {
                    return "opAttrs mismatch";
                }
            }
        }

        for (std::size_t i = 0; i < lhsValues.size(); ++i)
        {
            auto valueId = lhsValues[i];
            if (lhs.valueSymbol(valueId) != rhs.valueSymbol(valueId))
            {
                return "valueSymbol mismatch";
            }
            if (lhs.valueWidth(valueId) != rhs.valueWidth(valueId))
            {
                return "valueWidth mismatch";
            }
            if (lhs.valueSigned(valueId) != rhs.valueSigned(valueId))
            {
                return "valueSigned mismatch";
            }
            if (lhs.valueIsInput(valueId) != rhs.valueIsInput(valueId))
            {
                return "valueIsInput mismatch";
            }
            if (lhs.valueIsOutput(valueId) != rhs.valueIsOutput(valueId))
            {
                return "valueIsOutput mismatch";
            }
            if (lhs.valueDef(valueId) != rhs.valueDef(valueId))
            {
                return "valueDef mismatch";
            }
            if (!compareSrcLoc(lhs.valueSrcLoc(valueId), rhs.valueSrcLoc(valueId)))
            {
                return "valueSrcLoc mismatch";
            }

            auto lhsUsers = lhs.valueUsers(valueId);
            auto rhsUsers = rhs.valueUsers(valueId);
            if (lhsUsers.size() != rhsUsers.size())
            {
                return "valueUsers size mismatch";
            }
            for (std::size_t j = 0; j < lhsUsers.size(); ++j)
            {
                if (lhsUsers[j].operation != rhsUsers[j].operation ||
                    lhsUsers[j].operandIndex != rhsUsers[j].operandIndex)
                {
                    return "valueUsers mismatch";
                }
            }
        }

        return "";
    }

} // namespace

int main()
{
    try
    {
        namespace grh_ir = wolf_sv::grh::ir;

        grh_ir::GraphSymbolTable graphSymbols;
        grh_ir::GraphBuilder builder(graphSymbols);

        auto symPortA = graphSymbols.intern("in_a");
        auto symPortB = graphSymbols.intern("in_b");
        auto symPortOut = graphSymbols.intern("out");
        auto symA = graphSymbols.intern("a");
        auto symB = graphSymbols.intern("b");
        auto symTmp = graphSymbols.intern("tmp");
        auto symOutVal = graphSymbols.intern("out_val");
        auto symAdd = graphSymbols.intern("add0");
        auto symAssign = graphSymbols.intern("assign0");
        auto symDelay = graphSymbols.intern("delay");
        auto symLabel = graphSymbols.intern("label");

        auto vA = builder.addValue(symA, 8, false);
        auto vB = builder.addValue(symB, 8, false);
        auto vTmp = builder.addValue(symTmp, 8, true);
        auto vOut = builder.addValue(symOutVal, 8, false);

        auto opAdd = builder.addOp(OperationKind::kAdd, symAdd);
        builder.addOperand(opAdd, vA);
        builder.addOperand(opAdd, vB);
        builder.addResult(opAdd, vTmp);

        auto opAssign = builder.addOp(OperationKind::kAssign, symAssign);
        builder.addOperand(opAssign, vTmp);
        builder.addResult(opAssign, vOut);

        builder.bindInputPort(symPortA, vA);
        builder.bindInputPort(symPortB, vB);
        builder.bindOutputPort(symPortOut, vOut);

        builder.setAttr(opAdd, symDelay, AttributeValue(int64_t(3)));
        builder.setAttr(opAdd, symLabel, AttributeValue(std::string("fast")));

        SrcLoc opLoc;
        opLoc.file = "roundtrip.sv";
        opLoc.line = 21;
        opLoc.column = 4;
        builder.setOpSrcLoc(opAdd, opLoc);

        SrcLoc valLoc;
        valLoc.file = "roundtrip.sv";
        valLoc.line = 22;
        valLoc.column = 1;
        builder.setValueSrcLoc(vA, valLoc);

        builder.clearValueSymbol(vTmp);
        builder.clearOpSymbol(opAssign);

        grh_ir::GraphView view = builder.freeze();
        grh_ir::GraphBuilder rebuilt = grh_ir::GraphBuilder::fromView(view, graphSymbols);
        grh_ir::GraphView roundTrip = rebuilt.freeze();

        const std::string mismatch = compareViews(view, roundTrip);
        if (!mismatch.empty())
        {
            return fail(std::string("GraphBuilder fromView roundtrip ") + mismatch);
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("Unhandled exception: ") + ex.what());
    }
    return 0;
}
