# OpenC910 调试方案

测试命令 make run_c910_test

建立孤立测试现场

1.在 tests/data/openc910/bug_cases/ 目录下新建一个测试用例目录 假设为 case_001
2.最小化提取涉及的输入 verilog 文件和依赖的文件，编写一个 filelist.f 文件、Makefile 文件和一个简单的 testbench 文件
3.对最小化的模块进行wolf-sv-parser 处理，定位问题，形成错误报告和修复方案，写入 case_001/bug_report.md 文件

最小化提取方法参考

1.从报错符号入手（例如 ifu_icache_tag_wen_hold_0），定位相关模块（如 ct_ifu_icache_if）
2.只保留该模块源码，其他子模块用本地桩模块 stub_modules.v 代替（端口保持一致即可）
3.testbench 直接实例化该模块，所有输入拉低，输出悬空即可
4.filelist.f 仅包含 stub_modules.v、目标模块和 testbench
5.Makefile 直接调用 wolf-sv-parser 处理 filelist.f，保持复现稳定后再进一步裁剪
