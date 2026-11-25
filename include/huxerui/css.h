#pragma once

/// @file
/// @brief Inline CSS declarations for HuxerUI views.
///
/// Include `<huxerui/css.h>` and link `HuxerUI::CSS`. Pass a huxerui::css::Css value to View::With().

#include <memory>
#include <string_view>

#include <huxerui/modifier.h>

namespace huxerui::css {

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

} // namespace huxerui::css
