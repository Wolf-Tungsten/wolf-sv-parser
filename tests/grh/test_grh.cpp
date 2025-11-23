#include "emit.hpp"
#include "grh.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace wolf_sv::grh;
using namespace wolf_sv::emit;

namespace
{

    int fail(const std::string &message)
    {
        std::cerr << "[grh_tests] " << message << '\n';
        return 1;
    }

    template <typename Func>
    bool expectThrows(Func &&func)
    {
        try
        {
            func();
        }
        catch (const std::exception &)
        {
            return true;
        }
        return false;
    }

} // namespace

int main()
{
    try
    {
        Netlist netlist;
        Graph &graph = netlist.createGraph("demo");

        Value &a = graph.createValue("a", 8, false);
        Value &b = graph.createValue("b", 8, false);
        Value &sum = graph.createValue("sum", 8, false);
        Value &sumCopy = graph.createValue("sum_copy", 8, false);

        graph.bindInputPort("a", a);
        graph.bindInputPort("b", b);
        graph.bindOutputPort("sum", sum);
        graph.bindOutputPort("sum_copy", sumCopy);

        Operation &op = graph.createOperation(OperationKind::kAdd, "add0");
        op.addOperand(a);
        op.addOperand(b);
        op.addResult(sum);

        bool threwOnNaNAttribute = expectThrows([&]
                                                { op.setAttribute("invalid_nan", AttributeValue(std::numeric_limits<double>::quiet_NaN())); });
        if (!threwOnNaNAttribute)
        {
            return fail("Expected NaN attribute to throw");
        }

        bool threwOnDoubleArrayInfinity = expectThrows([&]
                                                       { op.setAttribute("invalid_array", AttributeValue(std::vector<double>{0.25, std::numeric_limits<double>::infinity()})); });
        if (!threwOnDoubleArrayInfinity)
        {
            return fail("Expected double array with infinity to throw");
        }

        Operation &assignOp = graph.createOperation(OperationKind::kAssign, "assign0");
        assignOp.addOperand(sum);
        assignOp.addResult(sumCopy);
        if (sumCopy.definingOp() != &assignOp)
        {
            return fail("Assign result defining operation not set");
        }

        if (a.users().size() != 1 || a.users()[0].operation != &op)
        {
            return fail("Operand usage tracking failed");
        }
        if (sum.definingOp() != &op)
        {
            return fail("Result defining operation not set");
        }

        Value &c = graph.createValue("c", 8, false);
        graph.bindInputPort("c", c);

        op.replaceOperand(1, c);
        if (op.operands()[1] != &c)
        {
            return fail("Operand replacement did not update slot");
        }
        if (!b.users().empty())
        {
            return fail("Old operand still listed as user after replacement");
        }
        if (c.users().size() != 1 || c.users()[0].operation != &op || c.users()[0].operandIndex != 1)
        {
            return fail("New operand usage tracking incorrect after replacement");
        }

        Graph &auxGraph = netlist.createGraph("aux");
        Value &foreignValue = auxGraph.createValue("foreign", 8, false);
        bool threwOnForeignOperand = expectThrows([&]
                                                  { op.replaceOperand(0, foreignValue); });
        if (!threwOnForeignOperand)
        {
            return fail("Expected replacing operand with foreign value to throw");
        }

        Operation &passThrough = graph.createOperation(OperationKind::kAssign, "assign1");
        passThrough.addOperand(c);
        Value &passResult = graph.createValue("passthrough", 8, false);
        passThrough.addResult(passResult);

        Value &passResultAlt = graph.createValue("passthrough_alt", 8, false);
        passThrough.replaceResult(0, passResultAlt);
        if (passResult.definingOp() != nullptr)
        {
            return fail("Old result still records defining operation after replacement");
        }
        if (passResultAlt.definingOp() != &passThrough)
        {
            return fail("New result defining operation incorrect after replacement");
        }
        if (passThrough.results()[0] != &passResultAlt)
        {
            return fail("Result replacement did not update slot");
        }

        bool threwOnForeignResult = expectThrows([&]
                                                 { passThrough.replaceResult(0, foreignValue); });
        if (!threwOnForeignResult)
        {
            return fail("Expected replacing result with foreign value to throw");
        }

        Value &existingResult = graph.createValue("existing_result", 8, false);
        Operation &existingProducer = graph.createOperation(OperationKind::kAssign, "assign_existing");
        existingProducer.addOperand(c);
        existingProducer.addResult(existingResult);

        bool threwOnExistingResult = expectThrows([&]
                                                  { passThrough.replaceResult(0, existingResult); });
        if (!threwOnExistingResult)
        {
            return fail("Expected replacing result with already-defined value to throw");
        }

        bool threwOnDuplicateValue = expectThrows([&]
                                                  { graph.createValue("a", 1, false); });
        if (!threwOnDuplicateValue)
        {
            return fail("Expected duplicate value symbol to throw");
        }

        bool threwOnInvalidWidth = expectThrows([&]
                                                { graph.createValue("invalid", 0, false); });
        if (!threwOnInvalidWidth)
        {
            return fail("Expected zero width to throw");
        }

        netlist.markAsTop("demo");

        EmitDiagnostics emitDiagnostics;
        EmitJSON emitter(&emitDiagnostics);
        EmitOptions emitOptions;
        auto jsonOpt = emitter.emitToString(netlist, emitOptions);
        if (!jsonOpt || emitDiagnostics.hasError())
        {
            return fail("Failed to emit JSON for netlist");
        }
        std::string json = *jsonOpt;

#ifdef GRH_STAGE1_JSON_PATH
        try
        {
            namespace fs = std::filesystem;
            const fs::path artifactPath = fs::path(GRH_STAGE1_JSON_PATH);
            const fs::path artifactDir = artifactPath.parent_path();

            if (!artifactDir.empty())
            {
                std::error_code ec;
                fs::create_directories(artifactDir, ec);
                if (ec)
                {
                    return fail(std::string("Failed to create artifact directory: ") + ec.message());
                }
            }

            std::ofstream outFile(artifactPath);
            if (!outFile)
            {
                return fail("Failed to open artifact file for writing");
            }
            outFile << json;
        }
        catch (const std::exception &ex)
        {
            return fail(std::string("Failed to persist GRH stage1 JSON: ") + ex.what());
        }
#endif

        Netlist parsed = Netlist::fromJsonString(json);
        if (parsed.topGraphs().size() != 1 || parsed.topGraphs()[0] != "demo")
        {
            return fail("Top graph round-trip failed");
        }

        const Graph *parsedGraph = parsed.findGraph("demo");
        if (!parsedGraph)
        {
            return fail("Parsed graph missing");
        }

        const Operation *parsedOp = parsedGraph->findOperation("add0");
        if (!parsedOp)
        {
            return fail("Parsed operation missing");
        }
        if (parsedOp->operands().size() != 2 || parsedOp->results().size() != 1)
        {
            return fail("Parsed operation connectivity mismatch");
        }

        const Operation *parsedAssign = parsedGraph->findOperation("assign0");
        if (!parsedAssign)
        {
            return fail("Parsed assign operation missing");
        }
        if (parsedAssign->kind() != OperationKind::kAssign)
        {
            return fail("Assign operation kind mismatch");
        }
        if (parsedAssign->operands().size() != 1 || parsedAssign->results().size() != 1)
        {
            return fail("Assign operation connectivity mismatch");
        }

        auto parsedAssignKind = parseOperationKind("kAssign");
        if (!parsedAssignKind || *parsedAssignKind != OperationKind::kAssign)
        {
            return fail("Failed to parse kAssign operation kind");
        }

        emitDiagnostics.clear();
        auto jsonAgainOpt = emitter.emitToString(parsed, emitOptions);
        if (!jsonAgainOpt || emitDiagnostics.hasError())
        {
            return fail("Failed to re-emit JSON after parse");
        }
        std::string jsonAgain = *jsonAgainOpt;
        if (json != jsonAgain)
        {
            return fail("JSON serialization not stable");
        }

        const std::string nestedArrayJson = R"({
  "graphs": [
    {
      "name": "illegal_nested_array",
      "vals": [],
      "ports": {
        "in": [],
        "out": []
      },
      "ops": [
        {
          "sym": "bad",
          "kind": "kAdd",
          "in": [],
          "out": [],
          "attrs": {
            "illegal": {
              "k": "int[]",
              "vs": [
                1,
                [2, 3]
              ]
            }
          }
        }
      ]
    }
  ],
  "tops": []
})";
        bool threwOnNestedArrayAttribute = expectThrows([&]
                                                        { Netlist::fromJsonString(nestedArrayJson); });
        if (!threwOnNestedArrayAttribute)
        {
            return fail("Expected nested array attribute to throw during parse");
        }

        const std::string objectAttributeJson = R"({
  "graphs": [
    {
      "name": "illegal_object_attr",
      "vals": [],
      "ports": {
        "in": [],
        "out": []
      },
      "ops": [
        {
          "sym": "bad_obj",
          "kind": "kAdd",
          "in": [],
          "out": [],
          "attrs": {
            "illegal": {
              "k": "string",
              "v": {
                "unexpected": "object"
              }
            }
          }
        }
      ]
    }
  ],
  "tops": []
})";
        bool threwOnObjectAttribute = expectThrows([&]
                                                   { Netlist::fromJsonString(objectAttributeJson); });
        if (!threwOnObjectAttribute)
        {
            return fail("Expected object attribute to throw during parse");
        }
    }
    catch (const std::exception &ex)
    {
        return fail(std::string("Unhandled exception: ") + ex.what());
    }
    return 0;
}
