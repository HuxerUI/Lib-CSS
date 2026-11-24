# Lib-CSS

Apply inline CSS declarations to [HuxerUI](https://github.com/HuxerUI/HuxerUI) views with `.With(Css("..."))`. Lib-CSS translates supported declarations into HuxerUI modifiers, so they participate in the view's normal layout and drawing.

## Integration

Lib-CSS uses C++20 and CMake 3.20 or later. It is verified with HuxerUI 0.5.5 on macOS. The library uses HuxerUI's internal property modifier interface, so run the tests when upgrading the SDK.

Register Lib-CSS after declaring the application target. Choose one of the two forms below and replace the placeholder with a revision that contains the library.

To pin a commit, pass its full 40-character SHA.

```cmake
huxerui_use_library(my_app
    TARGET HuxerUI::CSS
    URL "https://github.com/HuxerUI/Lib-CSS.git"
    COMMIT "<full-commit-sha>"
)
```

For a tagged release, use the published tag name as shown on [GitHub Tags](https://github.com/HuxerUI/Lib-CSS/tags).

```cmake
huxerui_use_library(my_app
    TARGET HuxerUI::CSS
    URL "https://github.com/HuxerUI/Lib-CSS.git"
    TAG "<release-tag>"
)
```

Include [`<huxerui/css.h>`](include/huxerui/css.h) and use `css::Css` with any view that accepts the corresponding HuxerUI modifiers.

```cpp
#include <huxerui/css.h>
#include <huxerui/view.h>

using namespace huxerui;
using huxerui::css::Css;

View SaveLabel() {
  return Text("Save").With(Css(R"(
    padding: 8px 16px;
    background-color: #2563eb;
    border-radius: 8px;
    color: white;
  )"));
}
```

## Modifier order and reuse

Within one declaration list, the last declaration for a property wins. HuxerUI applies modifiers from left to right, so a later modifier can override a CSS declaration. In the example below, `Padding(12.0F)` replaces the CSS padding on all four edges.

```cpp
static const Css label_style("padding: 8px 16px; background-color: #dbeafe;");

return Text("Save").With(label_style, Padding(12.0F));
```

A `Css` value parses and owns its declarations on construction. Reuse the value when applying the same style to several views or across recompositions.

## Supported declarations

| Property | Supported values |
| --- | --- |
| `width`, `height`, `min-width`, `min-height`, `max-width`, `max-height` | Lengths for HuxerUI's outer `Frame` |
| `padding` | One to four lengths in CSS shorthand order |
| `gap` | A single length for `Row` or `Column` spacing |
| `background-color`, `color` | A solid background color or text foreground color |
| `font-size` | A positive length |
| `border` | A uniform border in the form `<length> solid <color>` |
| `border-radius` | A single length for all four corners |

Lengths use non-negative `px` values or a unitless zero. One `px` is one HuxerUI logical unit. A declaration of `width: 120px; padding: 10px` requests an outer width of 120 and leaves 100 logical units for content when the parent grants that width. Borders paint inside the view bounds.

Colors accept `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA`, `black`, `white`, and `transparent`.

Separate declarations with semicolons. CSS comments and an optional final semicolon are accepted. Malformed declarations and unsupported properties or values throw `std::invalid_argument` at construction. The diagnostic includes a zero-based byte offset.

## Preview and tests

The [preview app](examples/preview) shows how CSS styles combine with native padding and background modifiers. Run it on macOS from the repository root.

```sh
cd examples/preview
huxerui run macos
```

From the repository root, run the tests with an installed HuxerUI SDK that includes the windowless UI testing library.

```sh
cmake -S tests -B build/tests -DCMAKE_PREFIX_PATH="$HUXERUI_HOME"
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

The [test suite](tests/css.cpp) compares layout and paint output against equivalent HuxerUI modifiers and checks override order, shorthand values, and invalid inputs.

## License

[MIT](LICENSE)
