## Write and Run Tests

Luma has built-in test support with the `@test` annotation:

```luma
@test
function void test_addition() {
    assert(1 + 1 == 2, "basic addition")
}
```

Test files use `@test` functions instead of `@main`. Check expectations with `assert(condition)` or `assert(condition, "message")`.

### Discovering tests

Tests are discovered automatically and shown in the **Test Explorer** sidebar. Run a single test or a whole file from the Explorer or the gutter icons beside each `@test` function. Run them under the **Coverage** profile to see line-level coverage in the gutter.

### Running tests

- **Keyboard shortcut** — press `Ctrl+Alt+T` (`Cmd+Alt+T` on macOS) with a `.luma` file focused.
- **Command Palette** — run **Luma: Run Tests in Current File**.
- **Terminal** — run `luma --test path/to/file.luma` to run every `@test` function in a file.
