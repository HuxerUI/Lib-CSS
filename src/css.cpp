#include <huxerui/css.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <locale>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace huxerui::css {

struct Css::Impl {
  std::vector<detail::ModifierSpec> properties;
};

namespace {

struct Token {
  std::string_view text;
  std::size_t offset = 0;
  std::string_view property{};
};

[[noreturn]] void Invalid(Token token, std::string_view reason) {
  std::string message = "Lib-CSS at byte " + std::to_string(token.offset);
  if (!token.property.empty()) {
    message += " in `";
    message += token.property;
    message += '`';
  }
  if (!token.text.empty()) {
    message += " near `";
    message += token.text;
    message += '`';
  }
  message += ": ";
  message += reason;
  throw std::invalid_argument(message);
}

bool IsWhitespace(char character) {
  return character == ' ' || character == '\t' || character == '\n' || character == '\r' || character == '\f';
}

char LowerAscii(char character) {
  return character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A')) : character;
}

bool MatchesKeyword(std::string_view text, std::string_view keyword) {
  return std::ranges::equal(text, keyword, {}, LowerAscii);
}

struct Declaration {
  Token name;
  std::array<Token, 4> values{};
  std::size_t value_count = 0;

  std::span<const Token> Arguments() const {
    return {values.data(), value_count};
  }

  Token SingleValue() const {
    if (value_count != 1) Invalid(name, "expected one value");
    return values.front();
  }
};

class DeclarationReader {
public:
  explicit DeclarationReader(std::string_view source) : source_(source) {}

  bool Next(Declaration& declaration) {
    SkipTrivia();
    while (Consume(';')) SkipTrivia();
    if (position_ == source_.size()) return false;

    declaration = {.name = ReadToken()};
    SkipTrivia();
    if (!Consume(':')) Invalid(declaration.name, "expected ':' after the property name");
    for (SkipTrivia(); position_ < source_.size() && !Consume(';'); SkipTrivia()) {
      if (declaration.value_count == declaration.values.size()) {
        Invalid(declaration.name, "too many values");
      }
      declaration.values[declaration.value_count++] = ReadToken(declaration.name.text);
    }
    if (declaration.value_count == 0) Invalid(declaration.name, "expected a value");
    return true;
  }

private:
  bool Consume(char character) {
    if (position_ == source_.size() || source_[position_] != character) return false;
    ++position_;
    return true;
  }

  bool StartsComment() const {
    return source_.substr(position_).starts_with("/*");
  }

  void SkipTrivia() {
    while (position_ < source_.size()) {
      if (IsWhitespace(source_[position_])) {
        ++position_;
      } else if (StartsComment()) {
        const std::size_t end = source_.find("*/", position_ + 2);
        if (end == std::string_view::npos) Invalid({"/*", position_}, "unterminated comment");
        position_ = end + 2;
      } else {
        break;
      }
    }
  }

  Token ReadToken(std::string_view property = {}) {
    const std::size_t start = position_;
    while (position_ < source_.size()) {
      const char character = source_[position_];
      if (IsWhitespace(character) || character == ':' || character == ';' || StartsComment()) break;
      ++position_;
    }
    if (position_ == start) Invalid({source_.substr(start, 1), start, property}, "expected a token");
    return {source_.substr(start, position_ - start), start, property};
  }

  std::string_view source_;
  std::size_t position_ = 0;
};

float ParseLength(Token value) {
  std::string_view number = value.text;
  const bool pixels = number.size() >= 2 && MatchesKeyword(number.substr(number.size() - 2), "px");
  if (pixels) number.remove_suffix(2);

  // The SDK supports macOS versions whose libc++ does not provide floating-point from_chars.
  std::istringstream stream{std::string(number)};
  stream.imbue(std::locale::classic());
  float result = 0.0F;
  if (!(stream >> std::noskipws >> result) || !stream.eof() || !std::isfinite(result) || result < 0.0F ||
      (!pixels && result != 0.0F)) {
    Invalid(value, "expected a non-negative px length or zero");
  }
  return result;
}

Color ParseColor(Token value) {
  const auto text = value.text;
  if (MatchesKeyword(text, "black")) return Color::Black();
  if (MatchesKeyword(text, "white")) return Color::White();
  if (MatchesKeyword(text, "transparent")) return Color::Rgb(0, 0, 0, 0.0F);

  if (!text.starts_with('#') || (text.size() != 4 && text.size() != 5 && text.size() != 7 && text.size() != 9)) {
    Invalid(value, "expected a hexadecimal color, black, white, or transparent");
  }
  const bool shorthand = text.size() <= 5;
  std::uint32_t rgba = 0;
  for (char character : text.substr(1)) {
    const char digit = LowerAscii(character);
    const int hex = digit >= '0' && digit <= '9' ? digit - '0' : digit - 'a' + 10;
    if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'))) {
      Invalid(value, "invalid hexadecimal color");
    }
    const auto channel = static_cast<std::uint32_t>(hex);
    rgba = shorthand ? (rgba << 8) | (channel * 17) : (rgba << 4) | channel;
  }
  if (text.size() == 4 || text.size() == 7) rgba = (rgba << 8) | 0xff;
  return Color::Rgba32(rgba);
}

Padding ParsePadding(std::span<const Token> values) {
  const float top = ParseLength(values[0]);
  const float right = values.size() > 1 ? ParseLength(values[1]) : top;
  const float bottom = values.size() > 2 ? ParseLength(values[2]) : top;
  const float left = values.size() > 3 ? ParseLength(values[3]) : right;
  return Padding(EdgeInsets{top, right, bottom, left});
}

Border ParseBorder(const Declaration& declaration) {
  const auto values = declaration.Arguments();
  if (values.size() != 3 || !MatchesKeyword(values[1].text, "solid")) {
    Invalid(declaration.name, "expected '<width> solid <color>'");
  }
  return {.color = ParseColor(values[2]), .width = ParseLength(values[0])};
}

using FrameField = std::optional<float> Frame::*;

constexpr std::array<std::pair<std::string_view, FrameField>, 6> dimensions{{
    {"width", &Frame::width}, {"min-width", &Frame::min_width}, {"max-width", &Frame::max_width},
    {"height", &Frame::height}, {"min-height", &Frame::min_height}, {"max-height", &Frame::max_height},
}};

void ValidateBounds(std::optional<float> minimum, std::optional<float> maximum, Token minimum_property) {
  if (minimum && maximum && *minimum > *maximum) {
    Invalid(minimum_property, "minimum size exceeds maximum size");
  }
}

detail::ModifierSpec ParseProperty(const Declaration& declaration) {
  const auto name = declaration.name.text;
  if (MatchesKeyword(name, "padding")) return detail::MakeModifierSpec(ParsePadding(declaration.Arguments()));
  if (MatchesKeyword(name, "border")) return detail::MakeModifierSpec(ParseBorder(declaration));
  if (MatchesKeyword(name, "border-radius")) {
    return detail::MakeModifierSpec(CornerRadius(ParseLength(declaration.SingleValue())));
  }
  if (MatchesKeyword(name, "background-color")) {
    return detail::MakeModifierSpec(Background(ParseColor(declaration.SingleValue())));
  }
  if (MatchesKeyword(name, "color")) return detail::MakeModifierSpec(Foreground(ParseColor(declaration.SingleValue())));
  if (MatchesKeyword(name, "font-size")) {
    const Token value = declaration.SingleValue();
    const float size = ParseLength(value);
    if (size == 0.0F) Invalid(value, "font size must be positive");
    return detail::MakeModifierSpec(FontSize(size));
  }
  if (MatchesKeyword(name, "gap")) return detail::MakeModifierSpec(Spacing(ParseLength(declaration.SingleValue())));
  Invalid(declaration.name, "unsupported CSS property");
}

} // namespace

Css::Css(std::string_view declarations) {
  auto impl = std::make_shared<Impl>();
  Frame frame;
  Token min_width;
  Token min_height;
  DeclarationReader reader(declarations);
  Declaration declaration{};
  while (reader.Next(declaration)) {
    const auto dimension = std::ranges::find_if(dimensions, [&](const auto& entry) {
      return MatchesKeyword(declaration.name.text, entry.first);
    });
    if (dimension == dimensions.end()) {
      impl->properties.push_back(ParseProperty(declaration));
      continue;
    }
    const FrameField field = dimension->second;
    frame.*field = ParseLength(declaration.SingleValue());
    if (field == &Frame::min_width) min_width = declaration.name;
    if (field == &Frame::min_height) min_height = declaration.name;
  }
  if (frame != Frame{}) {
    // Merge size declarations before applying the frame so only the final bounds are validated.
    ValidateBounds(frame.min_width, frame.max_width, min_width);
    ValidateBounds(frame.min_height, frame.max_height, min_height);
    impl->properties.push_back(detail::MakeModifierSpec(frame));
  }
  for (const auto& property : impl->properties) {
    const auto& descriptor = *property.descriptor;
    if (descriptor.compile == nullptr || descriptor.create_extension != nullptr) {
      throw std::logic_error("Lib-CSS requires a compatible HuxerUI property modifier descriptor");
    }
  }
  impl_ = std::move(impl);
}

const detail::ModifierDescriptor& Css::Descriptor() {
  static const detail::ModifierDescriptor descriptor{
      .compile = [](detail::ViewSpec& view, detail::ModifierSpec& modifier,
                    const std::shared_ptr<const Environment>& environment, detail::AppResources& resources) {
        const auto& css = *static_cast<const Css*>(modifier.value.get());
        if (!css.impl_) return;
        for (auto property : css.impl_->properties) {
          property.descriptor->compile(view, property, environment, resources);
        }
      },
  };
  return descriptor;
}

} // namespace huxerui::css
