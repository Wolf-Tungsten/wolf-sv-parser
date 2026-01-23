#include "load.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace grh::ir::load
{

    namespace
    {

        using grh::ir::AttributeValue;
        using grh::ir::Graph;
        using grh::ir::Netlist;
        using grh::ir::OperationId;
        using grh::ir::OperationKind;
        using grh::ir::SymbolId;
        using grh::ir::Value;
        using grh::ir::ValueId;
        using grh::ir::attributeValueIsJsonSerializable;
        using grh::ir::parseOperationKind;
        using grh::ir::SrcLoc;

        enum class AttributeKind
        {
            Bool,
            Int,
            Double,
            String,
            BoolArray,
            IntArray,
            DoubleArray,
            StringArray
        };

        struct JsonValue
        {
            using Array = std::vector<JsonValue>;
            using Object = std::map<std::string, JsonValue, std::less<>>;

            std::variant<std::nullptr_t, bool, int64_t, double, std::string, Array, Object> value;

            bool isNull() const { return std::holds_alternative<std::nullptr_t>(value); }
            bool isBool() const { return std::holds_alternative<bool>(value); }
            bool isInt() const { return std::holds_alternative<int64_t>(value); }
            bool isDouble() const { return std::holds_alternative<double>(value); }
            bool isString() const { return std::holds_alternative<std::string>(value); }
            bool isArray() const { return std::holds_alternative<Array>(value); }
            bool isObject() const { return std::holds_alternative<Object>(value); }

            bool asBool(std::string_view ctx) const
            {
                if (!isBool())
                {
                    throw std::runtime_error(std::string(ctx) + ": expected bool");
                }
                return std::get<bool>(value);
            }

            int64_t asInt(std::string_view ctx) const
            {
                if (isInt())
                {
                    return std::get<int64_t>(value);
                }
                if (isDouble())
                {
                    double v = std::get<double>(value);
                    if (std::trunc(v) != v)
                    {
                        throw std::runtime_error(std::string(ctx) + ": expected integer number");
                    }
                    return static_cast<int64_t>(v);
                }
                throw std::runtime_error(std::string(ctx) + ": expected integer");
            }

            double asDouble(std::string_view ctx) const
            {
                if (isDouble())
                {
                    return std::get<double>(value);
                }
                if (isInt())
                {
                    return static_cast<double>(std::get<int64_t>(value));
                }
                throw std::runtime_error(std::string(ctx) + ": expected number");
            }

            const std::string &asString(std::string_view ctx) const
            {
                if (!isString())
                {
                    throw std::runtime_error(std::string(ctx) + ": expected string");
                }
                return std::get<std::string>(value);
            }

            const Array &asArray(std::string_view ctx) const
            {
                if (!isArray())
                {
                    throw std::runtime_error(std::string(ctx) + ": expected array");
                }
                return std::get<Array>(value);
            }

            const Object &asObject(std::string_view ctx) const
            {
                if (!isObject())
                {
                    throw std::runtime_error(std::string(ctx) + ": expected object");
                }
                return std::get<Object>(value);
            }
        };

        class JsonParser
        {
        public:
            explicit JsonParser(std::string_view text) : text_(text), current_(text_.data()), end_(text_.data() + text_.size()) {}

            JsonValue parse()
            {
                skipWhitespace();
                JsonValue value = parseValue();
                skipWhitespace();
                if (current_ != end_)
                {
                    throw std::runtime_error("Trailing characters after JSON document");
                }
                return value;
            }

        private:
            JsonValue parseValue()
            {
                if (current_ == end_)
                {
                    throw std::runtime_error("Unexpected end of JSON input");
                }

                switch (*current_)
                {
                case '{':
                    return parseObject();
                case '[':
                    return parseArray();
                case '"':
                    return JsonValue{parseString()};
                case 't':
                    return JsonValue{parseLiteral("true", true)};
                case 'f':
                    return JsonValue{parseLiteral("false", false)};
                case 'n':
                    parseLiteral("null");
                    return JsonValue{std::nullptr_t{}};
                default:
                    if (*current_ == '-' || std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        return parseNumber();
                    }
                    break;
                }

                throw std::runtime_error("Invalid JSON value");
            }

            JsonValue parseObject()
            {
                JsonValue::Object obj;
                expect('{');
                skipWhitespace();
                if (consume('}'))
                {
                    return JsonValue{std::move(obj)};
                }

                while (true)
                {
                    skipWhitespace();
                    std::string key = parseString();
                    skipWhitespace();
                    expect(':');
                    skipWhitespace();
                    obj.emplace(std::move(key), parseValue());
                    skipWhitespace();
                    if (consume('}'))
                    {
                        break;
                    }
                    expect(',');
                }

                return JsonValue{std::move(obj)};
            }

            JsonValue parseArray()
            {
                JsonValue::Array arr;
                expect('[');
                skipWhitespace();
                if (consume(']'))
                {
                    return JsonValue{std::move(arr)};
                }

                while (true)
                {
                    skipWhitespace();
                    arr.push_back(parseValue());
                    skipWhitespace();
                    if (consume(']'))
                    {
                        break;
                    }
                    expect(',');
                }

                return JsonValue{std::move(arr)};
            }

            JsonValue parseNumber()
            {
                const char *start = current_;
                if (*current_ == '-')
                {
                    ++current_;
                }
                if (current_ == end_)
                {
                    throw std::runtime_error("Invalid JSON number");
                }
                if (*current_ == '0')
                {
                    ++current_;
                }
                else
                {
                    if (!std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        throw std::runtime_error("Invalid JSON number");
                    }
                    while (current_ != end_ && std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        ++current_;
                    }
                }

                bool isFloat = false;
                if (current_ != end_ && *current_ == '.')
                {
                    isFloat = true;
                    ++current_;
                    if (current_ == end_ || !std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        throw std::runtime_error("Invalid JSON number");
                    }
                    while (current_ != end_ && std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        ++current_;
                    }
                }

                if (current_ != end_ && (*current_ == 'e' || *current_ == 'E'))
                {
                    isFloat = true;
                    ++current_;
                    if (current_ != end_ && (*current_ == '+' || *current_ == '-'))
                    {
                        ++current_;
                    }
                    if (current_ == end_ || !std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        throw std::runtime_error("Invalid JSON number");
                    }
                    while (current_ != end_ && std::isdigit(static_cast<unsigned char>(*current_)))
                    {
                        ++current_;
                    }
                }

                std::string_view numberView(start, static_cast<std::size_t>(current_ - start));
                if (isFloat)
                {
                    double value = 0.0;
                    auto [ptr, ec] = std::from_chars(numberView.data(), numberView.data() + numberView.size(), value);
                    if (ec != std::errc())
                    {
                        value = std::stod(std::string(numberView));
                    }
                    return JsonValue{value};
                }

                int64_t intValue = 0;
                auto [ptr, ec] = std::from_chars(numberView.data(), numberView.data() + numberView.size(), intValue);
                if (ec != std::errc())
                {
                    intValue = std::stoll(std::string(numberView));
                }
                return JsonValue{intValue};
            }

            std::string parseString()
            {
                expect('"');
                std::string result;
                while (current_ != end_)
                {
                    char ch = *current_++;
                    if (ch == '"')
                    {
                        break;
                    }
                    if (ch == '\\')
                    {
                        if (current_ == end_)
                        {
                            throw std::runtime_error("Invalid escape sequence");
                        }

                        char esc = *current_++;
                        switch (esc)
                        {
                        case '"':
                        case '\\':
                        case '/':
                            result.push_back(esc);
                            break;
                        case 'b':
                            result.push_back('\b');
                            break;
                        case 'f':
                            result.push_back('\f');
                            break;
                        case 'n':
                            result.push_back('\n');
                            break;
                        case 'r':
                            result.push_back('\r');
                            break;
                        case 't':
                            result.push_back('\t');
                            break;
                        case 'u':
                        {
                            if (end_ - current_ < 4)
                            {
                                throw std::runtime_error("Invalid unicode escape");
                            }
                            unsigned value = 0;
                            for (int i = 0; i < 4; ++i)
                            {
                                char hex = *current_++;
                                value <<= 4;
                                if (hex >= '0' && hex <= '9')
                                {
                                    value |= static_cast<unsigned>(hex - '0');
                                }
                                else if (hex >= 'a' && hex <= 'f')
                                {
                                    value |= static_cast<unsigned>(hex - 'a' + 10);
                                }
                                else if (hex >= 'A' && hex <= 'F')
                                {
                                    value |= static_cast<unsigned>(hex - 'A' + 10);
                                }
                                else
                                {
                                    throw std::runtime_error("Invalid unicode escape");
                                }
                            }
                            if (value <= 0x7F)
                            {
                                result.push_back(static_cast<char>(value));
                            }
                            else if (value <= 0x7FF)
                            {
                                result.push_back(static_cast<char>(0xC0 | ((value >> 6) & 0x1F)));
                                result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
                            }
                            else
                            {
                                result.push_back(static_cast<char>(0xE0 | ((value >> 12) & 0x0F)));
                                result.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
                                result.push_back(static_cast<char>(0x80 | (value & 0x3F)));
                            }
                            break;
                        }
                        default:
                            throw std::runtime_error("Invalid escape sequence");
                        }
                    }
                    else
                    {
                        result.push_back(ch);
                    }
                }
                return result;
            }

            JsonValue parseLiteral(std::string_view literal, bool value)
            {
                for (char expected : literal)
                {
                    if (current_ == end_ || *current_++ != expected)
                    {
                        throw std::runtime_error("Invalid JSON literal");
                    }
                }
                return JsonValue{value};
            }

            void parseLiteral(std::string_view literal)
            {
                for (char expected : literal)
                {
                    if (current_ == end_ || *current_++ != expected)
                    {
                        throw std::runtime_error("Invalid JSON literal");
                    }
                }
            }

            void skipWhitespace()
            {
                while (current_ != end_ && std::isspace(static_cast<unsigned char>(*current_)))
                {
                    ++current_;
                }
            }

            bool consume(char ch)
            {
                if (current_ != end_ && *current_ == ch)
                {
                    ++current_;
                    return true;
                }
                return false;
            }

            void expect(char ch)
            {
                if (current_ == end_ || *current_ != ch)
                {
                    throw std::runtime_error("Unexpected character in JSON stream");
                }
                ++current_;
            }

            std::string_view text_;
            const char *current_;
            const char *end_;
        };

        JsonValue parseJson(std::string_view json)
        {
            JsonParser parser(json);
            return parser.parse();
        }

        AttributeKind parseAttributeKind(std::string_view text)
        {
            if (text == "bool")
            {
                return AttributeKind::Bool;
            }
            if (text == "int")
            {
                return AttributeKind::Int;
            }
            if (text == "double")
            {
                return AttributeKind::Double;
            }
            if (text == "string" || text == "str")
            {
                return AttributeKind::String;
            }
            if (text == "bool_array" || text == "bool[]")
            {
                return AttributeKind::BoolArray;
            }
            if (text == "int_array" || text == "int[]")
            {
                return AttributeKind::IntArray;
            }
            if (text == "double_array" || text == "double[]")
            {
                return AttributeKind::DoubleArray;
            }
            if (text == "string_array" || text == "string[]")
            {
                return AttributeKind::StringArray;
            }
            throw std::runtime_error("Unknown attribute kind: " + std::string(text));
        }

        AttributeValue parseAttributeValue(const JsonValue &jsonAttr)
        {
            const auto &object = jsonAttr.asObject("attribute");
            auto kindIt = object.find("t");
            if (kindIt == object.end())
            {
                kindIt = object.find("k");
            }
            if (kindIt == object.end())
            {
                kindIt = object.find("kind");
            }
            if (kindIt == object.end())
            {
                throw std::runtime_error("Attribute object missing kind");
            }
            const char *kindContext = kindIt->first == "t"   ? "attribute.t"
                                     : kindIt->first == "k" ? "attribute.k"
                                                            : "attribute.kind";
            AttributeKind kind = parseAttributeKind(kindIt->second.asString(kindContext));

            auto valueIt = object.find("v");
            if (valueIt == object.end())
            {
                valueIt = object.find("value");
            }

            auto valuesIt = object.find("vs");
            if (valuesIt == object.end())
            {
                valuesIt = object.find("values");
            }

            auto validateAndReturn = [](AttributeValue attr) -> AttributeValue
            {
                if (!attributeValueIsJsonSerializable(attr))
                {
                    throw std::runtime_error("Attribute value is not JSON serializable");
                }
                return attr;
            };

            switch (kind)
            {
            case AttributeKind::Bool:
                if (valueIt == object.end())
                {
                    throw std::runtime_error("Bool attribute missing value");
                }
                return validateAndReturn(AttributeValue{valueIt->second.asBool("attribute.v")});
            case AttributeKind::Int:
                if (valueIt == object.end())
                {
                    throw std::runtime_error("Int attribute missing value");
                }
                return validateAndReturn(AttributeValue{valueIt->second.asInt("attribute.v")});
            case AttributeKind::Double:
                if (valueIt == object.end())
                {
                    throw std::runtime_error("Double attribute missing value");
                }
                return validateAndReturn(AttributeValue{valueIt->second.asDouble("attribute.v")});
            case AttributeKind::String:
                if (valueIt == object.end())
                {
                    throw std::runtime_error("String attribute missing value");
                }
                return validateAndReturn(AttributeValue{valueIt->second.asString("attribute.v")});
            case AttributeKind::BoolArray:
            {
                if (valuesIt == object.end())
                {
                    throw std::runtime_error("Bool array attribute missing values");
                }
                std::vector<bool> arr;
                for (const auto &entry : valuesIt->second.asArray("attribute.vs"))
                {
                    arr.push_back(entry.asBool("attribute.vs[]"));
                }
                return validateAndReturn(AttributeValue{arr});
            }
            case AttributeKind::IntArray:
            {
                if (valuesIt == object.end())
                {
                    throw std::runtime_error("Int array attribute missing values");
                }
                std::vector<int64_t> arr;
                for (const auto &entry : valuesIt->second.asArray("attribute.vs"))
                {
                    arr.push_back(entry.asInt("attribute.vs[]"));
                }
                return validateAndReturn(AttributeValue{arr});
            }
            case AttributeKind::DoubleArray:
            {
                if (valuesIt == object.end())
                {
                    throw std::runtime_error("Double array attribute missing values");
                }
                std::vector<double> arr;
                for (const auto &entry : valuesIt->second.asArray("attribute.vs"))
                {
                    arr.push_back(entry.asDouble("attribute.vs[]"));
                }
                return validateAndReturn(AttributeValue{arr});
            }
            case AttributeKind::StringArray:
            {
                if (valuesIt == object.end())
                {
                    throw std::runtime_error("String array attribute missing values");
                }
                std::vector<std::string> arr;
                for (const auto &entry : valuesIt->second.asArray("attribute.vs"))
                {
                    arr.push_back(entry.asString("attribute.vs[]"));
                }
                return validateAndReturn(AttributeValue{arr});
            }
            }
            throw std::runtime_error("Unhandled attribute kind");
        }

        std::optional<SrcLoc> parseSrcLoc(const JsonValue &json, std::string_view context)
        {
            const auto &object = json.asObject(context);
            SrcLoc info{};

            if (auto fileIt = object.find("file"); fileIt != object.end())
            {
                info.file = fileIt->second.asString(std::string(context) + ".file");
            }

            if (auto lineIt = object.find("line"); lineIt != object.end())
            {
                info.line = static_cast<uint32_t>(lineIt->second.asInt(std::string(context) + ".line"));
            }

            if (auto colIt = object.find("col"); colIt != object.end())
            {
                info.column = static_cast<uint32_t>(colIt->second.asInt(std::string(context) + ".col"));
            }

            if (auto endLineIt = object.find("endLine"); endLineIt != object.end())
            {
                info.endLine = static_cast<uint32_t>(endLineIt->second.asInt(std::string(context) + ".endLine"));
            }

            if (auto endColIt = object.find("endCol"); endColIt != object.end())
            {
                info.endColumn = static_cast<uint32_t>(endColIt->second.asInt(std::string(context) + ".endCol"));
            }

            if (info.file.empty() && info.line == 0)
            {
                return std::nullopt;
            }
            return info;
        }

    } // namespace

    grh::ir::Netlist LoadJson::load(std::string_view json)
    {
        JsonValue root = parseJson(json);
        const auto &rootObj = root.asObject("netlist");

        Netlist netlist;
        auto graphsIt = rootObj.find("graphs");
        if (graphsIt == rootObj.end())
        {
            throw std::runtime_error("Netlist JSON missing graphs field");
        }
        const auto &graphsArray = graphsIt->second.asArray("graphs");
        for (const auto &graphValue : graphsArray)
        {
            const auto &graphObj = graphValue.asObject("graph");
            auto symbolIt = graphObj.find("symbol");
            if (symbolIt == graphObj.end())
            {
                throw std::runtime_error("Graph JSON missing symbol");
            }
            Graph &graph = netlist.createGraph(symbolIt->second.asString("graph.symbol"));

            const auto valuesIt = graphObj.find("vals");
            if (valuesIt == graphObj.end())
            {
                throw std::runtime_error("Graph JSON missing vals array");
            }

            std::unordered_map<std::string, ValueId> valueBySymbol;
            std::unordered_set<uint32_t> declaredInputs;
            std::unordered_set<uint32_t> declaredOutputs;
            std::unordered_set<uint32_t> declaredInouts;

            const auto &valuesArray = valuesIt->second.asArray("graph.vals");
            for (const auto &valueEntry : valuesArray)
            {
                const auto &valueObj = valueEntry.asObject("value");
                const auto &symbol = valueObj.at("sym").asString("value.sym");
                int64_t width = valueObj.at("w").asInt("value.w");
                bool isSigned = valueObj.at("sgn").asBool("value.sgn");
                SymbolId valueSym = graph.internSymbol(symbol);
                ValueId valueId = graph.createValue(valueSym, static_cast<int32_t>(width), isSigned);
                valueBySymbol.emplace(symbol, valueId);

                bool isInput = valueObj.at("in").asBool("value.in");
                bool isOutput = valueObj.at("out").asBool("value.out");
                bool isInout = false;
                if (auto inoutIt = valueObj.find("inout"); inoutIt != valueObj.end())
                {
                    isInout = inoutIt->second.asBool("value.inout");
                }
                if ((isInput && isOutput) || (isInout && (isInput || isOutput)))
                {
                    throw std::runtime_error("Value cannot be both input/output/inout in JSON");
                }

                if (isInput)
                {
                    declaredInputs.insert(valueSym.value);
                }
                if (isOutput)
                {
                    declaredOutputs.insert(valueSym.value);
                }
                if (isInout)
                {
                    declaredInouts.insert(valueSym.value);
                }

                if (auto dbgIt = valueObj.find("loc"); dbgIt != valueObj.end())
                {
                    if (auto loc = parseSrcLoc(dbgIt->second, "value.loc"))
                    {
                        graph.setValueSrcLoc(valueId, std::move(*loc));
                    }
                }
            }

            if (auto portsIt = graphObj.find("ports"); portsIt != graphObj.end())
            {
                const auto &portsObj = portsIt->second.asObject("graph.ports");
                auto parsePortArray = [&](std::string_view key, bool isInput)
                {
                    auto arrayIt = portsObj.find(std::string(key));
                    if (arrayIt == portsObj.end())
                    {
                        return;
                    }
                    const auto &portArray = arrayIt->second.asArray(std::string("graph.ports.") + std::string(key));
                    for (const auto &entry : portArray)
                    {
                        const auto &portObj = entry.asObject("graph.port");
                        auto nameField = portObj.find("name");
                        auto valField = portObj.find("val");
                        if (nameField == portObj.end() || valField == portObj.end())
                        {
                            throw std::runtime_error("Port entry missing name or val");
                        }
                        const std::string valueName = valField->second.asString("graph.port.val");
                        auto valueIt = valueBySymbol.find(valueName);
                        if (valueIt == valueBySymbol.end())
                        {
                            throw std::runtime_error("Port references unknown value: " + valueName);
                        }
                        SymbolId portName = graph.internSymbol(nameField->second.asString("graph.port.name"));
                        if (isInput)
                        {
                            graph.bindInputPort(portName, valueIt->second);
                        }
                        else
                        {
                            graph.bindOutputPort(portName, valueIt->second);
                        }
                    }
                };
                parsePortArray("in", true);
                parsePortArray("out", false);

                auto inoutIt = portsObj.find("inout");
                if (inoutIt != portsObj.end())
                {
                    const auto &portArray = inoutIt->second.asArray("graph.ports.inout");
                    for (const auto &entry : portArray)
                    {
                        const auto &portObj = entry.asObject("graph.inout_port");
                        auto nameField = portObj.find("name");
                        auto inField = portObj.find("in");
                        auto outField = portObj.find("out");
                        auto oeField = portObj.find("oe");
                        if (nameField == portObj.end() || inField == portObj.end() ||
                            outField == portObj.end() || oeField == portObj.end())
                        {
                            throw std::runtime_error("Inout port entry missing name/in/out/oe");
                        }
                        const std::string inName = inField->second.asString("graph.port.in");
                        const std::string outName = outField->second.asString("graph.port.out");
                        const std::string oeName = oeField->second.asString("graph.port.oe");
                        auto inIt = valueBySymbol.find(inName);
                        auto outIt = valueBySymbol.find(outName);
                        auto oeIt = valueBySymbol.find(oeName);
                        if (inIt == valueBySymbol.end() || outIt == valueBySymbol.end() ||
                            oeIt == valueBySymbol.end())
                        {
                            throw std::runtime_error("Inout port references unknown value");
                        }
                        SymbolId portName = graph.internSymbol(nameField->second.asString("graph.port.name"));
                        graph.bindInoutPort(portName, inIt->second, outIt->second, oeIt->second);
                    }
                }
            }
            else
            {
                throw std::runtime_error("Graph JSON missing ports object");
            }

            for (const auto &port : graph.inputPorts())
            {
                Value value = graph.getValue(port.value);
                if (!declaredInputs.contains(value.symbol().value))
                {
                    throw std::runtime_error("Input port missing isInput=true flag for value: " + std::string(graph.symbolText(value.symbol())));
                }
            }
            for (const auto &symbolValue : declaredInputs)
            {
                SymbolId sym{symbolValue};
                ValueId valueId = graph.findValue(sym);
                if (!valueId.valid())
                {
                    throw std::runtime_error("Value marked in=true but not bound to input port");
                }
                Value value = graph.getValue(valueId);
                if (!value.isInput())
                {
                    throw std::runtime_error("Value marked in=true but not bound to input port: " + std::string(graph.symbolText(sym)));
                }
            }

            for (const auto &port : graph.outputPorts())
            {
                Value value = graph.getValue(port.value);
                if (!declaredOutputs.contains(value.symbol().value))
                {
                    throw std::runtime_error("Output port missing isOutput=true flag for value: " + std::string(graph.symbolText(value.symbol())));
                }
            }
            for (const auto &symbolValue : declaredOutputs)
            {
                SymbolId sym{symbolValue};
                ValueId valueId = graph.findValue(sym);
                if (!valueId.valid())
                {
                    throw std::runtime_error("Value marked out=true but not bound to output port");
                }
                Value value = graph.getValue(valueId);
                if (!value.isOutput())
                {
                    throw std::runtime_error("Value marked out=true but not bound to output port: " + std::string(graph.symbolText(sym)));
                }
            }

            for (const auto &port : graph.inoutPorts())
            {
                Value inValue = graph.getValue(port.in);
                Value outValue = graph.getValue(port.out);
                Value oeValue = graph.getValue(port.oe);
                if (inValue.isInput() || inValue.isOutput() || outValue.isInput() ||
                    outValue.isOutput() || oeValue.isInput() || oeValue.isOutput())
                {
                    throw std::runtime_error("Inout port values must not be marked as input/output");
                }
                if (!inValue.isInout() || !outValue.isInout() || !oeValue.isInout())
                {
                    throw std::runtime_error("Inout port values must be marked as inout");
                }
            }

            for (const auto &symbolValue : declaredInouts)
            {
                SymbolId sym{symbolValue};
                ValueId valueId = graph.findValue(sym);
                if (!valueId.valid())
                {
                    throw std::runtime_error("Value marked inout=true not found in graph");
                }
                bool seen = false;
                for (const auto &port : graph.inoutPorts())
                {
                    if (port.in == valueId || port.out == valueId || port.oe == valueId)
                    {
                        seen = true;
                        break;
                    }
                }
                if (!seen)
                {
                    throw std::runtime_error("Value marked inout=true but not bound to inout port");
                }
            }

            if (auto opsIt = graphObj.find("ops"); opsIt != graphObj.end())
            {
                for (const auto &opEntry : opsIt->second.asArray("graph.ops"))
                {
                    const auto &opObj = opEntry.asObject("operation");
                    auto kindIt = opObj.find("kind");
                    if (kindIt == opObj.end())
                    {
                        throw std::runtime_error("Operation missing kind");
                    }
                    auto kind = parseOperationKind(kindIt->second.asString("operation.kind"));
                    if (!kind)
                    {
                        throw std::runtime_error("Unknown operation kind: " + kindIt->second.asString("operation.kind"));
                    }

                    auto symIt = opObj.find("sym");
                    if (symIt == opObj.end())
                    {
                        throw std::runtime_error("Operation missing symbol");
                    }

                    const std::string opSymbol = symIt->second.asString("operation.sym");
                    SymbolId opSym = graph.internSymbol(opSymbol);
                    OperationId opId = graph.createOperation(*kind, opSym);

                    auto parseSymbolList = [&](std::string_view key, std::string_view context) -> std::vector<std::string>
                    {
                        std::vector<std::string> entries;
                        if (auto it = opObj.find(std::string(key)); it != opObj.end())
                        {
                            for (const auto &entry : it->second.asArray(std::string(context)))
                            {
                                entries.push_back(entry.asString(std::string(context) + "[]"));
                            }
                        }
                        return entries;
                    };

                    for (const auto &symbol : parseSymbolList("in", "operation.in"))
                    {
                        auto valueIt = valueBySymbol.find(symbol);
                        if (valueIt == valueBySymbol.end())
                        {
                            throw std::runtime_error("Operand references unknown value: " + symbol);
                        }
                        graph.addOperand(opId, valueIt->second);
                    }

                    for (const auto &symbol : parseSymbolList("out", "operation.out"))
                    {
                        auto valueIt = valueBySymbol.find(symbol);
                        if (valueIt == valueBySymbol.end())
                        {
                            throw std::runtime_error("Result references unknown value: " + symbol);
                        }
                        graph.addResult(opId, valueIt->second);
                    }

                    if (auto attrsIt = opObj.find("attrs"); attrsIt != opObj.end())
                    {
                        for (const auto &[attrName, attrValue] : attrsIt->second.asObject("operation.attrs"))
                        {
                            graph.setAttr(opId, attrName, parseAttributeValue(attrValue));
                        }
                    }

                    if (auto dbgIt = opObj.find("loc"); dbgIt != opObj.end())
                    {
                        if (auto loc = parseSrcLoc(dbgIt->second, "operation.loc"))
                        {
                            graph.setOpSrcLoc(opId, std::move(*loc));
                        }
                    }
                }
            }
        }

        if (auto topIt = rootObj.find("tops"); topIt != rootObj.end())
        {
            for (const auto &entry : topIt->second.asArray("netlist.tops"))
            {
                netlist.markAsTop(entry.asString("netlist.tops[]"));
            }
        }

        return netlist;
    }

} // namespace grh::ir::load

namespace grh::ir
{

    Netlist Netlist::fromJsonString(std::string_view json)
    {
        grh::ir::load::LoadJson loader;
        return loader.load(json);
    }

} // namespace grh::ir
