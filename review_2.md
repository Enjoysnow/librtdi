# librtdi v2.1 需求与实现评审报告

经过对最新的 `REQUIREMENTS.md` (v2.1) 以及当前项目代码、构建脚本和测试的深入分析，发现了**一个致命的逻辑冲突**、**一个过于严格的约束**，以及**项目工程化上的几个脱节问题**。

以下是详细的评审报告：

## 1. 需求文档评审（歧义、缺漏与约束）

### 🔴 致命的逻辑冲突：Decorator 与 Forward Singleton 的所有权冲突
*   **问题描述**：
    *   **9.3 节**规定：装饰器构造函数必须接收 `unique_ptr<I>`（即**转移所有权**）。
    *   **8.3 节**规定：Forward Singleton 展开时，工厂返回的是**非所有权**的 `erased_ptr`（因为真正的实例由原始注册的缓存持有）。
    *   **冲突后果**：如果用户对一个被 Forward 的接口注册了装饰器，装饰器的 Wrapper 会将这个“非所有权”的指针强行转换为 `unique_ptr<I>`。当装饰器被销毁时，`unique_ptr` 会错误地 `delete` 掉全局共享的 Singleton 实例，导致 **Double Free 或 Use-After-Free (SIGSEGV)**。
*   **建议**：需求文档存在严重缺漏。必须明确规定“装饰器不能应用于 Forward Singleton”，或者修改装饰器的签名要求（例如 Singleton 装饰器接收 `I&`，Transient 接收 `unique_ptr<I>`）。

### ⚠️ 过于严格的约束：强制要求虚析构函数
*   **问题描述**：**2.2 节**和**13 节**强制要求当 `TInterface != TImpl` 时，`TInterface` 必须拥有虚析构函数。
*   **评审意见**：虽然这极大地简化了 `erased_ptr` 的 deleter 设计（直接 `delete static_cast<TInterface*>(p)` 即可完美处理多重/虚拟继承），但作为通用 DI 框架，这会阻止用户注册那些不拥有多态析构的 C API 包装类或第三方库接口。这是一个明确的 Trade-off，文档逻辑自洽，但对用户略显严苛。

### 💡 容易引起困惑的语义：集合依赖的生命周期匹配
*   **问题描述**：**4.1 节** `add_collection` 需要显式指定 `lifetime_kind`。如果用户注册了 `add_collection<I, T>(transient)`，但消费方写了 `deps<collection<I>>`（根据 2.4 节，这等同于请求 singleton 集合），构建期校验会报 `not_found`。
*   **评审意见**：文档逻辑自洽，但对用户心智负担较大。建议在文档或错误信息中明确提示“集合的生命周期必须与依赖声明完全匹配”。

---

## 2. 现有项目评审（构建、实现、测试、文档）

在确认了需求文档的上述情况后，对比了当前工作区中的代码实现，发现了以下不符合或需要改进的地方：

### 🔴 实现与测试：Bug 已在代码中兑现
*   **实现高度一致**：代码（`registry.hpp`, `registry.cpp`）非常精准地实现了 v2.1 需求文档中的四槽位模型、`erased_ptr`、`make_erased_as` 以及 `forward_deleter` 等核心机制。
*   **崩溃复现**：正如需求评审中指出的，代码中 `decorate` 的实现确实存在将非所有权 `erased_ptr` 转换为 `unique_ptr` 的问题。编写了一个 `test_forward_decorator.cpp` 测试用例，**运行后直接触发了 `SIGSEGV`（段错误）**，证实了需求设计上的这个致命缺陷已经反映到了代码中，且现有的测试套件（`tests/`）**遗漏了对 `forward` + `decorator` 组合的测试**。

### 📦 构建与打包：CMake 导出目标缺失
*   **问题描述**：在 `src/CMakeLists.txt` 中，使用了 `install(TARGETS librtdi EXPORT librtdi-targets ARCHIVE DESTINATION lib)`，但整个项目中**缺少**对应的 `install(EXPORT librtdi-targets ...)` 指令。
*   **后果**：这导致 CPack 打包生成的 `.deb` 或 `.tar.gz` 中没有 CMake 的 target 导出文件（`librtdi-targets.cmake` 和 `librtdiConfig.cmake`）。下游用户安装后，无法通过标准的 `find_package(librtdi)` 正常链接该库。

### 📄 文档：README.md 严重滞后
*   **问题描述**：根目录下的 `README.md` 仍然是旧版本的描述。
*   **不符合项**：
    *   仍然声称使用 `shared_ptr<void>`（v2.1 已改为 `erased_ptr`）。
    *   仍然声称支持 `scoped` 生命周期（v2.1 的 13 节已明确将其列为非目标）。
    *   仍然声称支持 `keyed` 命名注册（v2.1 的 13 节已将其列为非目标）。
    *   未提及全新的四槽位模型（单实例 vs 集合）和 `deps<collection<T>>` 等新语法。

## 总结建议
1. **首要任务**：修改需求文档中关于 Decorator 的设计，修复 Forward + Decorator 导致的所有权崩溃问题，并在代码中补充对应的单元测试。
2. **工程修复**：在 `src/CMakeLists.txt` 中补全 `install(EXPORT ...)` 逻辑，完善 CMake Package 的生成。
3. **文档同步**：全面重写 `README.md`，使其与 v2.1 需求文档的 `erased_ptr` 和四槽位模型保持一致。