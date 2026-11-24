## Working Rules
- Testability never shapes a design. Don't add types, seams, or APIs to make code testable; tests adapt to the design (e.g. a headless `Device` for RHI tests).
- Prefer small vendored or hand-rolled code over large dependencies (e.g. pugixml over urdfdom or assimp).
- For the README's Open Decisions (physics, formats, connectivity, training interface), present options instead of picking one. Treat training needs (headless runs, parallel environments, determinism) as being as important as live mirroring.
- Validate shaders with `spirv-val --target-env vulkan1.3 --scalar-block-layout`; without the flag it reports a false "improperly straddling vector" error on `Vertex` (buffer pointers use scalar layout).
- Redirect `slangc` output to a file rather than piping it into `head`, which can kill it before it writes the `.spv`.
- Propose a change to this file when a task shows a line to be wrong, or hits something non-obvious that cost time; most tasks need none. Prefer rewording or deleting a line to adding one.

## Verifying
- The exit code of `--run-tests` is the verdict, not the summary line. Debug builds assert at exit if the validation layer reported anything, so "ALL UNIT TEST SUITES PASSED" followed by a crash is a failure. Search the log (`build/<preset>/bin/logs/`) for `validation layer:` and `[TEST FAIL]`.
- A change is done when the debug build and tests pass and the `development` and `shipping` presets also build: CI builds all three on GCC and MSVC, and Shipping compiles out the test runner and asserts.
- Tests: `NE_TEST_CASE("suite", "name")` in `src/tests/unit/<area>_test.cpp`. Sources are globbed, so a new file needs no CMake edit. There is no filter flag; the whole suite runs.
- `cmake --build` recompiles changed shaders into `build/<preset>/bin/compiled_shaders/`; the downloaded compiler is `build/<preset>/_deps/slang-src/bin/slangc`.

## Code
- Exceptions are compiled out (`-fno-exceptions`, `_HAS_EXCEPTIONS=0`): no `try` / `throw`; use the `std::error_code` overloads of `std::filesystem`.
- Format only the lines you change (`git clang-format`): 12 files in `src/` don't match `.clang-format`, and reformatting them buries the real diff.
- Settings with defaults go in designated-initializer `Config` structs, and the objects a class uses (such as `Device`) are constructor parameters, as in `Buffer(Device*, const Config&)`; allocations log with `NE_VERBOSE`; every struct shared with Slang lives in `src/renderer/gpu_types.h` with its size `static_assert`ed. Constants are `kPascalCase`.
- Rendering work follows `rendering_plan.md`'s Decisions. They're settled: change one only for a concrete reason found in the code, and update the file when you do.
- Comments say what the code can't: units, invariants, spec requirements, a non-obvious order, workarounds. One line by default, at most three on a declaration; don't restate the name or narrate the next line. Say each fact once, where it's declared: code that uses it, and Slang copies of C++ types (`// Mirrors gpu::Light`), don't repeat it.
