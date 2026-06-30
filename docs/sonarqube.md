# SonarQube Rulebook — C++ / Lua

> **Scope:** applies to every line of new or modified code. All rules are derived from warnings observed and resolved in this repository. No rule is generic; each one maps to a class of defects already found here.
>
> **Enforcement model:** before submitting any change, the author runs through the [Pre-commit checklist](#pre-commit-checklist) at the bottom of this document. CI is the safety net, not the first line of defence.

---

## 1. Logic & Control Flow

### 1.1 No duplicate sub-expressions in boolean operators
*(SonarQube: "Identical sub-expressions on both sides of operator")*

A predicate that appears more than once in the same boolean expression is either a copy/paste bug or dead code. Remove or rename until every term in a `&&` / `||` chain is distinct.

```cpp
// ✗
if (hasCase && index >= 0 && hasCase && index < limit)

// ✓
if (hasCase && index >= 0 && index < limit)
```

### 1.2 No conditionals with identical branches
*(SonarQube: cpp:S3923 — "Returns the same value whether the condition is true or false")*

If both branches of a ternary or `if/else` produce the same value or perform the same action, the conditional is vacuous. Delete it and keep the common code.

```cpp
// ✗
const auto x = flag ? value : value;

// ✓
const auto x = value;
```

### 1.3 No variable shadowing in inner scopes
*(SonarQube: "Declaration shadows a local variable in the outer scope")*

Declare distinct names for step return codes in nested loops. Never reuse `rc`, `ret`, or similar names across nested scopes.

```cpp
// ✗
int rc = sqlite3_step(stmt1);
for (...) { int rc = sqlite3_step(stmt2); }

// ✓
int outerRc = sqlite3_step(stmt1);
for (...) { int innerRc = sqlite3_step(stmt2); }
```

### 1.4 No chained assignments
*(SonarQube: "Extract the assignment to 'x' from this expression")*

Write one assignment per statement. If resetting many fields to a common value is repetitive, extract a named helper.

```cpp
// ✗
a = b = c = nullptr;

// ✓
a = nullptr;
b = nullptr;
c = nullptr;
// — or —
void ResetWidgets() { a = b = c = nullptr; }
```

---

## 2. Complexity

### 2.1 Cognitive Complexity ≤ 25 per function
*(SonarQube: "Refactor this function to reduce its Cognitive Complexity from X to the 25 allowed")*

Any new or edited function that exceeds CC = 25 must be refactored in the same commit. Do not defer.

Strategies:
- Early returns / guard clauses instead of deep nesting.
- Extract one helper per logical responsibility (one mode, one phase, one domain section).
- Named lambdas or free functions instead of monolithic handlers.

### 2.2 Control-flow nesting depth ≤ 3
*(SonarQube: "Refactor this code to not nest more than 3 if|for|do|while|switch statements")*

When a fourth nesting level is needed, extract the inner block into a named helper or use an early `continue` / early `return` to flatten the structure.

### 2.3 Lambdas ≤ 20 lines; no redundant return types
*(SonarQube: "This lambda has N lines" / "Remove the redundant return type of this lambda")*

- A lambda that exceeds 20 lines must become a named function.
- For `wxEvtHandler::Bind(...)` handlers, the lambda should call a named member function, not embed multi-step logic inline.
- Omit the trailing `-> Type` unless deduction is ambiguous or there are multiple return paths with different types.

---

## 3. Functions as Orchestrators

Every public entry-point function must be a thin orchestrator: validate, dispatch, return. Business logic lives in small, single-purpose helpers.

| Entry-point pattern | Extract into helpers |
|---|---|
| Validation functions | One validator per domain section; each returns `bool` + sets a single `wxString& error` |
| JSON import/export (`TryParse*`) | Header validation · entity selection · per-object parsing |
| Summary/report builders | Schema checks · audit checks · example formatting |
| Database multi-phase loads | One helper per phase; entrypoint owns transaction boundaries |
| `Apply*Entity` functions | validate → parse → persist; per-section parsing in named helpers |
| Dependency-closure loops | One pass per domain; top-level loop is convergence logic only |
| Import-with-progress | Root schema check · entity partitioning · rename map · phased transaction · ticker |
| SQLite stepping loops | Reusable stepping helper with `onError` callback; per-row parsing kept minimal |
| Warning collectors | Orchestration only; one scanner per domain; shared `WarningCollector` for row construction |

---

## 4. Resource Management

### 4.1 No raw `delete` in new or modified code
*(SonarQube: "Rewrite the code so that you no longer need this delete")*

Use RAII. For new allocations, prefer `std::unique_ptr`. When interacting with legacy containers holding owning raw pointers, wrap the pointer in a local `std::unique_ptr<T> owned(ptr)` before clearing the container.

### 4.2 RAII guard types must manage copy and move explicitly
*(SonarQube: "Customize this struct's copy constructor to participate in resource management")*

Any stack guard that owns a resource handle (e.g. `sqlite3_stmt*`) must:
- `= delete` its copy constructor and copy assignment.
- Define or `= delete` its move constructor and move assignment based on whether transfer of ownership is intended.

### 4.3 No raw function pointers for callbacks
*(SonarQube: "Replace this function pointer with a template parameter or a std::function")*

- Stored callbacks: `std::function<Signature>`.
- Non-stored, performance-sensitive callbacks: template parameter.
- Avoid casting between handler types. Replace old `wxEvtHandler::Connect` patterns with typed `Bind`.

---

## 5. C++ Idioms

### 5.1 No C-style arrays
*(SonarQube: "Use std::array or std::vector instead of a C-style array")*

```cpp
// ✗
const char* items[] = { "a", "b", "c" };

// ✓ fixed-size constant
static constexpr std::array<const char*, 3> items = { "a", "b", "c" };

// ✓ variable / runtime list
static const std::vector<std::string> items = { "a", "b", "c" };
```

### 5.2 Prefer structured bindings for pairs, tuples, and map iteration
*(SonarQube: "Replace this declaration by a structured binding declaration")*

```cpp
// Map iteration
for (const auto& [key, value] : map) { ... }

// Pair-returning functions
const auto [begin, end] = map.equal_range(key);
```

### 5.3 Use `const` bindings by default in range-based loops
*(SonarQube: "Pointer and reference local variables should be const if the corresponding object is not modified")*

Default to `const auto&` or `const auto& [...]`. Omit `const` only when mutation of the element is required.

### 5.4 Prefer `try_emplace` over `emplace` for map insertion
*(SonarQube: "Replace this use of emplace with try_emplace")*

`try_emplace` avoids constructing the mapped value when the key already exists and communicates intent clearly.

```cpp
map.try_emplace(key, value);
map.try_emplace(key, std::move(value));
map.try_emplace(id, a, b, c);            // passes constructor args directly
```

### 5.5 Prefer `std::string_view` for non-owning string parameters
*(SonarQube: "Replace this const reference to std::string by a std::string_view")*

Use `std::string_view` for internal functions that read but do not own the string. Convert to `std::string` only at the boundary that requires ownership.

### 5.6 Use `override` / `final` instead of repeating `virtual` in derived classes
*(SonarQube: "Use 'override' or 'final' instead of 'virtual'")*

`virtual` belongs only in base-class declarations. Derived classes use `override` or `final`. This prevents silent breakage when base signatures change.

### 5.7 No unexplained empty virtual methods
*(SonarQube: "Add a nested comment explaining why this method is empty, or complete the implementation")*

An empty virtual body must have one of:
1. A short English comment explaining the intentional no-op.
2. Promotion to pure virtual (`= 0`) if all derived classes must implement it.
3. A real implementation.

### 5.8 Do not hide non-virtual functions in derived classes
*(SonarQube: "Rename this member function so that it doesn't hide an inherited non-virtual function")*

If the derived class needs polymorphic behaviour, make the base method `virtual` and mark the override with `override`. If polymorphism is not intended, rename the derived method to eliminate the hiding.

### 5.9 Prefer `std::ranges::*` algorithms and `std::erase` / `std::erase_if`
*(SonarQube: "Replace with the version of std::ranges::find_if that takes a range" / "Replace this erase-remove idiom")*

```cpp
// Prefer ranges algorithms
std::ranges::find_if(values, pred);
std::ranges::sort(values);
std::ranges::any_of(values, pred);

// Prefer erase helpers over erase-remove idiom
std::erase(container, value);
std::erase_if(container, pred);
```

### 5.10 Use `using enum` to reduce scoped-enum verbosity
*(SonarQube: "Reduce verbosity with using enum for SomeEnum")*

Inside functions or blocks that use many enumerators from the same scoped enum, introduce `using enum SomeEnum;` in the narrowest applicable scope.

---

## 6. Exception Handling

### 6.1 Throw and catch dedicated exception types
*(SonarQube: "Define and throw a dedicated exception" / "Catch a more specific exception")*

Generic exceptions (`std::runtime_error`, `std::exception`) must not be thrown for domain failures. Define a minimal, local exception type for each error domain, throw it at the source, and catch it by that type at call sites.

---

## 7. Class & Struct Design

### 7.1 Group widget pointers to keep field count below the Sonar threshold
*(SonarQube: "Refactor this structure so it has no more than N fields")*

For dialogs and panels with many widget members, group pointers by UI section into a nested `struct Widgets { ... }`. The owning class stores one `Widgets` member per section.

---

## Pre-commit Checklist

Run through every item before opening a pull request. If an item fails, fix it in the same commit.

**Logic**
- [ ] No predicate appears more than once in any `&&` / `||` chain.
- [ ] No ternary or `if/else` returns the same value in all branches.
- [ ] No inner scope declares a variable with the same name as an outer-scope variable.
- [ ] No chained assignments (`a = b = c = ...`).

**Complexity**
- [ ] Every touched function has Cognitive Complexity ≤ 25.
- [ ] No control-flow block is nested deeper than 3 levels.
- [ ] No lambda exceeds 20 lines or carries a redundant return type.

**Structure**
- [ ] Every public entry-point delegates to small, single-purpose helpers.
- [ ] No entry-point contains business logic beyond validate → dispatch → return.

**Resource management**
- [ ] No raw `delete` in new or modified code.
- [ ] Every RAII guard type defines or deletes copy and move operations explicitly.
- [ ] No raw function pointer used for a stored callback.

**C++ idioms**
- [ ] No C-style array.
- [ ] Map iteration uses structured bindings (`const auto& [key, value]`).
- [ ] Range-based loop bindings are `const` unless mutation is required.
- [ ] Map insertions use `try_emplace` instead of `emplace`.
- [ ] Non-owning string parameters use `std::string_view`.
- [ ] Derived-class overrides use `override` or `final`, not `virtual`.
- [ ] No empty virtual method without an explanatory comment or promotion to pure virtual.
- [ ] No derived-class function hides a non-virtual base function with the same name.
- [ ] Loop-based searches and erasures use `std::ranges::*` / `std::erase` / `std::erase_if`.

**Exceptions**
- [ ] No `std::runtime_error` or `std::exception` thrown for domain errors.
- [ ] Every `catch` targets a dedicated exception type, not a generic base.