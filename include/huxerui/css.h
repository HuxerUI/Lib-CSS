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
/// Supported declarations are width, height, their min/max variants, padding, gap, background-color, color,
/// font-size, border, and border-radius. A length is a non-negative number in `px` or a unitless zero.
/// One `px` corresponds to one HuxerUI logical unit. The size declarations use Frame's outer dimensions,
/// including padding. The gap declaration sets Row or Column spacing. Font size must be positive.
///
/// Padding accepts one to four lengths in CSS shorthand order. Border accepts `<length> solid <color>`,
/// with the border painted inside the bounds. Border radius accepts one radius for all corners.
/// Colors accept `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA`, `black`, `white`, and `transparent`.
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
  /// @param declarations Semicolon-separated property-value pairs. CSS comments and an optional final semicolon
  /// are accepted. An empty list leaves the View unchanged.
  /// @throws std::invalid_argument If the input is malformed, contains an unsupported property or value,
  /// or resolves to a minimum dimension greater than its maximum. The diagnostic includes a zero-based byte offset.
  /// @throws std::logic_error If the linked HuxerUI SDK does not provide a compatible property-only descriptor.
  explicit Css(std::string_view declarations);

  /// @brief Provides the descriptor used by View::With() to apply the parsed declarations.
  /// @return A reference to the library-owned descriptor with static storage duration.
  static const detail::ModifierDescriptor& Descriptor();

private:
  struct Impl;
  std::shared_ptr<const Impl> impl_;
};

} // namespace huxerui::css
