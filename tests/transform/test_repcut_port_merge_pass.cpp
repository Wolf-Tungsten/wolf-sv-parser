#include "core/grh.hpp"
#include "core/transform.hpp"
#include "transform/repcut_port_merge.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace wolvrix::lib::transform;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[repcut-port-merge-tests] " << message << '\n';
        return 1;
    }

    wolvrix::lib::grh::ValueId makeValue(wolvrix::lib::grh::Graph &graph,
                                         const std::string &name,
                                         int32_t width = 8,
                                         bool isSigned = false)
    {
        return graph.createValue(graph.internSymbol(name), width, isSigned);
    }

    std::optional<std::vector<std::string>> getAttrStrings(const wolvrix::lib::grh::Operation &op,
                                                           std::string_view key)
    {
        auto attr = op.attr(key);
        if (!attr)
        {
            return std::nullopt;
        }
        if (const auto *values = std::get_if<std::vector<std::string>>(&*attr))
        {
            return *values;
        }
        return std::nullopt;
    }

    wolvrix::lib::grh::OperationId findInstanceByName(const wolvrix::lib::grh::Graph &graph,
                                                      std::string_view instanceName)
    {
        for (const auto opId : graph.operations())
        {
            if (!opId.valid())
            {
                continue;
            }
            const auto op = graph.getOperation(opId);
            if (op.kind() != wolvrix::lib::grh::OperationKind::kInstance)
            {
                continue;
            }
            auto attr = op.attr("instanceName");
            if (!attr)
            {
                continue;
            }
            const auto *name = std::get_if<std::string>(&*attr);
            if (name != nullptr && *name == instanceName)
            {
                return opId;
            }
        }
        return wolvrix::lib::grh::OperationId::invalid();
    }

    bool containsName(const std::vector<std::string> &names, std::string_view name)
    {
        for (const auto &item : names)
        {
            if (item == name)
            {
                return true;
            }
        }
        return false;
    }

    wolvrix::lib::grh::SymbolId internUniqueSymbol(wolvrix::lib::grh::Graph &graph, const std::string &base)
    {
        std::string candidate = base;
        int suffix = 0;
        while (true)
        {
            const auto sym = graph.internSymbol(candidate);
            if (sym.valid() && !graph.findValue(sym).valid() && !graph.findOperation(sym).valid())
            {
                return sym;
            }
            candidate = base + "_" + std::to_string(++suffix);
        }
    }

    wolvrix::lib::grh::OperationId buildInstance(
        wolvrix::lib::grh::Graph &parent,
        std::string_view moduleName,
        std::string_view instanceName,
        const wolvrix::lib::grh::Graph &target,
        const std::unordered_map<std::string, wolvrix::lib::grh::ValueId> &inputMapping,
        const std::unordered_map<std::string, wolvrix::lib::grh::ValueId> &outputMapping)
    {
        const auto op =
            parent.createOperation(wolvrix::lib::grh::OperationKind::kInstance,
                                   internUniqueSymbol(parent, "inst_" + std::string(instanceName)));
        parent.setAttr(op, "moduleName", std::string(moduleName));
        parent.setAttr(op, "instanceName", std::string(instanceName));

        std::vector<std::string> inputNames;
        for (const auto &port : target.inputPorts())
        {
            inputNames.push_back(port.name);
        }
        std::vector<std::string> outputNames;
        for (const auto &port : target.outputPorts())
        {
            outputNames.push_back(port.name);
        }
        parent.setAttr(op, "inputPortName", inputNames);
        parent.setAttr(op, "outputPortName", outputNames);

        for (const auto &portName : inputNames)
        {
            parent.addOperand(op, inputMapping.at(portName));
        }
        for (const auto &portName : outputNames)
        {
            parent.addResult(op, outputMapping.at(portName));
        }
        return op;
    }

} // namespace

int main()
{
    using namespace wolvrix::lib::grh;

    Design design;

    Graph &producer = design.createGraph("top_repcut_part0");
    const auto prodIn0 = makeValue(producer, "prod_in0");
    const auto prodIn1 = makeValue(producer, "prod_in1");
    const auto prodIn2 = makeValue(producer, "prod_in2");
    producer.bindInputPort("src0", prodIn0);
    producer.bindInputPort("src1", prodIn1);
    producer.bindInputPort("src2", prodIn2);

    const auto prodOut0 = makeValue(producer, "prod_out0");
    const auto prodOut1 = makeValue(producer, "prod_out1");
    const auto prodTopOut = makeValue(producer, "prod_top_out");

    const auto assign0 = producer.createOperation(OperationKind::kAssign, producer.internSymbol("assign0"));
    producer.addOperand(assign0, prodIn0);
    producer.addResult(assign0, prodOut0);
    const auto assign1 = producer.createOperation(OperationKind::kAssign, producer.internSymbol("assign1"));
    producer.addOperand(assign1, prodIn1);
    producer.addResult(assign1, prodOut1);
    const auto assign2 = producer.createOperation(OperationKind::kAssign, producer.internSymbol("assign2"));
    producer.addOperand(assign2, prodIn2);
    producer.addResult(assign2, prodTopOut);

    producer.bindOutputPort("out_a", prodOut0);
    producer.bindOutputPort("out_b", prodOut1);
    producer.bindOutputPort("out_top", prodTopOut);

    Graph &consumer = design.createGraph("top_repcut_part1");
    const auto consIn0 = makeValue(consumer, "cons_in0");
    const auto consIn1 = makeValue(consumer, "cons_in1");
    consumer.bindInputPort("in_a", consIn0);
    consumer.bindInputPort("in_b", consIn1);
    const auto consOut = makeValue(consumer, "cons_out");
    const auto add = consumer.createOperation(OperationKind::kAdd, consumer.internSymbol("add"));
    consumer.addOperand(add, consIn0);
    consumer.addOperand(add, consIn1);
    consumer.addResult(add, consOut);
    consumer.bindOutputPort("sum", consOut);

    Graph &top = design.createGraph("top");
    design.markAsTop("top");

    const auto topIn0 = makeValue(top, "top_in0");
    const auto topIn1 = makeValue(top, "top_in1");
    const auto topIn2 = makeValue(top, "top_in2");
    top.bindInputPort("in0", topIn0);
    top.bindInputPort("in1", topIn1);
    top.bindInputPort("in2", topIn2);

    const auto linkA = makeValue(top, "link_a");
    const auto linkB = makeValue(top, "link_b");
    const auto keepTop = makeValue(top, "keep_top_value");
    const auto sumTop = makeValue(top, "sum_top");

    buildInstance(top,
                  producer.symbol(),
                  "part_0",
                  producer,
                  {{"src0", topIn0}, {"src1", topIn1}, {"src2", topIn2}},
                  {{"out_a", linkA}, {"out_b", linkB}, {"out_top", keepTop}});
    buildInstance(top,
                  consumer.symbol(),
                  "part_1",
                  consumer,
                  {{"in_a", linkA}, {"in_b", linkB}},
                  {{"sum", sumTop}});

    top.bindOutputPort("keep_top", keepTop);

    PassManager manager;
    manager.options().verbosity = PassVerbosity::Info;
    manager.addPass(std::make_unique<RepcutPortMergePass>(RepcutPortMergeOptions{.path = "top"}));

    PassDiagnostics diags;
    const PassManagerResult result = manager.run(design, diags);
    if (!result.success || diags.hasError())
    {
        return fail("repcut-port-merge should succeed on the simple wrapper graph");
    }
    if (!result.changed)
    {
        return fail("repcut-port-merge should report graph changes");
    }

    if (producer.outputPorts().size() != 2)
    {
        return fail("producer output port count should drop from 3 to 2");
    }
    if (consumer.inputPorts().size() != 1)
    {
        return fail("consumer input port count should drop from 2 to 1");
    }
    if (top.outputPorts().size() != 1 || top.outputPorts().front().name != "keep_top")
    {
        return fail("top output port should remain unchanged");
    }

    const auto part0Op = findInstanceByName(top, "part_0");
    const auto part1Op = findInstanceByName(top, "part_1");
    if (!part0Op.valid() || !part1Op.valid())
    {
        return fail("expected rebuilt instances to exist");
    }

    const auto part0Outputs = getAttrStrings(top.getOperation(part0Op), "outputPortName");
    const auto part1Inputs = getAttrStrings(top.getOperation(part1Op), "inputPortName");
    if (!part0Outputs || !part1Inputs)
    {
        return fail("rebuilt instances are missing port-name attributes");
    }
    if (part0Outputs->size() != 2)
    {
        return fail("producer wrapper instance should expose two outputs after merge");
    }
    if (part1Inputs->size() != 1)
    {
        return fail("consumer wrapper instance should expose one input after merge");
    }
    if (containsName(*part0Outputs, "out_a") || containsName(*part0Outputs, "out_b"))
    {
        return fail("producer wrapper instance should not keep fine-grained merged outputs");
    }
    if (containsName(*part1Inputs, "in_a") || containsName(*part1Inputs, "in_b"))
    {
        return fail("consumer wrapper instance should not keep fine-grained merged inputs");
    }
    if (!containsName(*part0Outputs, "out_top"))
    {
        return fail("top-connected producer output should remain unmerged");
    }

    std::size_t sliceCount = 0;
    for (const auto opId : consumer.operations())
    {
        if (!opId.valid())
        {
            continue;
        }
        if (consumer.getOperation(opId).kind() == OperationKind::kSliceStatic)
        {
            ++sliceCount;
        }
    }
    if (sliceCount < 2)
    {
        return fail("consumer should receive per-member slices from the merged input");
    }

    if (top.findValue("link_a").valid() || top.findValue("link_b").valid())
    {
        return fail("old fine-grained top link values should be erased");
    }

    return 0;
}
