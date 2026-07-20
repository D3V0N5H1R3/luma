# webview

Tiny cross-platform library for building web-based desktop GUIs.

- **Source:** <https://github.com/webview/webview>
- **Version:** 0.12.0
- **Vendored:** 2026-05-12
- **License:** MIT (see [LICENSE](LICENSE))

Hosts the native window for the `GraphicalUi` module, embedding the platform
WebView (WebView2 on Windows, WebKit on macOS, WebKitGTK on Linux).

## Files

- `webview.h` — amalgamated single-header build of the webview library.
- `WebView2.h`, `WebView2EnvironmentOptions.h` — Microsoft WebView2 SDK headers
  required by the Windows backend.
- `LICENSE` — upstream license text.

The backend is selected at configure time by
[`cmake/LumaWebView.cmake`](../../cmake/LumaWebView.cmake); on Windows the
WebView2 runtime is lazy-loaded.
