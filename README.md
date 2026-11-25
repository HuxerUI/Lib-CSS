# Lib-CSS

Style [HuxerUI](https://github.com/HuxerUI/HuxerUI) views with packaged CSS classes or inline declarations. Lib-CSS translates CSS into HuxerUI modifiers that participate in the view's normal layout and drawing.

## Integration

Lib-CSS uses C++20 and CMake 3.20 or later. Build it against a HuxerUI SDK or source checkout that provides `ModifierContext::Apply()`.

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

Include [`<huxerui/css.h>`](include/huxerui/css.h) to use `css::CssClass` and `css::Css`.

## Packaged style sheets

Save a CSS file at `resources/raw/styles.css` in the application's resource root.

```css
:root {
  --accent: #3730a3;
  --radius: 8px;
}

.button, .secondary {
  padding: 8px 16px;
  border-radius: var(--radius);
}

.primary {
  background-color: var(--accent);
  color: white;
}

.primary:hover {
  background-color: var(--accent-hover, #4f46e5);
}
```

Register the sheet with its generated raw resource key, then pass one or more class names to `CssClass()`.

```cpp
#include <huxerui/css.h>

#include <app_resources.h>

using namespace huxerui;

View App() {
  return Button("Save")
      .OnClick([] {})
      .With(css::CssClass("button", "primary"));
}

const Application application{
    App,
    {.application_hooks = {css::InstallStyleSheet(app::raw::styles_css)}},
};
```

`InstallStyleSheet()` reads and parses one sheet per application Runtime before opening the first window. Each `CssClass()` uses the same parsed sheet. Pass case-sensitive class names without a leading dot. An unknown name or a missing registration reports an error. Errors in the sheet report the resource key, line and column, and the selector when available.

The sheet accepts simple class selectors and the state suffixes `:hover`, `:active`, and `:focus-visible`. Separate selectors with commas to share a rule. If a View matches several selectors in a group, its base rule or each matching state rule applies once. Repeated class rules and multiple selected classes apply in file order, regardless of the order of names passed to `CssClass()`. Selected state properties merge into one `Indication`. A later modifier such as `Padding(12.0F)` can still override the corresponding base property.

Declare shared, case-sensitive custom properties in `:root`. The last definition of a name in the file wins, including for uses earlier in the file. Use `var(--name)` in base and interaction declarations, including inside gradients or color functions. `var(--name, fallback)` uses the fallback when the name is missing. Missing references without a fallback and cyclic references report a source location while loading the sheet. Substituted values must satisfy the target property's syntax.

## Inline styles

```cpp
#include <huxerui/css.h>
#include <huxerui/view.h>

using namespace huxerui;
using huxerui::css::Css;

View SaveLabel() {
  return Text("Save").With(Css(R"(
    padding: 8px 16px;
    background: linear-gradient(to right, hsl(224 72% 41%), rgb(29 78 216));
    border-radius: 6px 12px;
    box-shadow: 0 3px 8px rgb(0 0 0 / 20%);
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

## Interaction styles

```cpp
return Button("Save")
    .OnClick(save)
    .With(Css(R"(
      padding: 8px 16px;
      background-color: #3730a3;
      color: white;
      border-radius: 8px;
      &:hover { background-color: #4f46e5 }
      &:active { background-color: #27215e }
      &:focus-visible { border: 2px solid #a5b4fc }
    )"));
```

State blocks accept `background`, `background-color`, `color`, `border`, and `border-radius`. State `color` follows the SDK's indication foreground rules for `Text` and vector images. Repeated blocks for the same state merge by property in declaration order. HuxerUI selects one layer at a time in active, hover, then visible-focus order. A selected layer draws its fill over the base fill behind the content and uses the base appearance for unspecified properties. State changes apply immediately and preserve layout.

A declaration in any state block replaces the View's default `Indication`, including its ripple. The theme's keyboard focus ring is drawn separately. `.OnClick()` can appear before or after `.With()`. Use a clickable or focusable View to receive the corresponding interaction states.

## Supported declarations

| Property | Supported values |
| --- | --- |
| `width`, `height`, `min-width`, `min-height`, `max-width`, `max-height` | Lengths for HuxerUI's outer `Frame` |
| `padding` | One to four lengths in CSS shorthand order |
| `gap` | A single length for the existing layout's spacing |
| `justify-content` | `start`, `end`, `flex-start`, `flex-end`, `center`, `space-between`, `space-around`, or `space-evenly` |
| `align-items` | `start`, `end`, `flex-start`, `flex-end`, `center`, or `stretch` |
| `background` | A solid color or one `linear-gradient()` |
| `background-color`, `color` | A solid background color or text foreground color |
| `font-size` | A positive length |
| `border` | `<length> solid <color>` or `none` |
| `border-radius` | One to four lengths in CSS corner shorthand order |
| `box-shadow` | One outer shadow with two offsets, optional blur and spread, and an explicit color, or `none` |
| `cursor` | `default`, `pointer`, `text`, `crosshair`, `move`, `grab`, `grabbing`, `ew-resize`, `ns-resize`, `nesw-resize`, `nwse-resize`, `not-allowed`, or `wait` |
| `opacity` | A number or percentage, clamped to the unit interval |
| `transform` | `none` or a sequence of `translate(x[, y])`, `translateX(x)`, `translateY(y)`, `scale(factor)`, and `rotate(angle)` |

One `px` is one HuxerUI logical unit. Lengths require `px` or unitless zero. The size, spacing, padding, border and corner lengths are non-negative. For example, `width: 120px; padding: 10px` requests an outer width of 120 and leaves 100 logical units for content when the parent grants that width. Borders paint inside the bounds. Alignment applies to the main or cross axis of the existing layout. Cursor shapes follow the HuxerUI hit route and are mapped by the host.

### Colors and fills

Use `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA`, `black`, `white`, or `transparent`. The numeric `rgb()`, `rgba()`, `hsl()`, and `hsla()` forms accept space-separated channels with optional slash alpha or comma-separated channels with optional comma alpha. For example, `rgb(37 99 235 / 60%)` and `hsla(220, 80%, 50%, 0.6)` both supply translucent colors. RGB channels accept numbers or percentages. HSL saturation and lightness use percentages.

`linear-gradient()` accepts `to top`, `to right`, `to bottom`, or `to left` and at least two color stops. The default direction is `to bottom`. Stops take an optional percentage from `0%` to `100%`, and omitted positions are distributed between the surrounding stops. Each view has one background fill. `background`, `background-color`, and HuxerUI's `Background` replace that fill in declaration order.

### Shadows

Provide the horizontal and vertical offset, followed by optional blur and spread. Put the color before or after the lengths. Offsets and spread may be negative, and blur must be non-negative. For example, `box-shadow: 0 4px 12px -2px rgb(0 0 0 / 25%)` uses HuxerUI's shadow blur falloff. Use `box-shadow: none` to clear a preceding shadow.

Separate declarations with semicolons. CSS comments and an optional final semicolon are accepted. Malformed declarations and unsupported properties or values throw `std::invalid_argument` at construction. The diagnostic includes a zero-based byte offset.

### Presentation

Opacity and transforms preserve the View's measured size. Translation accepts signed `px` lengths or unitless zero. Scale is uniform and non-negative. Scale and rotation use the View's center, and rotation accepts `deg`, `rad`, `grad`, `turn`, or unitless zero. In `transform: translate(12px, -4px) scale(1.2)`, the scale acts first.

The last declaration of each property wins within one `Css` or `CssClass` modifier. `transform: none` clears an earlier list in that modifier. Separate CSS and native presentation modifiers compose in `.With()` order, with opacity factors multiplying. These properties are available in base declarations.

## Preview and tests

The [preview app](examples/preview) demonstrates inline declarations, native overrides, and interaction and presentation styles loaded from a [packaged CSS file](examples/preview/resources/raw/styles.css). Run it on macOS from the repository root.

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

The [test suite](tests/css.cpp) compares layout and paint output against equivalent HuxerUI modifiers and checks grouped class selection, variable expansion, override order, cursor requests, interaction states, and invalid inputs.

## License

[MIT](LICENSE)
