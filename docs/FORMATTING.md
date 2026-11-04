# 源码格式

根目录的 `.clang-format` 统一使用四空格缩进、100 列和同行大括号。保留 include 顺序、注释内容和字符串字面量，避免改变 Windows 头文件依赖及原版文本字节。`.gitattributes` 将这些源码固定为 LF，防止 Windows 的 Git 换行转换导致格式检查反复报错。

VS Code 使用 Microsoft C/C++ 扩展（`ms-vscode.cpptools`）的 clang-format，C/C++ 文件保存时自动格式化。`.inc` 关联为 C++。仓库只共享 `.vscode/settings.json` 和扩展推荐，其他个人 IDE 文件继续忽略。

在项目根目录执行：

```powershell
python -m pip install -r requirements-format.txt

# Format first-party include/mikudancestudio, src and tests (including .inc/.mm)
python scripts/format_sources.py

# Check only; return a nonzero exit code on formatting violations
python scripts/format_sources.py --check

# Only changed first-party sources, including tests and new files
python scripts/format_sources.py --changed HEAD
python scripts/format_sources.py --check --changed HEAD

# Limit the operation to one subsystem
python scripts/format_sources.py src/frontend_qt

# Equivalent CMake targets (explicit, never part of a normal build)
cmake --build out/build --target format
cmake --build out/build --target format-check
```

开发和 CI 固定使用 `requirements-format.txt` 中的 clang-format 21.1.8。脚本依次查找 `--clang-format PATH`／`CLANG_FORMAT`、PATH、VS Code C/C++ 扩展及 Visual Studio 的 C++ Clang 工具。VS Code 可通过个人设置 `C_Cpp.clang_format_path` 指向同一个程序，避免扩展自带版本不同造成格式来回变化。

处理范围为 `include/mikudancestudio`、`src`、`tests` 中的 C/C++、Objective-C++ 和 `.inc`。第三方目录、构建产物、资源和 `.def` 不参与。CI 检查相对 PR 基线／推送前提交的改动文件；本批完整格式化了修改和拆分的文件，没有用无关的全仓格式变更掩盖功能改动。`format-check` 可检查全部历史源码，仍可能报告未在本批处理的格式债务。
