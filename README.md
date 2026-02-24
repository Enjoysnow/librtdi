# librtdi – C++20 运行时依赖注入框架

`librtdi` 是一个参考 .NET `Microsoft.Extensions.DependencyInjection` 设计语义的 C++20 DI/IoC 框架，提供：

- 运行时类型擦除（`erased_ptr` + `type_index`）
- 零宏依赖声明（`deps<>`）
- 生命周期管理（`singleton` / `transient`）
- 四槽位模型（单实例 + 集合 × singleton + transient）
- 多实现、keyed 注册、forward、decorator
- `build()` 阶段校验（缺失依赖、循环依赖、生命周期违规）
- 多重继承与虚拟继承支持

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
    r->get<IGreeter>().greet();  // 返回 IGreeter&
}
```

### 带依赖的注册（`deps<>`）

```cpp
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

// 依赖通过构造函数引用注入
struct MyService : IService {
    ILogger& logger_;
    explicit MyService(ILogger& logger) : logger_(logger) {}
    void run() override { logger_.log("running"); }
};

int main() {
    using namespace librtdi;
    registry reg;
    reg.add_singleton<ILogger, ConsoleLogger>();
    reg.add_singleton<IService, MyService>(deps<ILogger>);
    //                                     ^^^^^^^^^^^
    //  deps<ILogger> → 注入 ILogger&（singleton 引用）

    auto r = reg.build();
    r->get<IService>().run();
}
```

## 核心概念

### 生命周期

| 枚举值 | 含义 | 解析 API |
|--------|------|----------|
| `singleton` | 全局唯一，首次解析时懒创建 | `get<T>()` → `T&` |
| `transient` | 每次解析创建新实例 | `create<T>()` → `unique_ptr<T>` |

### 四槽位模型

同一 `(type, key)` 可拥有最多 4 个独立槽位：

| 槽位 | 注册方法 | 注入类型 |
|------|----------|----------|
| 单例单实例 | `add_singleton<I,T>()` | `T&` |
| 瞬态单实例 | `add_transient<I,T>()` | `unique_ptr<T>` |
| 单例集合 | `add_collection<I,T>(singleton)` | `vector<T*>` |
| 瞬态集合 | `add_collection<I,T>(transient)` | `vector<unique_ptr<T>>` |

### 依赖标记

`deps<>` 中每个类型参数决定注入方式：

| 标记 | 注入类型 | 解析方法 |
|------|----------|----------|
| `T` / `singleton<T>` | `T&` | `get<T>()` |
| `transient<T>` | `unique_ptr<T>` | `create<T>()` |
| `collection<T>` | `vector<T*>` | `get_all<T>()` |
| `collection<transient<T>>` | `vector<unique_ptr<T>>` | `create_all<T>()` |

## 注册 API

### 单实例注册

```cpp
using namespace librtdi;
registry reg;

// 无依赖 singleton
reg.add_singleton<IFoo, FooImpl>();

// 带依赖 singleton
reg.add_singleton<IBar, BarImpl>(deps<IFoo, transient<IBaz>>);

// 无依赖 transient
reg.add_transient<IFoo, FooImpl>();

// 带依赖 transient
reg.add_transient<IBar, BarImpl>(deps<IFoo>);
```

### 集合注册

```cpp
// 多个实现注册到同一接口（集合槽位，可自由追加）
reg.add_collection<IPlugin, PluginA>(lifetime_kind::singleton);
reg.add_collection<IPlugin, PluginB>(lifetime_kind::singleton);

// 消费方通过 collection<IPlugin> 获取所有实现
reg.add_singleton<PluginManager, PluginManager>(deps<collection<IPlugin>>);
```

### Keyed 注册

```cpp
reg.add_singleton<ICache, RedisCache>("redis");
reg.add_singleton<ICache, MemCache>("memory");

auto r = reg.build({.validate_on_build = false});
auto& redis = r->get<ICache>("redis");
auto& mem   = r->get<ICache>("memory");
```

### 转发注册（Forward）

```cpp
struct Impl : IA, IB { /* ... */ };

reg.add_singleton<
reg.forward<IA, Impl>();  // IA 的 singleton 共享 Impl 的实例
reg.forward<IB, Impl>();  // IB 同上
```

### 装饰器（Decorator）

```cpp
struct LoggingFoo : IFoo {
    std::unique_ptr<IFoo> inner_;
    explicit Loggi
        : inner_(std::move(inner))
    void do_something() override {
        std::cout << "before\n";
        inner_->do_something();
        std::cout 
    }
};

reg.add_singleton<IFoo, FooImpl>();
reg.decorate<IFoo, LoggingFoo>();
```

## 解析 API

```cpp
auto r = reg.build()

// Singleton（引用生命周期�

auto* ptr = r->

// Transient
auto
auto opt = r->try_create<IBar>();    // unique_ptr<IBar> 或空

// 
auto all  = r->get_all<IPlugin>();       // vector<IPlugin*>
auto allT
```

## 构建期校验

`build()` 在构建 resolver 前自动执行校验：

```cpp
auto r = reg.build();  // 默认�
```

可选控制：

```cpp
auto r = reg.build({
    .validate_on_build  = true,   // 总开关
    .validate_lifetimes = true,   // 检查 captive dependency
    .detect_cycles      = true,   // 检查循环依赖
});
```

校验顺序：缺失依赖 → 生命周期兼容性 → 循环依赖。

## 继承模型

librtdi 支持所有 

| 继承模型 | 支持 | 说明
|----------|------|------|
| 单继承 | ✓ | 标准场景 |
| 多重继承 | ✓ | 通过 `make_erase
| �
| 菱形继承 | 

**要求**：当 `TInterface != T

## 异常体系

```
std::runtime_error
  └─ di_error                     ← 所有 DI 异常的基类
       ├─ not_found               ← 未找到注册
       ├─ cyclic_dependency       ← 循环依赖
       ├─ lifetime_mismatch       ← 生命周期违规
       ├─ dup
       └─ resolution_error        ← 工厂执行时异常的包装
```

所有异常消息均包含 demangled 类型名和源码位置。

## 线程安全

- **注册阶段**：`registry` 假定单线程使用
- **解析阶段**：`resolver` 可多线程并发使用；singleton 通过 `recursive_mutex` 保证 once-per-descriptor 语义

## 构建

```bash
cmake -B build -G Ninja
cm

# 运行测试
ctest --test-dir build --output-on-failure

# 打包
cmake --build build --target package
```

### 下游集成

安装后，通过标准 CMake �

```cmake
find_package(librtdi CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE librtdi::librtdi)
```

## 完整示例

参见 [examples/basic_usage.cpp](examples/basic_usage.cpp)，演示了 singleton、deps 注入、集合、decorator 的组合使用。

## 项目结构

```text
librtdi/
├── CMakeLists.txt
├── README.md
├── REQUIREMENTS.md
├── include/librtdi/
│   ├── descriptor.hpp
│   ├── exceptions.hpp
│   ├── registry.hpp
│   ├── resolver.hpp
│   ├── scope.hpp
│   └── type_traits.hpp
├── src/
│   ├── registry.cpp
│   ├── resolver.cpp
│   ├── scope.cpp
│   ├── validation.cpp
│   └── exceptions.cpp
├── tests/
│   ├── test_registration.cpp
│   ├── test_resolution.cpp
│   ├── test_lifetime.cpp
│   ├── test_multi_impl.cpp
│   ├── test_validation.cpp
│   ├── test_diagnostics.cpp
│   ├── test_concurrency.cpp
│   ├── test_auto_wiring.cpp
│   ├── test_edge_cases.cpp
│   ├── test_keyed.cpp
│   ├── test_forward.cpp
│   ├── test_decorator.cpp
│   └── test_inheritance.cpp
└── examples/
    └── basic_usage.cpp
```
