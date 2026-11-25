#pragma once

/// @file
/// @brief Inline and packaged CSS styles for HuxerUI views.
///
/// Include `<huxerui/css.h>` and link `HuxerUI::CSS`. Pass a Css or CssClass value to View::With().

#include <concepts>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <utility>

#include <huxerui/app.h>
#include <huxerui/modifier.h>
#include <huxerui/resource.h>

namespace huxerui::css {

/// @brief Creates an application hook that loads a packaged style sheet before the first window opens.
/// @param resource Generated key for a CSS file under `resources/raw`.
/// @return A hook to add to AppOptions::application_hooks.
///
/// Register one sheet per application. The hook reads and parses it once per Runtime and shares it across windows.
/// The sheet supports comma-separated class selectors and global custom properties declared in `:root`.
/// References using `var(--name, fallback)` are resolved while loading the sheet.
/// The hook propagates resource errors and throws std::invalid_argument for invalid CSS.
ApplicationHook InstallStyleSheet(RawResource resource);

/// @brief A reusable declaration list that styles one View through View::With().
///
/// The last declaration for a property within the list wins. View::With() applies modifiers left to right,
/// so a later HuxerUI modifier can override the same property.
///
/// Lengths are in `px` or unitless zero. One `px` corresponds to one HuxerUI logical unit. Width, height,
/// and their min/max variants use Frame's outer dimensions. Padding and border-radius accept one to four lengths.
/// Gap, justify-content, and align-items apply to the existing layout, such as Row or Column. Cursor follows the
/// HuxerUI pointer hit route on hosts with cursor support.
///
/// Background accepts a solid color or a single axis-aligned linear-gradient(). Background and background-color
/// replace the same fill. Border accepts `<length> solid <color>` or `none` and paints inside the bounds.
/// Box-shadow accepts a single outer shadow with an explicit color, or `none`.
///
/// Colors accept hexadecimal forms, black, white, transparent, and numeric rgb(), rgba(), hsl(), and hsla()
/// functions. Font size takes a positive length.
///
/// Opacity and basic 2D transforms affect presentation while preserving layout. Transform lists accept translation,
/// uniform non-negative scale, and rotation around the View center. Separate native modifiers compose with CSS effects.
///
/// Use `&:hover { ... }`, `&:active { ... }`, and `&:focus-visible { ... }` for interaction feedback.
/// Each block accepts background, background-color, color, border, and border-radius. State backgrounds blend
/// over the base fill behind the content. HuxerUI selects active, hover, then visible focus in that order.
/// A state declaration replaces the View's default indication, including its ripple.
///
/// @code{.cpp}
/// return Text("Save").With(
///     Css("padding: 8px 16px; background-color: #2563eb; color: white;"),
///     Padding(12.0F)
/// );
/// @endcode
class Css {
public:
  /// @brief Parses and owns the declarations. Copies share the parsed data and can be reused across compositions.
  /// @param declarations Semicolon-separated property-value pairs and optional interaction-state blocks.
  /// CSS comments and an optional final semicolon are accepted. Empty declarations leave the View unchanged.
  /// @throws std::invalid_argument If the input is malformed, contains an unsupported property or value,
  /// or resolves to a minimum dimension greater than its maximum. The diagnostic includes a zero-based byte offset.
  explicit Css(std::string_view declarations);

  /// @brief Applies the parsed declarations at this modifier's position in View::With().
  /// @param context The SDK's modifier context for the current View.
  void Configure(ModifierContext& context) const;

private:
  struct Impl;
  std::shared_ptr<const Impl> impl_;
};

/// @brief Applies one or more classes from the application's installed style sheet.
///
/// Pass case-sensitive class names without a leading dot, such as `CssClass("button", "primary")`.
/// Matching rules apply in sheet order. Selected interaction rules produce one Indication for this modifier.
class CssClass {
public:
  /// @brief Stores owned copies of the class names.
  /// @throws std::invalid_argument If a name is empty or contains unsupported characters.
  template <class... Names>
    requires (std::convertible_to<Names, std::string_view> && ...)
  explicit CssClass(std::string_view name, Names&&... names)
      : CssClass({name, std::string_view(std::forward<Names>(names))...}) {}

  /// @brief Applies the selected rules at this modifier's position in View::With().
  /// @param context The SDK's modifier context for the current View.
  /// @throws std::logic_error If no style sheet is installed.
  /// @throws std::invalid_argument If the installed sheet has no rule for a requested class.
  void Configure(ModifierContext& context) const;

private:
  explicit CssClass(std::initializer_list<std::string_view> names);

  struct Impl;
  std::shared_ptr<const Impl> impl_;
};

} // namespace huxerui::css
