#include "elaborate.hpp"

#include <iostream>
#include <string>

using namespace wolf_sv;

namespace {

int fail(const std::string& message) {
    std::cerr << "[write_back_memo] " << message << '\n';
    return 1;
}

int testNetWriteBack() {
    grh::ir::Netlist netlist;
    grh::ir::Graph& graph = netlist.createGraph("wb_net");
    auto makeValue = [&](std::string_view name, int32_t width, bool isSigned) {
        return graph.createValue(graph.internSymbol(name), width, isSigned);
    };

    SignalMemoEntry netEntry;
    netEntry.width = 8;
    netEntry.isSigned = false;
    netEntry.value = makeValue("net_value", 8, false);

    ValueId hi = makeValue("rhs_hi", 4, false);
    ValueId lo = makeValue("rhs_lo", 4, false);

    WriteBackMemo memo;
    memo.recordWrite(netEntry, WriteBackMemo::AssignmentKind::Continuous, nullptr,
                     {WriteBackMemo::Slice{"net[7:4]", 7, 4, hi, nullptr},
                      WriteBackMemo::Slice{"net[3:0]", 3, 0, lo, nullptr}});

    memo.finalize(graph, nullptr);
    if (!memo.empty()) {
        return fail("WriteBackMemo should be empty after finalize");
    }

    OperationId concatOpId = OperationId::invalid();
    OperationId assignOpId = OperationId::invalid();
    for (OperationId opId : graph.operations()) {
        const grh::ir::Operation op = graph.getOperation(opId);
        if (op.kind() == grh::ir::OperationKind::kConcat) {
            concatOpId = opId;
        }
        if (op.kind() == grh::ir::OperationKind::kAssign) {
            assignOpId = opId;
        }
    }

    if (!concatOpId) {
        return fail("Expected kConcat operation for multi-slice write-back");
    }
    if (!assignOpId) {
        return fail("Expected kAssign operation driving the net value");
    }
    const grh::ir::Operation concatOp = graph.getOperation(concatOpId);
    const grh::ir::Operation assignOp = graph.getOperation(assignOpId);
    if (concatOp.operands().size() != 2) {
        return fail("Concat operation should have 2 operands");
    }
    if (concatOp.operands()[0] != hi || concatOp.operands()[1] != lo) {
        return fail("Concat operands are not in high-to-low order");
    }
    if (concatOp.results().size() != 1) {
        return fail("Concat should produce a single temporary value");
    }
    if (assignOp.operands().size() != 1 ||
        assignOp.operands().front() != concatOp.results().front()) {
        return fail("Assign should consume concat result");
    }
    if (assignOp.results().size() != 1 || assignOp.results().front() != netEntry.value) {
        return fail("Assign should drive the memoized net value");
    }
    return 0;
}

int testRegWriteBack() {
    grh::ir::Netlist netlist;
    grh::ir::Graph& graph = netlist.createGraph("wb_reg");
    auto makeValue = [&](std::string_view name, int32_t width, bool isSigned) {
        return graph.createValue(graph.internSymbol(name), width, isSigned);
    };

    SignalMemoEntry regEntry;
    regEntry.width = 4;
    regEntry.isSigned = false;
    regEntry.value = makeValue("reg_q", 4, false);

    OperationId regOp = graph.createOperation(grh::ir::OperationKind::kRegister,
                                              graph.internSymbol("reg_state"));
    graph.addResult(regOp, regEntry.value);
    regEntry.stateOp = regOp;

    ValueId dataValue = makeValue("reg_data", 4, false);

    WriteBackMemo memo;
    memo.recordWrite(regEntry, WriteBackMemo::AssignmentKind::Procedural, nullptr,
                     {WriteBackMemo::Slice{"reg[3:0]", 3, 0, dataValue, nullptr}});

    memo.finalize(graph, nullptr);
    if (!memo.empty()) {
        return fail("WriteBackMemo should be empty after finalize");
    }

    if (graph.getOperation(regOp).operands().size() != 1 ||
        graph.getOperation(regOp).operands().front() != dataValue) {
        return fail("Register state operation should receive the composed data operand");
    }

    for (OperationId opId : graph.operations()) {
        if (opId == regOp) {
            continue;
        }
        if (graph.getOperation(opId).kind() == grh::ir::OperationKind::kAssign) {
            return fail("Register write-back should not emit extra kAssign operations");
        }
    }
    return 0;
}

int testPartialCoverage() {
    grh::ir::Netlist netlist;
    grh::ir::Graph& graph = netlist.createGraph("wb_partial");
    auto makeValue = [&](std::string_view name, int32_t width, bool isSigned) {
        return graph.createValue(graph.internSymbol(name), width, isSigned);
    };

    SignalMemoEntry entry;
    entry.width = 8;
    entry.isSigned = false;
    entry.value = makeValue("partial_net", 8, false);

    ValueId lowSlice = makeValue("rhs_low", 4, false);

    WriteBackMemo memo;
    memo.recordWrite(entry, WriteBackMemo::AssignmentKind::Continuous, nullptr,
                     {WriteBackMemo::Slice{"partial_net[3:0]", 3, 0, lowSlice, nullptr}});

    memo.finalize(graph, nullptr);

    OperationId concatOpId = OperationId::invalid();
    OperationId assignOpId = OperationId::invalid();
    OperationId zeroOpId = OperationId::invalid();
    for (OperationId opId : graph.operations()) {
        const grh::ir::Operation op = graph.getOperation(opId);
        switch (op.kind()) {
        case grh::ir::OperationKind::kConcat:
            concatOpId = opId;
            break;
        case grh::ir::OperationKind::kAssign:
            assignOpId = opId;
            break;
        case grh::ir::OperationKind::kConstant:
            zeroOpId = opId;
            break;
        default:
            break;
        }
    }

    if (!concatOpId || !assignOpId || !zeroOpId) {
        return fail("Partial coverage should create constant, concat and assign operations");
    }
    const grh::ir::Operation concatOp = graph.getOperation(concatOpId);
    const grh::ir::Operation assignOp = graph.getOperation(assignOpId);
    const grh::ir::Operation zeroOp = graph.getOperation(zeroOpId);
    if (zeroOp.results().size() != 1) {
        return fail("Zero-fill constant should produce exactly one result");
    }

    ValueId zeroValue = zeroOp.results().front();
    if (!zeroValue || graph.getValue(zeroValue).width() != 4) {
        return fail("Zero-fill constant should be 4 bits wide");
    }

    if (concatOp.operands().size() != 2) {
        return fail("Partial coverage concat should have two operands (zero-fill + RHS slice)");
    }
    if (concatOp.operands()[0] != zeroValue || concatOp.operands()[1] != lowSlice) {
        return fail("Concat operands should place zero-fill before the real slice");
    }
    if (assignOp.operands().size() != 1 || assignOp.operands().front() != concatOp.results().front()) {
        return fail("Assign should consume concat result for partial coverage");
    }
    if (assignOp.results().empty() || assignOp.results().front() != entry.value) {
        return fail("Assign should drive the memoized net value for partial coverage");
    }

    return 0;
}

} // namespace

int main() {
    if (int result = testNetWriteBack(); result != 0) {
        return result;
    }
    if (int result = testRegWriteBack(); result != 0) {
        return result;
    }
    if (int result = testPartialCoverage(); result != 0) {
        return result;
    }
    return 0;
}
