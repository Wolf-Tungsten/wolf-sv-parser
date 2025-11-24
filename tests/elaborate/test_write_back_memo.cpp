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
    grh::Netlist netlist;
    grh::Graph& graph = netlist.createGraph("wb_net");

    SignalMemoEntry netEntry;
    netEntry.width = 8;
    netEntry.isSigned = false;
    netEntry.value = &graph.createValue("net_value", 8, false);

    grh::Value& hi = graph.createValue("rhs_hi", 4, false);
    grh::Value& lo = graph.createValue("rhs_lo", 4, false);

    WriteBackMemo memo;
    memo.recordWrite(netEntry, WriteBackMemo::AssignmentKind::Continuous, nullptr,
                     {WriteBackMemo::Slice{"net[7:4]", 7, 4, &hi, nullptr},
                      WriteBackMemo::Slice{"net[3:0]", 3, 0, &lo, nullptr}});

    memo.finalize(graph, nullptr);
    if (!memo.empty()) {
        return fail("WriteBackMemo should be empty after finalize");
    }

    const grh::Operation* concatOp = nullptr;
    const grh::Operation* assignOp = nullptr;
    for (const auto& opSymbol : graph.operationOrder()) {
        const grh::Operation& op = graph.getOperation(opSymbol);
        if (op.kind() == grh::OperationKind::kConcat) {
            concatOp = &op;
        }
        if (op.kind() == grh::OperationKind::kAssign) {
            assignOp = &op;
        }
    }

    if (!concatOp) {
        return fail("Expected kConcat operation for multi-slice write-back");
    }
    if (!assignOp) {
        return fail("Expected kAssign operation driving the net value");
    }
    if (concatOp->operands().size() != 2) {
        return fail("Concat operation should have 2 operands");
    }
    if (concatOp->operands()[0] != &hi || concatOp->operands()[1] != &lo) {
        return fail("Concat operands are not in high-to-low order");
    }
    if (concatOp->results().size() != 1) {
        return fail("Concat should produce a single temporary value");
    }
    if (assignOp->operands().size() != 1 ||
        assignOp->operands().front() != concatOp->results().front()) {
        return fail("Assign should consume concat result");
    }
    if (assignOp->results().size() != 1 || assignOp->results().front() != netEntry.value) {
        return fail("Assign should drive the memoized net value");
    }
    return 0;
}

int testRegWriteBack() {
    grh::Netlist netlist;
    grh::Graph& graph = netlist.createGraph("wb_reg");

    SignalMemoEntry regEntry;
    regEntry.width = 4;
    regEntry.isSigned = false;
    regEntry.value = &graph.createValue("reg_q", 4, false);

    grh::Operation& regOp = graph.createOperation(grh::OperationKind::kRegister, "reg_state");
    regOp.addResult(*regEntry.value);
    regEntry.stateOp = &regOp;

    grh::Value& dataValue = graph.createValue("reg_data", 4, false);

    WriteBackMemo memo;
    memo.recordWrite(regEntry, WriteBackMemo::AssignmentKind::Procedural, nullptr,
                     {WriteBackMemo::Slice{"reg[3:0]", 3, 0, &dataValue, nullptr}});

    memo.finalize(graph, nullptr);
    if (!memo.empty()) {
        return fail("WriteBackMemo should be empty after finalize");
    }

    if (regOp.operands().size() != 1 || regOp.operands().front() != &dataValue) {
        return fail("Register state operation should receive the composed data operand");
    }

    for (const auto& opSymbol : graph.operationOrder()) {
        if (opSymbol == regOp.symbol()) {
            continue;
        }
        if (graph.getOperation(opSymbol).kind() == grh::OperationKind::kAssign) {
            return fail("Register write-back should not emit extra kAssign operations");
        }
    }
    return 0;
}

int testPartialCoverage() {
    grh::Netlist netlist;
    grh::Graph& graph = netlist.createGraph("wb_partial");

    SignalMemoEntry entry;
    entry.width = 8;
    entry.isSigned = false;
    entry.value = &graph.createValue("partial_net", 8, false);

    grh::Value& lowSlice = graph.createValue("rhs_low", 4, false);

    WriteBackMemo memo;
    memo.recordWrite(entry, WriteBackMemo::AssignmentKind::Continuous, nullptr,
                     {WriteBackMemo::Slice{"partial_net[3:0]", 3, 0, &lowSlice, nullptr}});

    memo.finalize(graph, nullptr);

    const grh::Operation* concatOp = nullptr;
    const grh::Operation* assignOp = nullptr;
    const grh::Operation* zeroOp = nullptr;
    for (const auto& opSymbol : graph.operationOrder()) {
        const grh::Operation& op = graph.getOperation(opSymbol);
        switch (op.kind()) {
        case grh::OperationKind::kConcat:
            concatOp = &op;
            break;
        case grh::OperationKind::kAssign:
            assignOp = &op;
            break;
        case grh::OperationKind::kConstant:
            zeroOp = &op;
            break;
        default:
            break;
        }
    }

    if (!concatOp || !assignOp || !zeroOp) {
        return fail("Partial coverage should create constant, concat and assign operations");
    }
    if (zeroOp->results().size() != 1) {
        return fail("Zero-fill constant should produce exactly one result");
    }

    grh::Value* zeroValue = zeroOp->results().front();
    if (!zeroValue || zeroValue->width() != 4) {
        return fail("Zero-fill constant should be 4 bits wide");
    }

    if (concatOp->operands().size() != 2) {
        return fail("Partial coverage concat should have two operands (zero-fill + RHS slice)");
    }
    if (concatOp->operands()[0] != zeroValue || concatOp->operands()[1] != &lowSlice) {
        return fail("Concat operands should place zero-fill before the real slice");
    }
    if (assignOp->operands().size() != 1 || assignOp->operands().front() != concatOp->results().front()) {
        return fail("Assign should consume concat result for partial coverage");
    }
    if (assignOp->results().empty() || assignOp->results().front() != entry.value) {
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
