# librtdi 评审报告 v3

**评审日期**：2026-02-24  
**评审对象**：`/mnt/c/Users/Charl/GhCopilotProjects/librtdi`（合并后单版本项目）  
**参考文档**：`REQUIREMENTS.md` v2.1

---

## 第一部分：需求文档审视

### 1.1 歧义

| # | 位置 | 问题 |
|---|------|------|
| A | §13 非目标 | "Windows / MSVC 支持…仅限 GCC/Clang"被列为非目标，但补充工程要求明确要求 MSVC VS2019+ 兼容。文档与实际需求自相矛盾；实现已向 MSVC 靠拢（`#if defined(__GNUC__)` fallback + `CompilerWarnings.cmake` MSVC 分支），文档的"非目标"列表应删除或修改这一条。 |
| B | §8.3 | forward 展开只搜索"真实 descriptor"，但没有说明是否包含已展开的其他 forward descriptor。即 `forward<IC, IB>()` + `forward<IB, Foo>()` 是否支持链式传播？文档没有给出任何说明。 |
| C | §4.3 | "工厂执行时若抛出异常，框架应将其包装为 `resolution_error`"——若工厂内部通过 `resolver.get<T>()` 触发 `not_found`（`di_error` 子类），是透传还是二次包装？文档未规定；实现做了区分（`di_error` 子类直接透传，其他 `std::exception` 包装为 `resolution_error`），属于合理行为但未文档化。 |
| D | §12.3 | "每个 singleton 缓存使用 `recursive_mutex` 保护"——"每个"暗示 per-slot 独立锁，但实现是整个 resolver 共用一把 `recursive_mutex`（全局锁）。语义上两种方式等价但性能特征不同，措辞有歧义。 |

### 1.2 没说清楚

| # | 位置 | 问题 |
|---|------|------|
| E | §7.2 | `get<T>()` 返回 `T&`，但文档没有说明引用的生命周期与 `resolver` 对象绑定。用户代码持有引用超过 `resolver` 存活期会导致 dangling reference，这是重要的安全边界，应在 API 文档中明确。 |
| F | §9.4 | 装饰器应用到"所有 `d.component_type == typeid(I)` 的 descriptor"——因为 build 管线顺序是"先 forward 展开、再应用 decorator"（§2.1），forward 展开生成的 descriptor 也会被装饰器覆盖。这是有意设计还是实现细节？文档没有明确说明，会影响用户对装饰器作用域的理解。 |

### 1.3 缺漏

| # | 位置 | 问题 |
|---|------|------|
| G | §11.1/11.2 | `registration_mismatch` 异常在整个文档中**没有任何触发条件描述**。§11.1 列出了该类，§11.2 仅写"类型不匹配；详情字符串"，但从注册 API（§4）、forward（§8）、decorator（§9）的所有规则中找不到任何标明会抛出它的场景。这是严重的文档缺漏。 |
| H | §2.1 | `build()` 之后再调用 `add_singleton` 等注册方法的行为未规范。实现抛出 `di_error("Cannot register components after build() has been called")`，这一行为在需求文档中没有说明。 |
| I | §8.3/§8.6 | `forward<I,T>()` 找不到 T 的任何注册时，`build()` 的行为未规范（应抛 `not_found`？还是静默忽略？）。当前实现插入 placeholder descriptor 使校验阶段报 `not_found(T)` 而非更具诊断价值的 forward 相关错误，且文档完全未提及此场景。 |
| J | §2.1 | 未说明 `resolver` 的生命周期是否允许超过 `registry`（两者均由 `shared_ptr` 或值语义管理）。文档没有显式保证"移交后 `registry` 可以销毁"的合同，用户无法依赖此行为编写安全代码。 |

---

## 第二部分：实现对比需求的不符合项

### P0 — 严重缺陷（需立即修复）

#### Bug①：`cyclic_dependency` 错误消息末尾节点重复

**文件**：`src/validation.cpp` + `src/exceptions.cpp`

`validation.cpp` 中 `dfs()` 构建 cycle 时已在末尾追加了起点：

```cpp
// validation.cpp
auto it = std::find(path.begin(), path.end(), node);
std::vector<std::type_index> cycle(it, path.end());
cycle.push_back(node);           // ← cycle 尾部已含起点，e.g. [A, B, A]
throw cyclic_dependency(cycle);
```

而 `exceptions.cpp` 的 `build_message()` 随后**再次追加** `cycle.front()`：

```cpp
// exceptions.cpp
for (std::size_t i = 0; i < cycle.size(); ++i) {
    if (i > 0) msg += " -> ";
    msg += internal::demangle(cycle[i]);    // 输出 "A -> B -> A"
}
if (!cycle.empty())
    msg += " -> " + internal::demangle(cycle.front()); // 又加 " -> A"
// 最终结果："A -> B -> A -> A"（多一个 A）
```

需求 §10.5 要求 `cyclic_dependency(cycle_path)` 中 `cycle_path` 包含环路节点序列；错误消息应为 `A -> B -> A`，而非 `A -> B -> A -> A`。

**修复方案**：删除 `build_message` 中末尾的 `+= " -> " + internal::demangle(cycle.front())` 一行（`validation.cpp` 已正确地在 `cycle` 末尾包含起点，不需要再次追加）。

---

#### Bug②：`README.md` API 示例全部错误

**文件**：`README.md`

README 的快速开始示例与实际已实现的 API 存在多处不符：

| 错误点 | README 内容 | 实际 API |
|--------|------------|---------|
| 方法名 | `r->resolve<IGreeter>()` | `resolver` 无 `resolve()`，应为 `r->get<IGreeter>()` |
| 返回类型处理 | `r->resolve<IGreeter>()->greet()` (指针) | `get<T>()` 返回 `T&`，应为 `r->get<IGreeter>().greet()` |
| deps 注入类型 | `MyService(std::shared_ptr<ILogger> l)` + `deps<ILogger>` | `deps<ILogger>` 注入 `ILogger&`（引用），不是 `shared_ptr` |
| 声称支持的特性 | 描述了 `scoped` 生命周期 | scoped 是 §13 明确列出的**非目标** |
| 存储机制描述 | "运行时类型擦除（`shared_ptr<void>` + `type_index`）" | 实际使用 `erased_ptr`（需求 §5 的核心设计），`shared_ptr<void>` 是被替换的旧方案 |

---

### P1 — 中级不符合

#### ③ CPack 缺少 cmake config 文件支持

**文件**：`CMakeLists.txt`

`CMakeLists.txt` 已有 `include(CPack)` 和 `install(TARGETS librtdi EXPORT librtdi-targets ...)`，但缺少以下配置使其完整可用：

- `install(EXPORT librtdi-targets DESTINATION lib/cmake/librtdi)` 规则
- `cmake/librtdiConfig.cmake.in` 模板
- `configure_package_config_file()` + `write_basic_package_version_file()` 调用

缺少以上配置，`cpack` 打出的包**无法支持 `find_package(librtdi CONFIG)`**，下游项目无法通过标准 CMake 方式集成该库。

---

#### ④ 锁粒度与 §12.3 措辞不符

**文件**：`src/resolver.cpp`

```cpp
struct resolver::impl {
    std::recursive_mutex singleton_mutex;   // 全局单锁
    std::unordered_map<std::size_t, erased_ptr> singletons;
};
```

需求 §12.3 描述"每个 singleton 缓存使用 `recursive_mutex` 保护"，措辞暗示 per-descriptor 粒度（即 N 个 singleton 使用 N 把锁）。当前实现是全局一把锁；在高并发、singleton 数量多的场景下，所有解析请求均需竞争同一把锁，形成不必要的性能瓶颈。

可考虑改用 `std::call_once` + per-descriptor `std::once_flag`，既满足 once-per-type 语义又避免全局争用。

---

### P2 — 轻微不符合

#### ⑤ 缺少 `decorate<I,D,TTarget>()` 模板便利重载

**文件**：`include/librtdi/registry.hpp`

用户使用精确匹配装饰器（§9.3 第三种形式）时，必须手写：

```cpp
reg.decorate<IFoo, LoggingFoo>(std::type_index(typeid(ConcreteFoo)));
```

`std::type_index(typeid(...))` 丢失了编译期类型检查，也不符合库其他 API 的全模板风格。可增加：

```cpp
template <typename TInterface, typename TDecorator, typename TTarget>
    requires derived_from_base<TDecorator, TInterface>
          && decorator_constructible<TDecorator, TInterface>
          && derived_from_base<TTarget, TInterface>
registry& decorate();
```

#### ⑥ keyed 解析方法缺少 API 文档注释

**文件**：`include/librtdi/resolver.hpp`

`get(string_view key)`、`try_get(string_view key)` 等所有 keyed 重载均无任何文档注释，与非 keyed 版本注释风格不一致，也与 §11.3"人因工程"目标不完全一致。

---

## 修复优先级汇总

| 优先级 | # | 问题 | 修复位置 |
|--------|---|------|---------|
| P0 | ① | cyclic_dependency 消息末尾重复首节点 | `src/exceptions.cpp` — 删除 `build_message` 末尾追加 |
| P0 | ② | README.md API 示例全部错误（方法名、返回类型、deps 类型、非目标特性、存储机制） | `README.md` — 重写快速开始示例 |
| P1 | ③ | CPack 缺少 cmake config 文件（`find_package` 支持） | `CMakeLists.txt` + 新增 `cmake/librtdiConfig.cmake.in` |
| P1 | ④ | 全局锁性能瓶颈（与 §12.3 措辞不符） | `src/resolver.cpp` — 改用 per-descriptor `once_flag` |
| P2 | ⑤ | 缺少 `decorate<I,D,TTarget>()` 模板便利重载 | `include/librtdi/registry.hpp` |
| P2 | ⑥ | keyed 解析方法缺少文档注释 | `include/librtdi/resolver.hpp` |

---

## 需求文档修订建议

| 优先级 | 章节 | 修订内容 |
|--------|------|---------|
| P1 | §13 非目标 | 删除或修改"Windows / MSVC 支持"条目，与补充工程要求一致 |
| P1 | §11.1/11.2 | 补充 `registration_mismatch` 的触发条件（目前完全缺失）|
| P2 | §4.3 | 补充 `di_error` 子类透传、其他异常包装为 `resolution_error` 的规则 |
| P2 | §12.3 | 将"每个 singleton 缓存"措辞修正为"singleton 缓存"，避免暗示 per-slot 锁 |
| P2 | §2.1 | 补充 `build()` 后注册将抛 `di_error` 的行为说明 |
| P2 | §7.2 | 补充 `get<T>()` 返回引用的生命周期与 `resolver` 绑定的安全说明 |
| P3 | §8.3 | 说明链式 forward（`forward<IC,IB>` + `forward<IB,Foo>`）是否支持 |
| P3 | §8.6 | 说明 `forward<I,T>()` 找不到 T 任何注册时 `build()` 的行为 |
