---
name: libcolorscreen_cleanup
description: Guidelines for modernizing and refactoring the core libcolorscreen rendering logic.
---

# Libcolorscreen Modernization & Cleanup

This skill provides guidelines for maintaining, refactoring, and modernizing the `libcolorscreen` C++ library. Follow these rules to ensure performance, correctness, and consistency.

## 1. Documentation & Coding Style

- **GNU Style Formatting**: Files with `.C` and `.h` in `libcolorscreen` MUST follow the GNU coding style.
- **Function Comments**: Every function must have a comment explaining its purpose and all its parameters. 
    - **Parameter Documentation**: Parameters within comments must be in **UPPERCASE**.
    - **Consistency**: Add missing comments to legacy functions when refactoring.
- **Member Initialization**: For parameter structures and classes, prefer C++11 default member initialization (`type var = initial_value;`) over assignments in the constructor body.
- **C++17**: Suggest improvements for using best practices of C++17. Be careful about performance impact. Always ask in plan.
- **Typos and grammar errors**: Check for typos and grammar errors in strings and comments. Always ask in plan for corrections in strings.  Project should use American english.

## 2. Type Safety & Correctness

- **Type Precision**:
    - Use `luminosity_t` for color and light intensity computations.
    - Use `coord_t` for spatial coordinates.
- **Avoid Implicit Conversions**: Carefully review code to prevent accidental conversions to `double`.
    - Avoid `double` literals (use `(coord_t)1.0` or `1.0f` if `luminosity_t` is float).
    - Use project math wrappers (`my_sqrt`, `my_pow`, `my_floor`) instead of standard `std::` functions to maintain type consistency.
- **Correctness Review**: Always perform a deep review for logic errors, especially **cut-and-paste errors**.
- **Error Handling**: Functions returning `bool` as an error indicator must have their return values checked by the caller. Systematically identify and fix missing checks.

## 3. Template & Performance Modernization

- **Constancy**: Proactively add missing `const`, `constexpr`, `pure_attr`, and `const_attr` to functions and variables where possible.
- **OpenMP**: Identify opportunities for parallelization. Suggest OpenMP improvements (e.g., `#pragma omp parallel for`) in the implementation plan.

## 4. `rgbdata` Gotchas

- **Default Initialization**: The `rgbdata()` constructor initializes fields to **-1** (legacy sentinel).
- **Zeroing Buffers**: When zeroing a buffer for accumulation, **NEVER** use `T{}` if `T` can be `rgbdata`.
    - **Correct Pattern**: Use `if constexpr` to explicitly use `rgbdata(0, 0, 0)`.

## 5. Verification & Workflow

- **Build Directory**: Always perform out-of-tree builds in the `build-qt` subdirectory.
- **Unit Tests**: 
    - When introducing new features or complex refactors, **suggest** new unit tests in the implementation plan.
    - **DO NOT** implement new tests automatically; wait for user approval of the test plan.
- **Regression Testing**: Always run `make check` or `./unittests` to verify functional parity.
