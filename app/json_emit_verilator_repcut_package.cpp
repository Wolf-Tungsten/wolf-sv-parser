#include "core/grh.hpp"
#include "emit/verilator_repcut_package.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream stream(path);
    if (!stream.is_open())
    {
        throw std::runtime_error("failed to open input json: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: json-emit-verilator-repcut-package <json_in> <output_dir> [top...]\n";
        return 1;
    }

    const std::filesystem::path jsonPath = std::filesystem::absolute(argv[1]);
    const std::filesystem::path outputDir = std::filesystem::absolute(argv[2]);
    std::vector<std::string> topOverrides;
    for (int i = 3; i < argc; ++i)
    {
        topOverrides.emplace_back(argv[i]);
    }

    try
    {
        const std::string text = readFile(jsonPath);
        const auto design = wolvrix::lib::grh::Design::fromJsonString(text);

        wolvrix::lib::emit::EmitDiagnostics diagnostics;
        wolvrix::lib::emit::EmitVerilatorRepCutPackage emitter(&diagnostics);
        wolvrix::lib::emit::EmitOptions options;
        options.outputDir = outputDir.string();
        options.topOverrides = std::move(topOverrides);

        const auto result = emitter.emit(design, options);
        if (!result.success || diagnostics.hasError())
        {
            for (const auto &diag : diagnostics.messages())
            {
                if (!diag.message.empty())
                {
                    std::cerr << diag.message;
                    if (!diag.context.empty())
                    {
                        std::cerr << " [" << diag.context << "]";
                    }
                    std::cerr << '\n';
                }
            }
            return 1;
        }
    }
    catch (const std::exception &ex)
    {
        std::cerr << ex.what() << '\n';
        return 1;
    }

    return 0;
}
