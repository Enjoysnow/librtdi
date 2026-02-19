# librtdi – C++20 运行时依赖注入框架

`librtdi` 是一个参考 .NET `Microsoft.Extensions.DependencyInjection` 设计语义的 C++20 DI/IoC 框架，提供：

- 运行时类型擦除（`shared_ptr<void>` + `type_index`）
- 零宏依赖声明（`deps<>`）
- 生命周期管理（`singleton` / `scoped` / `transient`）
- 多实现、keyed 注册、forward、decorator
- `build()` 阶段校验（缺失依赖、歧义依赖、循环依赖、生命周期违规）

## 快速开始

### 最小示例

```cpp
#include <librtdi.hpp>
#include <iostream>

struct IGreeter {
    virtual ~IGreeter() = default;
    virtual void greet() const = 0;
};

struct ConsoleGreeter : IGreeter {
    void greet() const override { std::cout << "Hello, DI!\n"; }
};

int main() {
    using namespace librtdi;

    registry reg;
    reg.add_singleton<IGreeter, ConsoleGreeter>();

    auto r = reg.build();
    r->resolve<IGreeter>()->greet();
}
```

### 带依赖的注册（`deps<>`）

```cpp
#include <librtdi.hpp>
#include <iostream>

struct ILogger {
    virtual ~ILogger() = default;
    virtual void log(std::string_view msg) const = 0;
};

struct ConsoleLogger : ILogger {
    void log(std::string_view msg) const override {
        std::cout << "[LOG] " << msg << "\n";
    }
};

struct IService {
    virtual ~IService() = default;
    virtual void run() = 0;
};

struct MyService : IService {
    std::shared_ptr<ILogger> logger;
    explicit MyService(std::shared_ptr<ILogger> l) : logger(std::move(l)) {}
    void run() override { logger->log("Running!"); }
};

int main() {
    using namespace librtdi;

    registry reg;
    reg.add_singleton<ILogger, ConsoleLogger>();
    reg.add_transient<IService, MyService>(deps<ILogger>);

    auto r = reg.build();
    r->resolve<IService>()->run();
}
```

## 实战示例

### 示例 1：请求级 scope（Web 场景）

```cpp
#include <librtdi.hpp>
#include <memory>

struct IRequestContext {
    virtual ~IRequestContext() = default;
    virtual std::string request_id() const = 0;
};

struct RequestContext : IRequestContext {
    std::string id;
    explicit RequestContext(std::string v = "req-001") : id(std::move(v)) {}
    std::string request_id() const override { return id; }
};

struct IHandler {
    virtual ~IHandler() = default;
    virtual void handle() = 0;
};

struct Handler : IHandler {
    std::shared_ptr<IRequestContext> ctx;
    explicit Handler(std::shared_ptr<IRequestContext> c) : ctx(std::move(c)) {}
    void handle() override {
        // 在同一 scope 内，ctx 始终是同一个实例
    }
};

int main() {
    using namespace librtdi;

    registry reg;
    reg.add_scoped<IRequestContext, RequestContext>();
    reg.add_transient<IHandler, Handler>(deps<IRequestContext>);

    auto root = reg.build();

    // 请求 A
    {
        auto scope = root->create_scope();
        auto& r = scope->get_resolver();
        auto h1 = r.resolve<IHandler>();
        auto h2 = r.resolve<IHandler>();
        (void)h1;
        (void)h2;
    }

    // 请求 B（新的 scoped 实例）
    {
        auto scope = root->create_scope();
        auto& r = scope->get_resolver();
        auto h = r.resolve<IHandler>();
        (void)h;
    }
}
```

### 示例 2：插件链（多实现 + resolve_all）

```cpp
#include <librtdi.hpp>
#include <memory>
#include <vector>

struct IPlugin {
    virtual ~IPlugin() = default;
    virtual void run() = 0;
};

struct PluginA : IPlugin { void run() override {} };
struct PluginB : IPlugin { void run() override {} };
struct PluginC : IPlugin { void run() override {} };

int main() {
    using namespace librtdi;

    registry reg;
    reg.add_singleton<IPlugin, PluginA>();
    reg.add_singleton<IPlugin, PluginB>();
    reg.add_singleton<IPlugin, PluginC>();

    auto r = reg.build();

    auto all = r->resolve_all<IPlugin>(); // A, B, C（注册顺序）
    for (auto& p : all) {
        p->run();
    }

    auto last = r->resolve_any<IPlugin>(); // last-wins -> PluginC
    (void)last;
}
```

### 示例 3：日志装饰链（decorate）

```cpp
#include <librtdi.hpp>
#include <memory>
#include <string_view>

struct ILogger {
    virtual ~ILogger() = default;
    virtual void log(std::string_view msg) const = 0;
};

struct ConsoleLogger : ILogger {
    void log(std::string_view) const override {}
};

struct TimestampLogger : ILogger {
    std::shared_ptr<ILogger> inner;
    explicit TimestampLogger(std::shared_ptr<ILogger> i) : inner(std::move(i)) {}
    void log(std::string_view msg) const override {
        // prepend timestamp
        inner->log(msg);
    }
};

struct PrefixLogger : ILogger {
    std::shared_ptr<ILogger> inner;
    explicit PrefixLogger(std::shared_ptr<ILogger> i) : inner(std::move(i)) {}
    void log(std::string_view msg) const override {
        // prepend [APP]
        inner->log(msg);
    }
};

int main() {
    using namespace librtdi;

    registry reg;
    reg.add_singleton<ILogger, ConsoleLogger>();
    reg.decorate<ILogger, TimestampLogger>(); // 内层
    reg.decorate<ILogger, PrefixLogger>();    // 外层

    auto r = reg.build();
    auto logger = r->resolve<ILogger>();
    logger->log("hello");
}
```

### 示例 4：keyed + `single/replace/skip` 策略对比

先看行为速表（同一接口 `IDb`，按 key 分槽位）：

| 操作 | 槽位状态 | 结果 |
|------|----------|------|
| `single` | 空槽位 | 写入并锁定 |
| `single` | 已锁定 | 抛 `duplicate_registration` |
| `replace` | 已锁定/未锁定 | 清空后写入（可覆盖 single 锁） |
| `skip` | 已有注册 | 不报错，直接跳过 |

```cpp
#include <librtdi.hpp>

struct IDb { virtual ~IDb() = default; };
struct SqliteDb : IDb {};
struct PostgresDb : IDb {};
struct MySqlDb : IDb {};

int main() {
    using namespace librtdi;
    using enum registration_policy;

    registry reg;

    // key="local"：single 首次写入并锁定
    reg.add_singleton<IDb, SqliteDb>("local", single);

    // 同 key 再次 single -> duplicate_registration
    // reg.add_singleton<IDb, PostgresDb>("local", single);

    // replace 可覆盖被 single 锁定的槽位
    reg.add_singleton<IDb, PostgresDb>("local", replace);

    // skip：若已有注册则静默跳过（local 仍是 PostgresDb）
    reg.add_singleton<IDb, MySqlDb>("local", skip);

    // 不同 key 是不同槽位，互不影响
    reg.add_singleton<IDb, SqliteDb>("report", single);

    auto r = reg.build();
    auto local  = r->resolve<IDb>("local");
    auto report = r->resolve<IDb>("report");
    (void)local;
    (void)report;
}
```

## 核心 API

### `registry`

- 生命周期注册：
  - `add_singleton<I, T>(...)`
  - `add_scoped<I, T>(...)`
  - `add_transient<I, T>(...)`
- 支持重载：
  - 零依赖（`T` 默认构造）
  - `deps<>` 声明依赖
  - keyed（首参 `std::string_view key`）
- 其他能力：
  - `forward<I, T>(registration_policy = multiple)`
  - `decorate<I, D>(...)`
  - `build(build_options = {})`
  - `descriptors()`（只读诊断）

### `resolver`

非 keyed：

- `resolve<T>()`：严格解析（0 抛 `not_found`，>1 抛 `ambiguous_component`）
- `try_resolve<T>()`：严格解析（0 返回 `nullptr`，>1 抛 `ambiguous_component`）
- `resolve_any<T>()`：last-wins（0 抛 `not_found`）
- `try_resolve_any<T>()`：last-wins（0 返回 `nullptr`）
- `resolve_all<T>()`：返回全部实现（可为空）

keyed：

- `resolve<T>(key)`
- `try_resolve<T>(key)`
- `resolve_any<T>(key)`
- `try_resolve_any<T>(key)`
- `resolve_all<T>(key)`

作用域：

- `create_scope()` 创建 RAII scope
- `scope::get_resolver()` 获取 scoped resolver

## 生命周期规则

`lifetime_kind`：

- `singleton`：全局唯一（根 resolver 缓存）
- `scoped`：单 scope 唯一（每个 scope 独立缓存）
- `transient`：每次解析新建

兼容性规则（`validate_scopes=true` 时校验）：

- `singleton` 只能依赖 `singleton`
- `scoped` 不能依赖 `transient`
- `transient` 可依赖任意生命周期

从根 resolver 解析 `scoped` 会抛 `no_active_scope`。

## 注册策略 `registration_policy`

- `multiple`（默认）：同槽位可多条注册
- `single`：锁定槽位（`(type, key)`）
- `replace`：清空槽位后写入新注册（可覆盖 `single` 锁）
- `skip`：槽位已有注册则跳过

### `single` 语义（实现细节）

针对同一 `(type, key)`：

- 0 条：新增并锁定
- 1 条且未锁：仅加锁，不新增
- 1 条且已锁：抛 `duplicate_registration`
- >1 条：抛 `duplicate_registration`

## Forward（转发）

`forward<I, T>()` 用于把接口 `I` 转发到目标类型 `T` 的注册结果，语义是“共享同一实例”。

- forward 描述符在 `build()` 阶段展开
- 若 `T` 有 N 条 non-keyed 注册，则生成 N 条 `I` 的注册
- 展开后 `I` 的生命周期复制自对应 `T`
- 内部使用两步 `static_pointer_cast` 处理多重继承指针偏移
- 当前仅支持 non-keyed forward

## Decorator（装饰器）

支持四种形式：

- `decorate<I, D>()`
- `decorate<I, D>(deps<Extra...>)`
- `decorate<I, D>(typeid(TargetImpl))`
- `decorate<I, D>(typeid(TargetImpl), deps<Extra...>)`

行为说明：

- 按注册顺序应用（后注册外层包装先注册内层）
- 默认作用于同接口下所有实现（包含 keyed 与 non-keyed）
- targeted decorate 仅作用于 `impl_type` 匹配项
- 装饰后生命周期保持原实现生命周期

## Build 校验

`build_options`：

```cpp
struct build_options {
    bool validate_on_build = true;
    bool validate_scopes   = true;
};
```

当 `validate_on_build=true` 时，`build()` 会执行：

1. 缺失依赖检查（`not_found`）
2. 歧义依赖检查（`ambiguous_component`）
3. 循环依赖检查（`cyclic_dependency`）
4. 生命周期检查（`lifetime_mismatch`，受 `validate_scopes` 控制）

说明：`deps<>` 依赖按 non-keyed 严格解析语义处理。

## 异常体系

- `di_error`（基类，包含 `source_location`）
- `not_found`
- `ambiguous_component`
- `cyclic_dependency`
- `lifetime_mismatch`
- `no_active_scope`
- `duplicate_registration`
- `resolution_error`

## 线程安全

- 注册阶段（`registry`）按单线程初始化假设
- 解析阶段支持并发
- singleton / scoped 缓存使用 `recursive_mutex`
- singleton 采用“构造期间持锁”，保证并发下槽位最多构造一次

## 已知限制（MVP）

- 仅支持构造函数注入
- `deps<>` 依赖要求构造参数为 `std::shared_ptr<TDep>`
- `deps<>` 仅走 non-keyed 图谱（keyed 依赖不参与 `deps<>` 校验）
- `forward` 仅支持 non-keyed
- 不保证 scoped 析构顺序为依赖图逆拓扑
- 依赖 `abi::__cxa_demangle`，当前目标为 GCC/Clang

## 构建与测试

环境要求：C++20，CMake ≥ 3.20，GCC ≥ 14 或 Clang ≥ 17

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

可选：

```bash
# 关闭告警集
cmake -B build -DLIBRTDI_ENABLE_WARNINGS=OFF

# 开启 Address/UB Sanitizer
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DLIBRTDI_ENABLE_SANITIZERS=ON
```

## 项目结构

```text
librtdi/
├── CMakeLists.txt
├── README.md
├── REQUIREMENTS.md
├── include/librtdi/
│   ├── librtdi.hpp
│   ├── descriptor.hpp
│   ├── registry.hpp
│   ├── resolver.hpp
│   ├── scope.hpp
│   └── exceptions.hpp
├── src/
│   ├── registry.cpp
│   ├── resolver.cpp
│   ├── scope.cpp
│   ├── validation.cpp
│   └── exceptions.cpp
└── tests/
    ├── test_registration.cpp
    ├── test_resolution.cpp
    ├── test_lifetime.cpp
    ├── test_scope.cpp
    ├── test_multi_impl.cpp
    ├── test_keyed.cpp
    ├── test_validation.cpp
    ├── test_diagnostics.cpp
    ├── test_concurrency.cpp
    ├── test_auto_wiring.cpp
    ├── test_edge_cases.cpp
    ├── test_policy.cpp
    ├── test_forward.cpp
    └── test_decorator.cpp
```

## License

MIT
