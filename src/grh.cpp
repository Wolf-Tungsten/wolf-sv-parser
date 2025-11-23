#include "grh.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "slang/text/Json.h"

namespace wolf_sv::grh
{

    bool attributeValueIsJsonSerializable(const AttributeValue &value)
    {
        struct Visitor
        {
            bool operator()(bool) const noexcept { return true; }
            bool operator()(int64_t) const noexcept { return true; }
            bool operator()(double v) const noexcept { return std::isfinite(v); }
            bool operator()(const std::string &) const noexcept { return true; }
            bool operator()(const std::vector<bool> &) const noexcept { return true; }
            bool operator()(const std::vector<int64_t> &) const noexcept { return true; }
            bool operator()(const std::vector<double> &arr) const noexcept
            {
                for (double entry : arr)
                {
                    if (!std::isfinite(entry))
                    {
                        return false;
                    }
                }
                return true;
            }
            bool operator()(const std::vector<std::string> &) const noexcept { return true; }
        };

        return std::visit(Visitor{}, value);
    }

    Netlist::Netlist(Netlist &&other) noexcept
    {
        *this = std::move(other);
    }

    Netlist &Netlist::operator=(Netlist &&other) noexcept
    {
        if (this != &other)
        {
            graphs_ = std::move(other.graphs_);
            graphByName_ = std::move(other.graphByName_);
            graphAliasByName_ = std::move(other.graphAliasByName_);
            topGraphs_ = std::move(other.topGraphs_);
            resetGraphOwners();

            other.graphByName_.clear();
            other.graphAliasByName_.clear();
            other.topGraphs_.clear();
        }
        return *this;
    }

    void Netlist::resetGraphOwners()
    {
        for (auto &graphPtr : graphs_)
        {
            if (graphPtr)
            {
                graphPtr->owner_ = this;
            }
        }
    }

    namespace
    {

        template <class... Ts>
        struct Overloaded : Ts...
        {
            using Ts::operator()...;
        };
        template <class... Ts>
        Overloaded(Ts...) -> Overloaded<Ts...>;

        constexpr std::string_view kOperationNames[] = {
            "kConstant",
            "kAdd",
            "kSub",
            "kMul",
            "kDiv",
            "kMod",
            "kEq",
            "kNe",
            "kLt",
            "kLe",
            "kGt",
            "kGe",
            "kAnd",
            "kOr",
            "kXor",
            "kXnor",
            "kNot",
            "kLogicAnd",
            "kLogicOr",
            "kLogicNot",
            "kReduceAnd",
            "kReduceOr",
            "kReduceXor",
            "kReduceNor",
            "kReduceNand",
            "kReduceXnor",
            "kShl",
            "kLShr",
            "kAShr",
            "kMux",
            "kAssign",
            "kConcat",
            "kReplicate",
            "kSliceStatic",
            "kSliceDynamic",
            "kSliceArray",
            "kRegister",
            "kRegisterEn",
            "kRegisterRst",
            "kRegisterEnRst",
            "kRegisterARst",
            "kRegisterEnARst",
            "kMemory",
            "kMemoryAsyncReadPort",
            "kMemorySyncReadPort",
            "kMemoryWritePort",
            "kMemoryMaskWritePort",
            "kInstance",
            "kBlackbox",
            "kDisplay",
            "kAssert",
            "kDpicImport",
            "kDpicCall"};

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

        void writeAttributeValue(slang::JsonWriter &writer, const AttributeValue &value)
        {
            if (!attributeValueIsJsonSerializable(value))
            {
                throw std::runtime_error("Attribute value is not JSON serializable");
            }
            std::visit(
                Overloaded{
                    [&](bool v)
                    {
                        writer.writeProperty("k");
                        writer.writeValue(std::string_view("bool"));
                        writer.writeProperty("v");
                        writer.writeValue(v);
                    },
                    [&](int64_t v)
                    {
                        writer.writeProperty("k");
                        writer.writeValue(std::string_view("int"));
                        writer.writeProperty("v");
                        writer.writeValue(v);
                    },
                    [&](double v)
                    {
                        writer.writeProperty("k");
                        writer.writeValue(std::string_view("double"));
                        writer.writeProperty("v");
                        writer.writeValue(v);
                    },
                    [&](const std::string &v)
                    {
                        writer.writeProperty("k");
                        writer.writeValue(std::string_view("string"));
                        writer.writeProperty("v");
                        writer.writeValue(v);
                    },
                    [&](const std::vector<bool> &arr)
                    {
                        writer.writeProperty("k");
                        writer.writeValue(std::string_view("bool[]"));
                        writer.writeProperty("vs");
                        writer.startArray();
                        for (bool entry : arr)
                        {
                            writer.writeValue(entry);
                        }
                        writer.endArray();
                    },
                    [&](const std::vector<int64_t> &arr)
                    {
                        writer.writeProperty("k");
                        writer.writeValue(std::string_view("int[]"));
                        writer.writeProperty("vs");
                        writer.startArray();
                        for (int64_t entry : arr)
                        {
                            writer.writeValue(entry);
                        }
                        writer.endArray();
                    },
                    [&](const std::vector<double> &arr)
                    {
                        writer.writeProperty("k");
                        writer.writeValue(std::string_view("double[]"));
                        writer.writeProperty("vs");
                        writer.startArray();
                        for (double entry : arr)
                        {
                            writer.writeValue(entry);
                        }
                        writer.endArray();
                    },
                    [&](const std::vector<std::string> &arr)
                    {
                        writer.writeProperty("k");
                        writer.writeValue(std::string_view("string[]"));
                        writer.writeProperty("vs");
                        writer.startArray();
                        for (const auto &entry : arr)
                        {
                            writer.writeValue(entry);
                        }
                        writer.endArray();
                    }},
                value);
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

        void ensureGraphOwnership(const Graph &graph, const Value &value)
        {
            if (&value.graph() != &graph)
            {
                throw std::runtime_error("Value belongs to a different graph");
            }
        }

    } // namespace

    std::string_view toString(OperationKind kind) noexcept
    {
        auto index = static_cast<std::size_t>(kind);
        if (index >= std::size(kOperationNames))
        {
            return "<unknown>";
        }
        return kOperationNames[index];
    }

    std::optional<OperationKind> parseOperationKind(std::string_view text) noexcept
    {
        for (std::size_t i = 0; i < std::size(kOperationNames); ++i)
        {
            if (kOperationNames[i] == text)
            {
                return static_cast<OperationKind>(i);
            }
        }
        return std::nullopt;
    }

    Value::Value(Graph &graph, std::string symbol, int64_t width, bool isSigned) : graph_(&graph),
                                                                                   symbol_(std::move(symbol)),
                                                                                   width_(width),
                                                                                   isSigned_(isSigned)
    {
        if (symbol_.empty())
        {
            throw std::invalid_argument("Value symbol must not be empty");
        }
        if (width_ <= 0)
        {
            throw std::invalid_argument("Value width must be positive");
        }
    }

    void Value::setDefiningOp(Operation *op)
    {
        if (defineOp_ && defineOp_ != op)
        {
            throw std::runtime_error("Value already has a defining operation");
        }
        if (isInput_)
        {
            throw std::runtime_error("Input value cannot have a defining operation");
        }
        defineOp_ = op;
    }

    void Value::addUser(Operation *op, std::size_t operandIndex)
    {
        users_.push_back(ValueUser{.operation = op, .operandIndex = operandIndex});
    }

    void Value::removeUser(Operation *op, std::size_t operandIndex)
    {
        auto it = std::find_if(users_.begin(), users_.end(), [&](const ValueUser &user)
                               { return user.operation == op && user.operandIndex == operandIndex; });
        if (it == users_.end())
        {
            throw std::runtime_error("Value usage entry not found during removal");
        }
        users_.erase(it);
    }

    void Value::clearDefiningOp(Operation *op)
    {
        if (defineOp_ != op)
        {
            throw std::runtime_error("Defining operation mismatch during clear");
        }
        defineOp_ = nullptr;
    }

    void Value::setAsInput()
    {
        if (isOutput_)
        {
            throw std::runtime_error("Value cannot be both input and output");
        }
        if (defineOp_)
        {
            throw std::runtime_error("Input value cannot have a defining operation");
        }
        isInput_ = true;
    }

    void Value::setAsOutput()
    {
        if (isInput_)
        {
            throw std::runtime_error("Value cannot be both input and output");
        }
        isOutput_ = true;
    }

    Operation::Operation(Graph &graph, OperationKind kind, std::string symbol) : graph_(&graph),
                                                                                 kind_(kind),
                                                                                 symbol_(std::move(symbol))
    {
        if (symbol_.empty())
        {
            throw std::invalid_argument("Operation symbol must not be empty");
        }
    }

    void Operation::addOperand(Value &value)
    {
        ensureGraphOwnership(*graph_, value);
        operands_.push_back(&value);
        value.addUser(this, operands_.size() - 1);
    }

    void Operation::addResult(Value &value)
    {
        ensureGraphOwnership(*graph_, value);
        results_.push_back(&value);
        value.setDefiningOp(this);
    }

    void Operation::replaceOperand(std::size_t index, Value &value)
    {
        if (index >= operands_.size())
        {
            throw std::out_of_range("Operand index out of range");
        }
        ensureGraphOwnership(*graph_, value);
        Value *current = operands_[index];
        if (current == &value)
        {
            return;
        }
        value.addUser(this, index);
        current->removeUser(this, index);
        operands_[index] = &value;
    }

    void Operation::replaceResult(std::size_t index, Value &value)
    {
        if (index >= results_.size())
        {
            throw std::out_of_range("Result index out of range");
        }
        ensureGraphOwnership(*graph_, value);
        Value *current = results_[index];
        if (current == &value)
        {
            return;
        }
        value.setDefiningOp(this);
        current->clearDefiningOp(this);
        results_[index] = &value;
    }

    void Operation::setAttribute(std::string key, AttributeValue value)
    {
        if (key.empty())
        {
            throw std::invalid_argument("Attribute key must not be empty");
        }
        if (!attributeValueIsJsonSerializable(value))
        {
            throw std::invalid_argument("Attribute value must be JSON-serializable basic type or array");
        }
        attributes_.insert_or_assign(std::move(key), std::move(value));
    }

    Graph::Graph(Netlist &owner, std::string name) : owner_(&owner),
                                                     name_(std::move(name))
    {
        if (name_.empty())
        {
            throw std::invalid_argument("Graph name must not be empty");
        }
    }

    Value &Graph::addValueInternal(std::unique_ptr<Value> value)
    {
        auto *raw = value.get();
        auto [it, inserted] = valueBySymbol_.emplace(raw->symbol(), raw);
        if (!inserted)
        {
            throw std::runtime_error("Duplicated value symbol: " + raw->symbol());
        }
        values_.push_back(std::move(value));
        return *raw;
    }

    Operation &Graph::addOperationInternal(std::unique_ptr<Operation> op)
    {
        auto *raw = op.get();
        auto [it, inserted] = opBySymbol_.emplace(raw->symbol(), raw);
        if (!inserted)
        {
            throw std::runtime_error("Duplicated operation symbol: " + raw->symbol());
        }
        operations_.push_back(std::move(op));
        return *raw;
    }

    Value &Graph::createValue(std::string symbol, int64_t width, bool isSigned)
    {
        auto instance = std::unique_ptr<Value>(new Value(*this, std::move(symbol), width, isSigned));
        return addValueInternal(std::move(instance));
    }

    Operation &Graph::createOperation(OperationKind kind, std::string symbol)
    {
        auto instance = std::unique_ptr<Operation>(new Operation(*this, kind, std::move(symbol)));
        return addOperationInternal(std::move(instance));
    }

    void Graph::bindInputPort(std::string portName, Value &value)
    {
        ensureGraphOwnership(*this, value);
        if (inputPorts_.contains(portName))
        {
            throw std::runtime_error("Duplicated input port: " + portName);
        }
        value.setAsInput();
        inputPorts_.emplace(std::move(portName), &value);
    }

    void Graph::bindOutputPort(std::string portName, Value &value)
    {
        ensureGraphOwnership(*this, value);
        if (outputPorts_.contains(portName))
        {
            throw std::runtime_error("Duplicated output port: " + portName);
        }
        value.setAsOutput();
        outputPorts_.emplace(std::move(portName), &value);
    }

    Value *Graph::findValue(std::string_view symbol) noexcept
    {
        auto it = valueBySymbol_.find(std::string(symbol));
        if (it == valueBySymbol_.end())
        {
            return nullptr;
        }
        return it->second;
    }

    const Value *Graph::findValue(std::string_view symbol) const noexcept
    {
        auto it = valueBySymbol_.find(std::string(symbol));
        if (it == valueBySymbol_.end())
        {
            return nullptr;
        }
        return it->second;
    }

    Operation *Graph::findOperation(std::string_view symbol) noexcept
    {
        auto it = opBySymbol_.find(std::string(symbol));
        if (it == opBySymbol_.end())
        {
            return nullptr;
        }
        return it->second;
    }

    const Operation *Graph::findOperation(std::string_view symbol) const noexcept
    {
        auto it = opBySymbol_.find(std::string(symbol));
        if (it == opBySymbol_.end())
        {
            return nullptr;
        }
        return it->second;
    }

    void Graph::writeJson(slang::JsonWriter &writer) const
    {
        writer.startObject();
        writer.writeProperty("name");
        writer.writeValue(name_);

        writer.writeProperty("vals");
        writer.startArray();
        for (const auto &valuePtr : values_)
        {
            const Value &value = *valuePtr;
            writer.startObject();
            writer.writeProperty("sym");
            writer.writeValue(value.symbol());
            writer.writeProperty("w");
            writer.writeValue(value.width());
            writer.writeProperty("sgn");
            writer.writeValue(value.isSigned());
            writer.writeProperty("in");
            writer.writeValue(value.isInput());
            writer.writeProperty("out");
            writer.writeValue(value.isOutput());
            if (value.definingOp())
            {
                writer.writeProperty("def");
                writer.writeValue(value.definingOp()->symbol());
            }

            writer.writeProperty("users");
            writer.startArray();
            for (const auto &user : value.users())
            {
                writer.startObject();
                writer.writeProperty("op");
                writer.writeValue(user.operation->symbol());
                writer.writeProperty("idx");
                writer.writeValue(static_cast<int64_t>(user.operandIndex));
                writer.endObject();
            }
            writer.endArray();
            writer.endObject();
        }
        writer.endArray();

        writer.writeProperty("ports");
        writer.startObject();

        writer.writeProperty("in");
        writer.startArray();
        for (const auto &[name, value] : inputPorts_)
        {
            writer.startObject();
            writer.writeProperty("name");
            writer.writeValue(name);
            writer.writeProperty("val");
            writer.writeValue(value->symbol());
            writer.endObject();
        }
        writer.endArray();

        writer.writeProperty("out");
        writer.startArray();
        for (const auto &[name, value] : outputPorts_)
        {
            writer.startObject();
            writer.writeProperty("name");
            writer.writeValue(name);
            writer.writeProperty("val");
            writer.writeValue(value->symbol());
            writer.endObject();
        }
        writer.endArray();

        writer.endObject(); // ports

        writer.writeProperty("ops");
        writer.startArray();
        for (const auto &operationPtr : operations_)
        {
            const Operation &op = *operationPtr;
            writer.startObject();
            writer.writeProperty("sym");
            writer.writeValue(op.symbol());
            writer.writeProperty("kind");
            writer.writeValue(toString(op.kind()));

            writer.writeProperty("in");
            writer.startArray();
            for (const Value *operand : op.operands())
            {
                writer.writeValue(operand->symbol());
            }
            writer.endArray();

            writer.writeProperty("out");
            writer.startArray();
            for (const Value *result : op.results())
            {
                writer.writeValue(result->symbol());
            }
            writer.endArray();

            if (!op.attributes().empty())
            {
                writer.writeProperty("attrs");
                writer.startObject();
                for (const auto &[key, attrValue] : op.attributes())
                {
                    writer.writeProperty(key);
                    writer.startObject();
                    writeAttributeValue(writer, attrValue);
                    writer.endObject();
                }
                writer.endObject();
            }

            writer.endObject();
        }
        writer.endArray();

        writer.endObject();
    }

    Graph &Netlist::addGraphInternal(std::unique_ptr<Graph> graph)
    {
        auto *raw = graph.get();
        auto [it, inserted] = graphByName_.emplace(raw->name(), raw);
        if (!inserted)
        {
            throw std::runtime_error("Duplicated graph name: " + raw->name());
        }
        graphs_.push_back(std::move(graph));
        return *raw;
    }

    Graph &Netlist::createGraph(std::string name)
    {
        auto instance = std::make_unique<Graph>(*this, std::move(name));
        return addGraphInternal(std::move(instance));
    }

    Graph *Netlist::findGraph(std::string_view name) noexcept
    {
        std::string key(name);
        auto it = graphByName_.find(key);
        if (it != graphByName_.end())
        {
            return it->second;
        }
        auto aliasIt = graphAliasByName_.find(key);
        if (aliasIt != graphAliasByName_.end())
        {
            return aliasIt->second;
        }
        return nullptr;
    }

    const Graph *Netlist::findGraph(std::string_view name) const noexcept
    {
        std::string key(name);
        auto it = graphByName_.find(key);
        if (it != graphByName_.end())
        {
            return it->second;
        }
        auto aliasIt = graphAliasByName_.find(key);
        if (aliasIt != graphAliasByName_.end())
        {
            return aliasIt->second;
        }
        return nullptr;
    }

    void Netlist::registerGraphAlias(std::string alias, Graph &graph)
    {
        if (alias.empty())
        {
            return;
        }
        graphAliasByName_[std::move(alias)] = &graph;
    }

    void Netlist::markAsTop(std::string_view graphName)
    {
        if (!findGraph(graphName))
        {
            throw std::runtime_error("Cannot mark unknown graph as top: " + std::string(graphName));
        }
        auto nameStr = std::string(graphName);
        if (std::find(topGraphs_.begin(), topGraphs_.end(), nameStr) == topGraphs_.end())
        {
            topGraphs_.push_back(std::move(nameStr));
        }
    }

    namespace
    {

        std::vector<const Graph *> graphsSortedByName(const Netlist &netlist)
        {
            std::vector<const Graph *> graphs;
            graphs.reserve(netlist.graphs().size());
            for (const auto &graphPtr : netlist.graphs())
            {
                graphs.push_back(graphPtr.get());
            }
            std::sort(graphs.begin(), graphs.end(), [](const Graph *lhs, const Graph *rhs)
                      { return lhs->name() < rhs->name(); });
            return graphs;
        }

        std::vector<const Graph *> validateTopGraphsForJson(const Netlist &netlist,
                                                            std::span<const Graph *const> topGraphs)
        {
            std::vector<const Graph *> resolved;
            resolved.reserve(topGraphs.size());
            for (const Graph *graph : topGraphs)
            {
                if (graph == nullptr)
                {
                    throw std::runtime_error("Top graph list contains a null entry");
                }
                if (&graph->owner() != &netlist)
                {
                    throw std::runtime_error("Top graph belongs to a different netlist: " + graph->name());
                }
                resolved.push_back(graph);
            }
            return resolved;
        }

        std::vector<const Graph *> gatherTopGraphsFromNames(const Netlist &netlist,
                                                            const std::vector<std::string> &topNames)
        {
            std::vector<const Graph *> tops;
            tops.reserve(topNames.size());
            for (const auto &name : topNames)
            {
                const Graph *graph = netlist.findGraph(name);
                if (!graph)
                {
                    throw std::runtime_error("Top graph not found during serialization: " + name);
                }
                tops.push_back(graph);
            }
            return tops;
        }

    } // namespace

    std::string Netlist::toJsonString(std::span<const Graph *const> topGraphs, bool pretty) const
    {
        const std::vector<const Graph *> validatedTops = validateTopGraphsForJson(*this, topGraphs);

        slang::JsonWriter writer;
        writer.setPrettyPrint(pretty);
        writer.startObject();

        writer.writeProperty("graphs");
        writer.startArray();
        for (const Graph *graph : graphsSortedByName(*this))
        {
            graph->writeJson(writer);
        }
        writer.endArray();

        writer.writeProperty("tops");
        writer.startArray();
        for (const Graph *graph : validatedTops)
        {
            writer.writeValue(graph->name());
        }
        writer.endArray();

        writer.endObject();
        return std::string(writer.view());
    }

    std::string Netlist::toJsonString(bool pretty) const
    {
        return toJsonString(gatherTopGraphsFromNames(*this, topGraphs_), pretty);
    }

    AttributeValue parseAttributeValue(const JsonValue &jsonAttr)
    {
        const auto &object = jsonAttr.asObject("attribute");
        auto kindIt = object.find("k");
        if (kindIt == object.end())
        {
            kindIt = object.find("kind");
        }
        if (kindIt == object.end())
        {
            throw std::runtime_error("Attribute object missing kind");
        }
        const char *kindContext = kindIt->first == "k" ? "attribute.k" : "attribute.kind";
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

    Netlist Netlist::fromJsonString(std::string_view json)
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
            auto nameIt = graphObj.find("name");
            if (nameIt == graphObj.end())
            {
                throw std::runtime_error("Graph JSON missing name");
            }
            Graph &graph = netlist.createGraph(nameIt->second.asString("graph.name"));

            const JsonValue *valuesValue = nullptr;
            if (auto valsIt = graphObj.find("vals"); valsIt != graphObj.end())
            {
                valuesValue = &valsIt->second;
            }
            else if (auto valsIt = graphObj.find("values"); valsIt != graphObj.end())
            {
                valuesValue = &valsIt->second;
            }
            if (valuesValue == nullptr)
            {
                throw std::runtime_error("Graph JSON missing vals array");
            }

            std::unordered_set<std::string> declaredInputs;
            std::unordered_set<std::string> declaredOutputs;

            const auto &valuesArray = valuesValue->asArray("graph.vals");
            for (const auto &valueEntry : valuesArray)
            {
                const auto &valueObj = valueEntry.asObject("value");
                const auto &symbol = (valueObj.contains("sym") ? valueObj.at("sym") : valueObj.at("symbol")).asString("value.sym");
                int64_t width = (valueObj.contains("w") ? valueObj.at("w") : valueObj.at("width")).asInt("value.w");
                bool isSigned = (valueObj.contains("sgn") ? valueObj.at("sgn") : valueObj.at("signed")).asBool("value.sgn");
                Value &value = graph.createValue(symbol, width, isSigned);

                bool isInput = false;
                if (auto inIt = valueObj.find("in"); inIt != valueObj.end())
                {
                    isInput = inIt->second.asBool("value.in");
                }
                else
                {
                    isInput = valueObj.at("isInput").asBool("value.isInput");
                }

                bool isOutput = false;
                if (auto outIt = valueObj.find("out"); outIt != valueObj.end())
                {
                    isOutput = outIt->second.asBool("value.out");
                }
                else
                {
                    isOutput = valueObj.at("isOutput").asBool("value.isOutput");
                }
                if (isInput && isOutput)
                {
                    throw std::runtime_error("Value cannot be both input and output in JSON");
                }

                if (isInput)
                {
                    declaredInputs.insert(symbol);
                }
                if (isOutput)
                {
                    declaredOutputs.insert(symbol);
                }
            }

            bool parsedPortsObject = false;
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
                        Value *value = graph.findValue(valField->second.asString("graph.port.val"));
                        if (!value)
                        {
                            throw std::runtime_error("Port references unknown value: " + valField->second.asString("graph.port.val"));
                        }
                        if (isInput)
                        {
                            graph.bindInputPort(nameField->second.asString("graph.port.name"), *value);
                        }
                        else
                        {
                            graph.bindOutputPort(nameField->second.asString("graph.port.name"), *value);
                        }
                    }
                };
                parsePortArray("in", true);
                parsePortArray("out", false);
                parsedPortsObject = true;
            }

            if (!parsedPortsObject)
            {
                if (auto inputPortsIt = graphObj.find("inputPorts"); inputPortsIt != graphObj.end())
                {
                    for (const auto &[portName, symbolValue] : inputPortsIt->second.asObject("graph.inputPorts"))
                    {
                        Value *value = graph.findValue(symbolValue.asString("graph.inputPort.symbol"));
                        if (!value)
                        {
                            throw std::runtime_error("Input port references unknown value: " + symbolValue.asString("graph.inputPort.symbol"));
                        }
                        graph.bindInputPort(portName, *value);
                    }
                }

                if (auto outputPortsIt = graphObj.find("outputPorts"); outputPortsIt != graphObj.end())
                {
                    for (const auto &[portName, symbolValue] : outputPortsIt->second.asObject("graph.outputPorts"))
                    {
                        Value *value = graph.findValue(symbolValue.asString("graph.outputPort.symbol"));
                        if (!value)
                        {
                            throw std::runtime_error("Output port references unknown value: " + symbolValue.asString("graph.outputPort.symbol"));
                        }
                        graph.bindOutputPort(portName, *value);
                    }
                }
            }

            for (const auto &[_, value] : graph.inputPorts())
            {
                if (!declaredInputs.contains(value->symbol()))
                {
                    throw std::runtime_error("Input port missing isInput=true flag for value: " + value->symbol());
                }
            }
            for (const auto &symbol : declaredInputs)
            {
                const Value *value = graph.findValue(symbol);
                if (!value || !value->isInput())
                {
                    throw std::runtime_error("Value marked isInput=true but not bound to input port: " + symbol);
                }
            }

            for (const auto &[_, value] : graph.outputPorts())
            {
                if (!declaredOutputs.contains(value->symbol()))
                {
                    throw std::runtime_error("Output port missing isOutput=true flag for value: " + value->symbol());
                }
            }
            for (const auto &symbol : declaredOutputs)
            {
                const Value *value = graph.findValue(symbol);
                if (!value || !value->isOutput())
                {
                    throw std::runtime_error("Value marked isOutput=true but not bound to output port: " + symbol);
                }
            }

            const JsonValue *opsValue = nullptr;
            if (auto opsIt = graphObj.find("ops"); opsIt != graphObj.end())
            {
                opsValue = &opsIt->second;
            }
            else if (auto opsIt = graphObj.find("operations"); opsIt != graphObj.end())
            {
                opsValue = &opsIt->second;
            }

            if (opsValue != nullptr)
            {
                for (const auto &opEntry : opsValue->asArray("graph.ops"))
                {
                    const auto &opObj = opEntry.asObject("operation");
                    const JsonValue *kindValue = nullptr;
                    if (auto kindIt = opObj.find("kind"); kindIt != opObj.end())
                    {
                        kindValue = &kindIt->second;
                    }
                    else if (auto typeIt = opObj.find("type"); typeIt != opObj.end())
                    {
                        kindValue = &typeIt->second;
                    }
                    if (!kindValue)
                    {
                        throw std::runtime_error("Operation missing kind");
                    }
                    auto kind = parseOperationKind(kindValue->asString("operation.kind"));
                    if (!kind)
                    {
                        throw std::runtime_error("Unknown operation kind: " + kindValue->asString("operation.kind"));
                    }

                    const JsonValue *symbolValue = nullptr;
                    if (auto symIt = opObj.find("sym"); symIt != opObj.end())
                    {
                        symbolValue = &symIt->second;
                    }
                    else if (auto symIt = opObj.find("symbol"); symIt != opObj.end())
                    {
                        symbolValue = &symIt->second;
                    }
                    if (!symbolValue)
                    {
                        throw std::runtime_error("Operation missing symbol");
                    }

                    Operation &op = graph.createOperation(*kind, symbolValue->asString("operation.symbol"));

                    auto parseSymbolList = [&](std::string_view primary,
                                               std::string_view legacy,
                                               std::string_view context) -> std::vector<std::string>
                    {
                        const JsonValue *listValue = nullptr;
                        if (auto it = opObj.find(std::string(primary)); it != opObj.end())
                        {
                            listValue = &it->second;
                        }
                        else if (auto it = opObj.find(std::string(legacy)); it != opObj.end())
                        {
                            listValue = &it->second;
                        }

                        std::vector<std::string> entries;
                        if (listValue)
                        {
                            for (const auto &entry : listValue->asArray(std::string(context)))
                            {
                                entries.push_back(entry.asString(std::string(context) + "[]"));
                            }
                        }
                        return entries;
                    };

                    for (const auto &symbol : parseSymbolList("in", "operands", "operation.in"))
                    {
                        Value *operand = graph.findValue(symbol);
                        if (!operand)
                        {
                            throw std::runtime_error("Operand references unknown value: " + symbol);
                        }
                        op.addOperand(*operand);
                    }

                    for (const auto &symbol : parseSymbolList("out", "results", "operation.out"))
                    {
                        Value *result = graph.findValue(symbol);
                        if (!result)
                        {
                            throw std::runtime_error("Result references unknown value: " + symbol);
                        }
                        op.addResult(*result);
                    }

                    const JsonValue *attrsValue = nullptr;
                    if (auto attrsIt = opObj.find("attrs"); attrsIt != opObj.end())
                    {
                        attrsValue = &attrsIt->second;
                    }
                    else if (auto attrsIt = opObj.find("attributes"); attrsIt != opObj.end())
                    {
                        attrsValue = &attrsIt->second;
                    }

                    if (attrsValue)
                    {
                        for (const auto &[attrName, attrValue] : attrsValue->asObject("operation.attrs"))
                        {
                            op.setAttribute(attrName, parseAttributeValue(attrValue));
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
        else if (auto topLegacyIt = rootObj.find("topGraphs"); topLegacyIt != rootObj.end())
        {
            for (const auto &entry : topLegacyIt->second.asArray("netlist.topGraphs"))
            {
                netlist.markAsTop(entry.asString("netlist.topGraphs[]"));
            }
        }

        return netlist;
    }

} // namespace wolf_sv::grh
