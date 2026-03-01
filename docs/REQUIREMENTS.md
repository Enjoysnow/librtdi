# 需求规格说明书

**项目名称**：librtdi — C++20 运行时依赖注入框架（MVP）  
**文档版本**：2.6  
**语言**：C++20  
**最后更新**：2026-02-26

---

## 目录

1. [背景与目标](#1-背景与目标)
2. [核心概念](#2-核心概念)
3. [组件描述符](#3-组件描述符)
4. [注册 API](#4-注册-api)
5. [类型擦除存储：erased_ptr](#5-类型擦除存储erased_ptr)
6. [生命周期语义](#6-生命周期语义)
7. [解析 API](#7-解析-api)
8. [转发注册（Forward）](#8-转发注册forward)
9. [装饰器（Decorator）](#9-装饰器decorator)
10. [构建期校验](#10-构建期校验)
11. [异常体系](#11-异常体系)
12. [线程安全](#12-线程安全)
13. [明确的非目标](#13-明确的非目标)

---

## 1. 背景与目标

### 1.1 动机

提供一个适用于 C++20 的**运行时**依赖注入框架，满足以下需求：

- 无需入侵式基类、无需通过宏声明依赖
- 在一个统一的注册 → 构建 → 解析流程中管理服务的生命周期
- 在 `build()` 阶段而非运行时的首次解析时发现配置错误

### 1.2 核心设计目标

| 目标 | 说明 |
|------|------|
| 运行时类型擦除 | 所有注册信息以运行时类型而非模板参数为 key 存储，支持大规模注册 |
| 零宏 | 依赖声明通过语言原生手段完成，不引入预处理器宏 |
| 构建期校验 | 依赖图的正确性（缺失、循环、生命周期违规）在 `build()` 时全部检测完毕 |
| 可组合性 | 转发、装饰、多实现等机制可任意组合，无特殊顺序限制 |
| 人因工程 | 错误消息应包含 demangled 类型名与源码位置，便于诊断 |
| 轻量级存储 | 使用 `erased_ptr`（原始函数指针 deleter）代替 `shared_ptr<void>` |
| 继承模型支持 | 支持单继承、多重继承与虚拟继承（virtual base），所有指针调整在注册时通过上行 `static_cast` 完成 |

---

## 2. 核心概念

### 2.1 框架整体流程

框架分为**三个阶段**，各阶段有明确边界：

```
注册阶段 (registry)
  └─ add_singleton / add_transient / add_collection / forward / decorate
        ↓
构建阶段 (registry::build)
  └─ 展开 forward → 应用 decorator → 校验依赖图 → 生成 resolver
        ↓
解析阶段 (resolver)
  └─ get / create / get_all / create_all
```

`registry::build()` 调用后，registry 本身的 descriptor 集合被移交给 resolver，registry 不再持有有效状态。`build()` 返回的 `resolver`（`shared_ptr<resolver>`）独立于 `registry` 存活；`registry` 可在 `build()` 后安全销毁。

`build()` 仅允许调用一次；第二次调用将抛出 `di_error`。`build()` 调用后，再调用任何注册方法（`add_singleton` / `add_transient` / `add_collection` / `forward` / `decorate`）同样抛出 `di_error`。

### 2.2 接口与实现的关系

- 每一条注册均绑定一个**接口类型**（用于查找）和一个**具体实现类型**（用于构造）
- 具体实现类型必须派生自接口类型（编译期 `is_base_of` 断言）
- 当 `TInterface != TImpl` 时，`TInterface` **必须拥有虚析构函数**（编译期 `static_assert`），以保证通过基类指针正确删除派生对象
- 当 `TInterface == TImpl` 时，注册的是具体类型自身，不要求虚析构函数

### 2.3 四槽位模型 (4-Slot Model)

每个 `(component_type, key)` 对可以拥有最多 **4 个独立槽位**，由 `(type_index, string, lifetime_kind, bool is_collection)` 四元组唯一标识：

| 槽位 | lifetime | is_collection | 注册方法 | 注入类型 |
|------|----------|---------------|----------|----------|
| 单例单实例 | singleton | false | `add_singleton` | `T&` |
| 瞬态单实例 | transient | false | `add_transient` | `std::unique_ptr<T>` |
| 单例集合 | singleton | true | `add_collection(lifetime_kind::singleton)` | `std::vector<T*>` |
| 瞬态集合 | transient | true | `add_collection(lifetime_kind::transient)` | `std::vector<std::unique_ptr<T>>` |

- **单实例槽位**：同一 `(type, key, lifetime)` 下最多允许 1 条注册，重复注册抛 `duplicate_registration`
- **集合槽位**：同一 `(type, key, lifetime)` 下允许任意多条注册（自由追加）

### 2.4 依赖的表达方式

依赖通过一个零大小的**标签类型** `deps<D1, D2, ...>` 在注册点声明，传入注册方法作为参数。每个 `Di` 可以是以下 5 种形式之一：

| 依赖标记 | 含义 | 注入类型 |
|----------|------|----------|
| `T` (bare) | 单例依赖 | `T&` |
| `singleton<T>` | 显式单例标记（等同于 bare T） | `T&` |
| `transient<T>` | 瞬态依赖 | `std::unique_ptr<T>` |
| `collection<T>` | 单例集合依赖 | `std::vector<T*>` |
| `collection<singleton<T>>` | 等同于 `collection<T>` | `std::vector<T*>` |
| `collection<transient<T>>` | 瞬态集合依赖 | `std::vector<std::unique_ptr<T>>` |

示例：

```cpp
registry.add_singleton<IFoo, FooImpl>(deps<IBar, transient<IBaz>, collection<IPlugin>>);
```

`deps<>` 要求：
- `TImpl` 必须可由注入类型列表构造（编译期断言：`constructible_from_deps` concept）
- 解析时框架对每个 `Di` 按其标记执行对应的解析方法

无依赖时直接调用不带标签的重载：

```cpp
registry.add_singleton<IFoo, FooImpl>();
// TImpl 必须默认可构造
```

---

## 3. 组件描述符

描述符（`descriptor`）是注册信息的运行时表示，包含以下语义字段：

| 字段 | 类型 | 语义 |
|------|------|------|
| `component_type` | `type_index` | 接口类型，查找 key 的一部分 |
| `lifetime` | `lifetime_kind` | 生命周期枚举（singleton / transient） |
| `factory` | `factory_fn` | 工厂函数，`std::function<erased_ptr(resolver&)>` |
| `dependencies` | `vector<dependency_info>` | 依赖元数据列表（用于 build-time 校验） |
| `key` | `string` | 空字符串表示非命名注册；非空为 keyed 注册 |
| `is_collection` | `bool` | 该注册是否属于集合槽位 |
| `impl_type` | `optional<type_index>` | 具体实现类型（用于装饰器精确匹配） |
| `forward_target` | `optional<type_index>` | 非空时表示这是一条 forward 展开后的记录 |
| `forward_cast` | `function<void*(void*)>` | forward 展开时的指针偏移转换 |

### 3.1 dependency_info

依赖元数据结构：

| 字段 | 类型 | 语义 |
|------|------|------|
| `type` | `type_index` | 被依赖的接口类型 |
| `is_collection` | `bool` | 是否为集合依赖 |
| `is_transient` | `bool` | 是否为瞬态依赖 |

校验时用 `(type, is_collection, is_transient)` 三元组确定所需槽位。

---

## 4. 注册 API

### 4.1 注册方法矩阵

registry 提供以下注册方法，所有方法均返回 `registry&`（支持链式调用）：

**单实例注册：**

| 方法签名 | 生命周期 | 依赖来源 |
|----------|----------|----------|
| `add_singleton<I,T>()` | singleton | 无（T 默认可构造） |
| `add_singleton<I,T>(deps<D...>)` | singleton | deps<> 标签 |
| `add_transient<I,T>()` | transient | 无 |
| `add_transient<I,T>(deps<D...>)` | transient | deps<> 标签 |

**集合注册：**

| 方法签名 | 依赖来源 |
|----------|----------|
| `add_collection<I,T>(lifetime_kind)` | 无 |
| `add_collection<I,T>(lifetime_kind, deps<D...>)` | deps<> 标签 |

**命名注册（keyed）：**

以上所有方法均存在对应的 keyed 重载，通过在参数列表首位增加 `string_view key` 区分。keyed 与 non-keyed 注册在同一接口类型下互不干扰。

### 4.2 编译期约束

每个 `add_*<I, T>` 调用均在编译期检查：

- `T` 派生自 `I`（`derived_from_base<T, I>` concept）
- 无依赖版本：`T` 默认可构造（`default_constructible<T>` concept）
- 带依赖版本：`T` 可由注入类型列表构造（`constructible_from_deps<T, Deps...>` concept）

### 4.3 工厂函数语义

每条注册在内部持有一个工厂函数，签名为 `erased_ptr(resolver&)`：

- 工厂在解析时被调用，接收当前激活的 `resolver`
- 工厂返回类型擦除的 `erased_ptr`；框架在类型安全边界处通过 `static_cast<T*>` 恢复类型
- 工厂执行时若抛出异常：若异常属于 `di_error` 子类（如 `not_found`），框架直接透传不做二次包装；其他 `std::exception` 子类则包装为 `resolution_error` 再向上传播；非 `std::exception` 的异常原样透传，不做任何包装

#### 4.3.1 解析链上下文标注

当工厂在解析依赖过程中抛出 `di_error` 子类异常时，框架在每层 `resolve_*_by_index` 中对该异常调用 `append_resolution_context()`，附加当前正在解析的组件信息（demangled 类型名 + 实现类型名）。最终 `what()` 输出形如：

```
Component not found: IC [...] (while resolving IB [impl: BImpl] -> IA [impl: AImpl])
```

多层嵌套解析会产生完整的解析链（箭头方向从内层到外层），便于定位异常源头及其在依赖图中的路径。此标注保留原始异常类型不变（`not_found` 仍为 `not_found`），仅在 `what()` 返回值中追加上下文信息。

### 4.4 单实例槽位唯一性

对同一 `(component_type, key, lifetime)` 的单实例槽位，第二次调用 `add_singleton` 或 `add_transient` 将抛 `duplicate_registration`。不同 lifetime 的单实例槽位相互独立，同一接口可以同时拥有 singleton 和 transient 注册。

---

## 5. 类型擦除存储：erased_ptr

### 5.1 设计动机

标准 `unique_ptr<void>` 在默认删除器下不可用（`void` 是不完整类型，`default_delete<void>` 无法调用 `delete`），而 `shared_ptr<void>` 虽然可用但引入了引用计数开销。`erased_ptr` 是一个最小化的 RAII 包装器，等价于 `unique_ptr<void, void(*)(void*)>`。

### 5.2 定义

```cpp
struct erased_ptr {
    void* ptr = nullptr;
    void (*deleter)(void*) = nullptr;

    erased_ptr() = default;
    erased_ptr(void* p, void (*d)(void*)) noexcept;

    erased_ptr(erased_ptr&&) noexcept;
    erased_ptr& operator=(erased_ptr&&) noexcept;

    erased_ptr(const erased_ptr&) = delete;
    erased_ptr& operator=(const erased_ptr&) = delete;

    ~erased_ptr();

    void  reset() noexcept;
    void* get() const noexcept;
    void* release() noexcept;
    explicit operator bool() const noexcept;
};
```

### 5.3 关键特性

- 大小恰为 **2 个指针**（16字节 / 64位平台），无堆分配
- `deleter` 为原始函数指针 `void(*)(void*)`，不使用 `std::function`
- 移动语义（noexcept）、不可复制
- `make_erased<T>(args...)` 辅助函数：创建 `new T(args...)` 并设置正确的 deleter（仅用于 `TInterface == TImpl` 场景）
- `make_erased_as<TInterface, TImpl>(args...)` 辅助函数：创建 `new TImpl(args...)`，执行 `static_cast<TInterface*>(impl)` 后将 `TInterface*` 存入 `void*`。这确保 `static_cast<TInterface*>(void*)` 在解析时正确还原指针，即使在多重继承或虚拟继承下指针值发生偏移也不会出错。要求 `TInterface` 拥有虚析构函数（当 `TInterface != TImpl` 时）

### 5.4 非所有权 erased_ptr

当 `deleter == nullptr` 时，`reset()` 不执行删除。用于 forward-singleton 场景（指向目标 singleton 的缓存实例，由原始描述符拥有）。

---

## 6. 生命周期语义

### 6.1 两种生命周期

| 枚举值 | 含义 |
|--------|------|
| `lifetime_kind::singleton` | 全局唯一。在 resolver 上首次解析时创建；此后所有解析均返回同一实例 |
| `lifetime_kind::transient` | 每次解析均创建新实例；不缓存 |

### 6.2 缓存位置

- Singleton 实例由 `resolver` 内部的缓存（`erased_ptr`）持有，以 descriptor 索引为 key
- Transient 实例不缓存，每次调用工厂创建新实例

### 6.3 默认行为：Eager 实例化

当 `build_options::eager_singletons == true`（默认）时，所有 singleton 实例在 `build()` 返回前即完成创建。
当 `eager_singletons == false` 时，singleton 在首次被请求时才调用工厂创建（惰性初始化）。
详见 §10.2。

---

## 7. 解析 API

### 7.1 Resolver 是只读的

resolver 在 `build()` 后不再接受注册操作，其接口仅提供解析方法。

### 7.2 非命名解析方法

| 方法 | 返回类型 | 行为 |
|------|----------|------|
| `get<T>()` | `T&` | 返回 singleton 实例引用；未注册抛 `not_found` |
| `try_get<T>()` | `T*` | 返回 singleton 实例指针；未注册返回 `nullptr` |
| `create<T>()` | `unique_ptr<T>` | 创建新 transient 实例；未注册抛 `not_found` |
| `try_create<T>()` | `unique_ptr<T>` | 创建新 transient 实例；未注册返回空 |
| `get_all<T>()` | `vector<T*>` | 返回所有 singleton 集合项；无注册时返回空容器 |
| `create_all<T>()` | `vector<unique_ptr<T>>` | 创建所有 transient 集合项；无注册时返回空容器 |

> **引用生命周期**：`get<T>()` 和 `try_get<T>()` 返回的引用/指针的生命周期与 `resolver` 对象绑定。`resolver` 销毁后，所有通过这些方法获得的引用/指针均失效（dangling）。

### 7.3 命名解析方法

与非命名解析方法一一对应，增加 `string_view key` 参数。keyed 解析仅匹配相同 key 的注册；non-keyed 与 keyed 注册之间互不可见。

### 7.4 `deps<>` 在解析时的行为

`deps<Di...>` 中每个 `Di` 根据其标记类型调用对应的解析方法：

| 依赖标记 | 调用方法 |
|----------|----------|
| bare `T` / `singleton<T>` | `resolver.get<T>()` |
| `transient<T>` | `resolver.create<T>()` |
| `collection<T>` / `collection<singleton<T>>` | `resolver.get_all<T>()` |
| `collection<transient<T>>` | `resolver.create_all<T>()` |

---

## 8. 转发注册（Forward）

### 8.1 目的

允许一个具体实现 `T` 同时以多个接口类型被解析。对于 singleton 槽位，多个接口共享**同一实例**（forward 的 singleton 使用非所有权 erased_ptr 指向目标 singleton）。

### 8.2 注册时语义

`registry.forward<IBar, Foo>()` 要求 `Foo` 派生自 `IBar`（编译期断言）。注册一条前向记录，`build()` 时展开。

### 8.3 `build()` 阶段的展开规则

1. 遍历所有 forward 记录
2. 对每条记录，查找所有 `component_type == typeid(Foo)` 且 non-keyed 的真实 descriptor
3. 为每条目标 descriptor 生成一条新 descriptor：
   - `component_type = typeid(IBar)`
   - `lifetime` 复制自目标 descriptor
   - `is_collection` 复制自目标 descriptor
   - Singleton：工厂通过 `resolve_singleton_by_index` 解析目标实例，再通过 `forward_cast` 调整指针，返回非所有权 erased_ptr
   - Transient：工厂通过 `resolve_transient_by_index` 创建目标实例，调整指针后转移所有权
4. 将展开后的 descriptor 追加到集合中

### 8.4 forward 传播全部槽位

一次 `forward<IBar, Foo>()` 调用会将 Foo 的所有**非命名**已注册槽位（singleton 单实例、transient 单实例、singleton 集合、transient 集合）全部传播到 IBar。keyed 注册不受 forward 影响。

### 8.5 指针转换

`forward_cast` 函数执行 `void* → TTarget* → TInterface*` 的 static_cast 链。由于向上转型（derived → base）的 `static_cast` 在所有继承形式下均合法且正确（包括单继承、多重继承和虚拟继承），此机制适用于所有继承模型。

对于 forward-transient 场景，框架使用专用的 `forward_deleter`（在 `forward<I, T>()` 模板中生成），执行 `delete static_cast<TInterface*>(p)`，确保在多重继承指针偏移下也能正确释放对象。

### 8.6 当前限制

- `forward<I, T>()` 仅对 `T` 的 non-keyed 注册展开；keyed 形式不在本 MVP 范围内
- **链式 forward**（如 `forward<IC, IB>()` + `forward<IB, Foo>()`）不受支持；所有 forward 记录在 `build()` 时独立展开，后展开的 descriptor 对先执行的 forward 不可见
- 若 `forward<I, T>()` 在 `build()` 时找不到 T 的任何 non-keyed 注册，框架会在构建期校验阶段抛出 `not_found(T)`
- `forward<I, T>()` 要求 `T` 派生自 `I`（编译期 `derived_from_base` 约束），方向始终为派生类 → 基类（上行转发），不支持反向

#### 8.6.1 链式 forward 不可行的设计说明

链式 forward 的缺失是**刻意的设计决策**而非遗漏。以下是完整的评估过程：

**问题描述**

当继承关系构成有向无环图（DAG）而非简单链时，多条 forward 路径可能到达同一槽位，导致语义不确定：

```
示例 DAG 拓扑（菱形扇入）：

    A
   / \
  B   C
   \ /
    D   ← add_singleton<D, DImpl>()

若自动链式展开：
forward<B, D> → 展开 B 的 descriptor（指向 D）
forward<C, D> → 展开 C 的 descriptor（指向 D）
forward<A, B> → 展开 A 的 descriptor（指向 B → D）
forward<A, C> → 展开 A 的 descriptor（指向 C → D）

结果：A 的槽位出现两条重复 descriptor，
分别经由 B→D 和 C→D 路径生成，
语义冲突且无法确定保留哪条。
```

**行业参考**

所有主流 DI 框架均采用**单跳 forward**（flat forwarding）设计：

- .NET `Microsoft.Extensions.DependencyInjection`：`services.AddSingleton<IA>(sp => sp.GetRequiredService<Impl>())`，每条 forward 直接指向具体实现
- Autofac：`RegisterType<T>().As<I1>().As<I2>()`，所有接口绑定到同一注册源
- Castle Windsor：`Component.For<I1, I2>().ImplementedBy<T>()`，平铺多接口
- Ninject：`Bind<I>().ToMethod(ctx => ctx.Kernel.Get<T>())`，单跳工厂委托

无一提供自动链式传播机制。

**正确用法**

需要多级接口共享同一实例时，应为每个接口分别 forward 到源类型：

```cpp
reg.add_singleton<D, DImpl>();
reg.forward<B, D>();  // B → D
reg.forward<C, D>();  // C → D
reg.forward<A, D>();  // A → D（直接指向源，而非通过 B 或 C）
```

这保证每个接口恰好对应一条 descriptor，语义明确、无歧义。

---

## 9. 装饰器（Decorator）

### 9.1 目的

在不修改原始注册、不影响消费方的前提下，为一个或多个已注册实现透明地包裹额外逻辑（装饰器模式）。

### 9.2 装饰器注册时机

装饰器注册**不立即修改 descriptor 集合**，在内部维护一个待应用的 decorator 回调列表；`build()` 时统一应用。

### 9.3 四种注册形式

| 形式 | 匹配范围 | 构造约束 |
|------|----------|----------|
| `decorate<I, D>()` | `I` 的所有注册 | `D(decorated_ptr<I>)` 可构造 |
| `decorate<I, D>(deps<Extra...>)` | 同上 | `D(decorated_ptr<I>, inject_type_t<Extra>...)` 可构造 |
| `decorate<I, D>(type_index target)` | `impl_type == target` 的注册 | `D(decorated_ptr<I>)` 可构造 |
| `decorate<I, D>(type_index target, deps<Extra...>)` | 同上 | `D(decorated_ptr<I>, inject_type_t<Extra>...)` 可构造 |
| `decorate_target<I, D, TTarget>()` | `impl_type == typeid(TTarget)` | `D(decorated_ptr<I>)` 可构造；编译期断言 `TTarget` 派生自 `I` |
| `decorate_target<I, D, TTarget>(deps<Extra...>)` | 同上 | `D(decorated_ptr<I>, inject_type_t<Extra>...)` 可构造 |

`decorate_target` 是 `decorate(type_index)` 的类型安全替代形式，避免手动构造 `type_index(typeid(...))`，并在编译期验证 `TTarget` 与 `I` 的继承关系。

编译期要求：`D` 派生自 `I`；构造签名使用 `decorated_ptr<I>`（非 `unique_ptr<I>` 或 `shared_ptr<I>`）。

### 9.4 `build()` 阶段的应用规则

按装饰器的注册顺序逐一处理：

- **全局装饰器**（无 target）：`d.component_type == typeid(I)` 时应用
- **目标装饰器**：`d.component_type == typeid(I)` 且 `d.impl_type == target` 时应用
- **所有 descriptor 均可装饰**：包括 forward 展开的 singleton / transient 描述符。`decorated_ptr<I>` 的所有权语义自动适配——singleton 时内部 `erased_ptr` 的 deleter 为空（非拥有），装饰器析构时不释放被装饰对象；transient 时内部 `erased_ptr` 拥有对象，装饰器析构时正常释放。
- **应用方式**：`d.factory = decorator_wrapper(d.factory)`

descriptor 的 `impl_type` 不会因装饰而改变（精确匹配仍基于原始实现类型）。

### 9.5 `decorated_ptr<I>` 类型

`decorated_ptr<I>` 是装饰器接收被装饰对象的统一句柄类型，定义在 `decorated_ptr.hpp` 中。

| 成员 | 说明 |
|------|------|
| `I& get()` / `I* operator->()` / `I& operator*()` | 访问被装饰对象 |
| `bool owns()` | `true` = transient（析构时释放对象）；`false` = singleton（析构时不释放） |

内部结构：
- `I* ptr_`：指向被装饰对象的裸指针（始终有效）
- `erased_ptr owner_`：所有权容器。对于 singleton descriptor，`owner_.deleter == nullptr`，析构时 `reset()` 不释放内存；对于 transient descriptor，`owner_` 拥有对象完整生命周期

`decorated_ptr<I>` 可移动、不可复制，无默认构造。

### 9.5 链式叠加顺序

多个 decorator 按注册顺序叠加：先注册者在内层（更靠近原始工厂），后注册者在外层。

### 9.6 生命周期继承

装饰后的 descriptor 保持原始 `lifetime` 不变。

---

## 10. 构建期校验

### 10.1 校验开关

`build()` 接受可选的 `build_options` 结构：

| 字段 | 默认值 | 含义 |
|------|--------|------|
| `validate_on_build` | `true` | 总开关，`false` 时跳过所有校验 |
| `validate_lifetimes` | `true` | 控制是否执行生命周期兼容性检查 |
| `detect_cycles` | `true` | 控制是否执行循环依赖检测 |
| `eager_singletons` | `true` | `true` 时 `build()` 返回前实例化所有 singleton |
| `allow_empty_collections` | `true` | `true` 时集合依赖的零注册不视为缺失（详见 §10.4） |

### 10.2 Eager Singleton 实例化

当 `eager_singletons == true`（默认）时，`build()` 在创建 `resolver` 后、返回前，
遍历所有 `lifetime == singleton` 的 descriptor（含 collection singleton 和
forward singleton），调用其工厂函数完成实例化。

**效果**：
- 工厂异常在 `build()` 阶段即暴露，而非延迟到首次 `get()` 时
- 消除首次请求的初始化延迟
- Forward singleton 的代理工厂会触发原始 singleton 的创建（如尚未创建）

当 `eager_singletons == false` 时，所有 singleton 保持惰性（lazy）行为，
仅在首次 `get()` 或 `resolve_singleton_by_index()` 时创建。

### 10.3 校验执行顺序

以下校验依次执行，任一失败即抛异常并停止后续校验：

1. 缺失依赖检查
2. 生命周期兼容性检查（仅 `validate_lifetimes == true` 时）
3. 循环依赖检查（仅 `detect_cycles == true` 时）

### 10.4 缺失依赖检查

对所有 descriptor 的 `dependencies` 中的每条 `dependency_info`：

- 当 `dep.is_collection == true` 且 `allow_empty_collections == true`（默认）时，**跳过此依赖的缺失检查**。集合依赖语义为"零到多"，零注册是合法状态，与 `get_all<T>()` / `create_all<T>()` 返回空容器的运行时行为一致。此行为与 .NET DI、Autofac、Google Fruit、Spring 等主流框架保持对齐（详见附录 A.9）
- 当 `allow_empty_collections == false` 时，集合依赖与单实例依赖执行相同的存在性检查
- 对于非集合依赖，根据 `(dep.type, dep.is_transient ? transient : singleton, dep.is_collection)` 确定所需槽位
- 检查该槽位是否存在至少一条注册
- 若不存在，抛 `not_found(dep.type)`，**消息中包含要求此依赖的消费者类型名**（`required by ConsumerType`）、消费者的 impl 类型名（若存在）、消费者的生命周期以及注册位置

### 10.5 生命周期兼容性检查（captive dependency 检测）

对所有 `lifetime == singleton` 的 descriptor 的 `dependencies`：

| 依赖类型 | 是否违规 | 说明 |
|----------|----------|------|
| `is_transient && !is_collection` | **违规** | 单例捕获了瞬态单实例 → 抛 `lifetime_mismatch` |
| `is_transient && is_collection` | 合法 | 单例在初始化时获取一批 transient 集合是可接受的模式 |
| `!is_transient` | 合法 | 依赖同为 singleton |

### 10.6 循环依赖检查

对依赖图执行 DFS 三色标记环检测：

- **节点**：所有接口类型（以 `type_index` 为粒度，忽略 lifetime 和 is_collection 差异）
- **边**：`dependency_info` 中的类型依赖关系
- **环的判定**：发现 Gray→Gray 的反向边

发现环时，抛 `cyclic_dependency(cycle_path)`，其中 `cycle_path` 包含环路中所有节点的 `type_index` 序列（序列末尾重复起始节点以闭合环路，例如 `[A, B, A]`）。

---

## 11. 异常体系

### 11.1 继承层次

```
std::runtime_error
  └─ di_error                     ← 所有 DI 异常的基类
       ├─ not_found               ← 未找到注册
       ├─ cyclic_dependency       ← 循环依赖
       ├─ lifetime_mismatch       ← 生命周期违规（captive dependency）
       ├─ duplicate_registration  ← 单实例槽位重复注册
       └─ resolution_error        ← 工厂执行时抛出异常的包装
```

### 11.2 各异常必须携带的信息

| 异常 | 必须携带 |
|------|----------|
| `di_error`（基类） | 消息字符串；`std::source_location`（用户调用处）；可选解析链上下文（`resolution_context_`，通过 `append_resolution_context()` 追加） |
| `not_found` | `type_index`（未找到的类型）；可选 key 字符串；可选诊断提示（consumer 信息或 slot_hint） |
| `cyclic_dependency` | `vector<type_index>`（环路节点序列） |
| `lifetime_mismatch` | 消费者 `type_index` + lifetime 名 + 可选 impl 类型名；依赖 `type_index` + lifetime 名 |
| `duplicate_registration` | `type_index`；可选 key 字符串 |
| `resolution_error` | `type_index`；内层异常的 `what()`；组件的注册位置（若可用） |

#### 11.2.1 `di_error` 解析链上下文 API

`di_error` 基类提供以下方法用于解析链追踪：

| 方法 | 说明 |
|------|------|
| `append_resolution_context(const std::string& component_info)` | 追加一层解析上下文信息，可多次调用以构建完整链；每层之间以 `" -> "` 分隔 |
| `what() const noexcept override` | 当存在解析上下文时，返回 `"<原始消息> (while resolving <链>)"`；否则退化为 `std::runtime_error::what()` |

`what()` 使用惰性缓存实现（`mutable cached_what_`），首次调用时拼接，`append_resolution_context()` 调用后清除缓存。`what()` 内部使用 try/catch 保证 noexcept 语义。

### 11.3 错误消息可读性要求

- 所有涉及类型的错误消息均应包含 demangled 类型名（`abi::__cxa_demangle`）
- 所有 `di_error` 子类均应在消息中附加调用处的源码位置（文件名、行号）
- **`not_found` 槽位提示**：当 `get<T>()` 或 `create<T>()` 因槽位不匹配而抛出 `not_found` 时（例如类型注册为 transient 而通过 `get<T>()` 解析），错误消息应包含诊断提示，指明该类型在哪个槽位存在以及应使用哪个方法（如 "type is registered as transient (use create<T>())"）

### 11.4 `source_location` 准确性要求

所有公共模板方法（`add_singleton`、`add_transient`、`add_collection`、`forward`、`decorate` 等）以及 `build()` 均接受 `std::source_location loc = std::source_location::current()` 作为末尾参数。由于模板在用户编译单元中实例化，`source_location::current()` 的默认值在用户调用处求值，确保异常中携带的位置信息指向用户代码而非库内部。

### 11.5 注册位置追踪

`descriptor` 结构体携带 `std::source_location registration_location` 字段，在 `register_single` 和 `register_collection` 中赋值。后续 `resolution_error` 可在消息中附加 "(registered at file:line)" 以帮助定位出错组件的注册位置。验证阶段（`check_missing_dependencies`）也使用此字段在 `not_found` 的提示中包含注册位置。

### 11.6 非标准异常透传

`resolve_singleton_by_index` 和 `resolve_transient_by_index` 仅捕获 `di_error`（直接重抛）和 `std::exception`（包装为 `resolution_error`）。不派生自 `std::exception` 的异常不做捕获，原样透传给调用方。

### 11.7 编译期诊断消息

所有 `static_assert` 消息均包含触发断言的具体 API 名称，例如：
- `"add_singleton<I,T>: I must have a virtual destructor when I != T"`
- `"add_collection<I,T>: I must have a virtual destructor when I != T"`
- `"forward<From,To>: From must have a virtual destructor"`

以便开发者在编译错误输出中立即识别出错的注册调用。

### 11.8 注册调用栈追踪（Boost.Stacktrace）

当 CMake 选项 `LIBRTDI_ENABLE_STACKTRACE`（默认 `ON`）启用且系统可找到 Boost.Stacktrace 时，所有公共注册 API（`add_singleton`、`add_transient`、`add_collection`、`forward`、`decorate`）都会在**头文件模板函数内部**调用 `internal::capture_stacktrace()` 捕获完整调用栈，将其以 `std::any` 形式存入 `descriptor::registration_stacktrace` 字段。

#### 调用栈捕获位置与 API 名称

`capture_stacktrace()` 声明在公有头文件 `descriptor.hpp` 的 `librtdi::internal` 命名空间中，实现在 `src/stacktrace_capture.cpp` 中（编译时使用 `LIBRTDI_HAS_STACKTRACE` PRIVATE 宏）。捕获发生在公共 API 模板函数体内（而非 `register_*` 内部实现），因此栈帧的第一帧即为用户调用公共 API 的位置。

每个 `descriptor` 还携带 `api_name` 字段（如 `"add_singleton"`、`"forward"`、`"decorate"`），由公共 API 模板在注册时设置。诊断输出的调用栈标题行格式为：

```
Registration stacktrace for MyType [impl: Impl] (called via add_singleton):
  #0 ...
  #1 ...
```

#### 公有 API

| 方法 | 说明 |
|------|------|
| `di_error::set_diagnostic_detail(std::string)` | 设置扩展诊断信息（如注册调用栈文本），由库内部在抛出异常前调用 |
| `di_error::diagnostic_detail() const` | 获取扩展诊断信息（空字符串表示无） |
| `di_error::full_diagnostic() const` | 返回 `what()` + 换行 + `diagnostic_detail()`（若有），用于日志或终端输出 |

#### 行为规则

1. **捕获时机**：调用栈在公共 API 模板函数内部捕获（`add_singleton`、`add_transient`、`add_collection`、`forward`、`decorate`），保留了从用户代码到公共 API 入口的完整帧链。`forward` 和 `decorate` 同样捕获调用栈，在展开或应用时传播到生成的 descriptor 中。
2. **附着时机**：当 `build()` 验证阶段或 `resolve_*_by_index` 运行时抛出异常时，库自动从相关 `descriptor` 中提取调用栈并通过 `set_diagnostic_detail()` 附着到异常上。
3. **涉及的异常类型**：`not_found`、`lifetime_mismatch`、`cyclic_dependency`、`resolution_error`。
4. **ABI 隔离**：`descriptor::registration_stacktrace` 类型为 `std::any`，Boost 头文件仅在 `src/` 内部包含（`stacktrace_utils.hpp`），不泄漏到公有头文件中。`capture_stacktrace()` 函数声明在公有头文件中但实现在 `.cpp` 文件中，确保 Boost 依赖不传播给下游代码。
5. **编译定义**：`LIBRTDI_HAS_STACKTRACE`（PRIVATE，编译库时使用）、`LIBRTDI_STACKTRACE_AVAILABLE`（PUBLIC，下游代码可用于条件编译）。
6. **后端选择**：优先使用 `stacktrace_backtrace`（符号解析更完整），退而使用 `stacktrace_basic`。
7. **关闭方式**：`cmake -DLIBRTDI_ENABLE_STACKTRACE=OFF ..` 将跳过 Boost 查找，所有捕获函数返回空 `std::any`，`full_diagnostic()` 退化为 `what()`。

---

## 12. 线程安全

### 12.1 注册阶段

`registry` 的操作假定发生在单线程阶段（应用启动时），不要求线程安全。

### 12.2 解析阶段

`resolver` 在 `build()` 后可在多线程环境中并发使用，要求：

- **Singleton**：多线程首次解析时，同一 descriptor 的工厂必须恰好被调用一次（once-per-descriptor 语义）
- **Transient**：每次都创建新实例，天然线程安全

### 12.3 锁的实现

Singleton 缓存整体使用一把 `recursive_mutex` 保护，允许工厂闭包内部递归解析（虽然这通常是循环依赖配置错误，但不应导致死锁）。

---

## 13. 明确的非目标

以下功能**不在本 MVP 范围内**：

| 非目标 | 说明 |
|--------|------|
| 属性注入 | 仅支持构造函数注入 |
| 作用域 (Scope) | 不提供 scoped 生命周期，不提供嵌套容器 |
| 注册策略 (Policy) | 不提供 single / replace / skip 策略；单实例槽位强制唯一，集合槽位自由追加 |
| Keyed forward | `forward<I,T>()` 仅展开 T 的 non-keyed 注册 |
| Keyed deps<> | `deps<>` 中的依赖始终通过 non-keyed 解析满足 |
| 预热/预创建 | 已实现（`eager_singletons`，§10.2）；不在非目标中 |
| 反射/代码生成 | 依赖必须在源码中明确以 `deps<>` 声明 |
| 跨库/插件注册合并 | 不提供多 registry 聚合机制 |
| 自定义分配器 | 所有实例通过标准 `new` 分配 |
| 无虚析构函数的接口基类 | 当 `TInterface != TImpl` 时，`TInterface` **必须**拥有虚析构函数；框架通过 `static_assert` 在编译期强制此约束 |
| MSVC 全面支持 | 异常消息 demangling 依赖 `abi::__cxa_demangle`；MSVC 下退化为编译器原生 `type_index::name()`，功能正常但可读性降低。MSVC 未经全面测试 |

---

## 附录 A：关键设计决策记录

### A.1 erased_ptr 而非 shared_ptr<void>

`shared_ptr<void>` 虽然可以类型擦除 deleter，但引入引用计数开销。MVP 中 singleton 由 resolver 独占管理（lifetime 绑定 resolver），无需共享所有权。`erased_ptr` 仅 2 个指针大小，移动操作 noexcept 且无堆分配。

### A.2 四槽位模型而非 registration_policy

旧设计使用 `multiple` / `single` / `replace` / `skip` 策略控制同一接口的重复注册。新设计将单实例与集合在类型层面分离：

- `add_singleton` / `add_transient`：单实例槽位，同 (type, key, lifetime) 只允许一个
- `add_collection`：集合槽位，自由追加

这消除了策略的复杂性，同时保留了"同一接口多实现"的能力。

### A.3 deps<> 依赖标记类型

旧设计中 `deps<Di...>` 每个 Di 均以 `shared_ptr<Di>` 注入。新设计引入标记类型以在编译期区分注入方式：

- Bare `T` / `singleton<T>` → `T&`（引用注入，由 resolver 持有实例）
- `transient<T>` → `unique_ptr<T>`（所有权转移）
- `collection<T>` → `vector<T*>`（指针集合）
- `collection<transient<T>>` → `vector<unique_ptr<T>>`（所有权集合）

### A.4 装饰器构造签名使用 `decorated_ptr<I>`

装饰器通过 `decorated_ptr<I>` 接收被装饰实例。`decorated_ptr<I>` 内持引用（`I*`）和所有权容器（`erased_ptr`）。对于 transient 场景，`erased_ptr` 拥有对象完整生命周期；对于 singleton 场景（包括 forward-singleton），`erased_ptr` 的 deleter 为空，析构时不释放对象。这统一了所有场景下的装饰器构造签名，无需区分所有权语义。详见 §9.5 和附录 A.8。

### A.5 forward 展开在 build() 时执行

`forward<IBar, Foo>()` 注册时，Foo 的后续注册数量尚不确定。在 `build()` 时展开可以看到完整的 descriptor 集合，从而生成正确数量的转发关系，并确保 lifetime / is_collection 复制的一致性。

### A.6 多重继承与虚拟继承支持

旧设计中 `make_erased<TImpl>()` 将 `TImpl*` 直接存入 `void*`，解析时通过 `static_cast<TInterface*>(void*)` 恢复。在单继承下这是正确的（指针值不变），但在**多重继承**中，`TInterface` 不是第一个基类时指针偏移不为零，该 round-trip 会产生未定义行为。

新设计引入 `make_erased_as<TInterface, TImpl>()`：

1. `new TImpl(...)` 创建实例
2. `static_cast<TInterface*>(impl)` 执行编译期已知的上行转型，得到正确偏移的 `TInterface*`
3. 将 `TInterface*` 而非 `TImpl*` 存入 `void*`
4. deleter 执行 `delete static_cast<TInterface*>(p)`，要求 `TInterface` 拥有虚析构函数

这使得解析时的 `static_cast<TInterface*>(void*)` 成为恒等转换，在所有继承形式下均正确。上行 `static_cast`（derived → base）对虚拟继承也是合法的，因此虚拟继承自然获得支持，无需 `dynamic_cast`。

### A.7 单跳 forward 而非链式 forward

曾评估过**多轮迭代展开算法**：在 `build()` 时使用 `do { ... } while (any_progress)` 循环，每轮将新展开的 descriptor 作为后续 forward 的查找目标，直至所有 forward 记录均已展开或无法继续。

此方案在线性继承链（`A ← B ← C`）下可正确工作，但在 DAG 拓扑（菱形、扇入）下存在根本缺陷：

- 扇入节点导致同一 `(type, key, lifetime, is_collection)` 槽位被多条路径重复生成 descriptor
- 去重需要决定"保留哪条路径的工厂"，引入不确定性
- descriptor 顺序依赖展开轮次，破坏注册顺序的可预测性

**决策**：保持单次遍历展开，所有 forward 直接指向具体注册类型，一跳到位。这与 .NET DI、Autofac、Castle Windsor、Ninject 等主流框架的设计一致（详见 §8.6.1）。

若用户需要将同一实现暴露为多个接口，应多次调用 `forward<Ix, T>()`，每条均直接指向源类型。未来可考虑引入 `forward_as<T, I1, I2, ...>()` 便捷 API 以减少样板代码，但语义仍为 N 条独立的单跳 forward。

### A.8 `decorated_ptr<I>` 替代 `unique_ptr<I>`

旧设计中装饰器构造函数接收 `std::unique_ptr<I>`，工厂 wrapper 通过 `erased_ptr::release()` → `unique_ptr<I>` 转移所有权。这在 forward-singleton 场景下产生**不可解的 double-free**：

- Forward-singleton 工厂返回非所有权 `erased_ptr`（`deleter == nullptr`）
- `release()` 返回裸指针，`unique_ptr<I>` 错误地声称拥有该对象
- resolver 析构时，装饰器的 `~unique_ptr<I>` 和目标 descriptor 的 `erased_ptr` 各自尝试释放同一对象

旧设计通过在 `build()` 时**跳过 forward-singleton 的装饰**来规避此问题，但这导致无法按接口差异化装饰（如仅给 `IBase` 加日志装饰器而 `IDerived` 保持原样）。

**新设计**引入 `decorated_ptr<I>`：

- 内持 `I* ptr_`（引用）和 `erased_ptr owner_`（所有权容器）
- Singleton 时 `owner_.deleter == nullptr`，析构不释放 → 无 double-free
- Transient 时 `owner_` 持有完整所有权 → 装饰器析构正常释放
- 装饰器通过 `inner_->method()` 访问被装饰对象，用法与 `unique_ptr<I>` 一致（`operator->()` 重载）
- `owns()` 方法允许装饰器在需要时区分生命周期语义

工厂 wrapper 不再调用 `release()`，改为 `ep.get()` 获取指针 + `move(ep)` 转移 `erased_ptr` 进 `decorated_ptr`，所有权链完整且无歧义。

### A.9 集合依赖默认可选（`allow_empty_collections`）

旧设计中 `check_missing_dependencies` 对集合依赖（`dep.is_collection == true`）和单实例依赖执行完全相同的"必须存在至少一条注册"检查。然而 `resolver::get_all<T>()` 和 `create_all<T>()` 在零注册时返回空容器而非抛出异常，导致构建期校验与运行时行为存在语义断裂。

**行业调研**

所有主流 DI 框架均将集合依赖视为**隐式可选**，零注册时返回空集合而非报错：

- **.NET Microsoft.Extensions.DependencyInjection**：`IEnumerable<T>` 在零注册时返回空序列，从不抛异常
- **Autofac**：隐式关系类型 `IEnumerable<T>` 始终可解析，零注册时为空集合
- **Google Fruit**：`getMultibindings<T>()` 零 provider 时返回空 vector
- **Spring Framework**：`@Autowired List<T>` 对集合类型绕过 required 检查，注入空集合
- **Dagger 2**：`@Multibinds` 声明后允许空 `Set<T>`
- **Ninject / Castle Windsor**：集合解析零注册时返回空集合

设计哲学一致：集合表达"零到多"语义（plugin/handler/middleware 贡献模式），零个贡献者是完全合法的应用状态。

**设计决策**

引入 `build_options::allow_empty_collections`（默认 `true`），在 `check_missing_dependencies` 中当 `dep.is_collection && allow_empty_collections` 时跳过该依赖的缺失检查。选择 `build_options` 开关而非新增 `optional_collection<T>` 标签类型，原因是：

1. 避免 `deps<>` API 膨胀（已有 `T`、`singleton<T>`、`transient<T>`、`collection<T>`、`collection<singleton<T>>`、`collection<transient<T>>` 六种形式）
2. "所有集合都可选"或"所有集合都 required"在同一项目中通常是一致的策略选择，全局粒度够用
3. 若未来出现按类型细分的需求，可追加 `required_collection<T>` 标签作为 opt-in 强制语义，无需改动现有 API

---

*本文档描述 MVP 需求；行为细节以 `tests/` 目录下的测试断言为规范补充来源。*
