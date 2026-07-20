## Try the Playground

The **Playground** is an interactive scratch pad for evaluating Luma snippets without creating a file — perfect for experimenting with the language or trying out a standard library function.

### Open the Playground

Run **Luma: Open Playground** from the Command Palette (`Ctrl+Shift+P` / `Cmd+Shift+P`). A panel opens beside your editor with a code area and an output area.

### Run a snippet

1. Type Luma code into the editor area — you don't need an `@main` function, so bare statements run directly.
2. Click **▶ Run** or press `Ctrl+Enter` (`Cmd+Enter` on macOS).
3. Output appears in the panel below. Use **Clear** to reset it.

For example:

```luma
print("Hello from the Playground!")

mutable integer total = 0
for x in [1, 2, 3, 4, 5] {
    total = total + x
}
print("Sum is ${total}")
```

### Settings

The Playground is enabled by default and can be tuned with these settings:

| Setting                         | Default   | Description                             |
| ------------------------------- | --------- | --------------------------------------- |
| `luma.playground.enabled`       | `true`    | Enable or disable the Playground.       |
| `luma.playground.timeout`       | `10000`   | Maximum execution time in milliseconds. |
| `luma.playground.maxOutputSize` | `1048576` | Maximum output buffer size in bytes.    |

The Playground runs each snippet with the `luma` interpreter, so make sure it is installed or its path is set via the `luma.path` setting.
