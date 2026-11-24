#include <huxerui/css.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <locale>
#include <numbers>
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
  std::array<Token, 5> values{};
  std::size_t value_count = 0;

  std::span<const Token> Arguments() const {
    return {values.data(), value_count};
  }

  Token SingleValue() const {
    if (value_count != 1) Invalid(name, "expected one value");
    return values.front();
  }
};

class ValueReader {
public:
  explicit ValueReader(Token input) : input_(input) {}

  bool Empty() {
    SkipTrivia();
    return position_ == input_.text.size();
  }

  bool At(char character) {
    return !Empty() && input_.text[position_] == character;
  }

  bool Consume(char character) {
    if (!At(character)) return false;
    ++position_;
    return true;
  }

  Token Read(std::string_view property = {}) {
    SkipTrivia();
    if (property.empty()) property = input_.property;
    const std::size_t start = position_;
    std::size_t depth = 0;
    while (position_ < input_.text.size()) {
      const char character = input_.text[position_];
      if (StartsComment()) {
        if (depth == 0) break;
        ConsumeComment();
        continue;
      }
      if (character == '(') {
        ++depth;
      } else if (character == ')') {
        if (depth == 0) break;
        if (--depth == 0) {
          ++position_;
          break;
        }
      } else if (depth == 0 &&
                 (IsWhitespace(character) || character == ':' || character == ';' ||
                  character == ',' || character == '/')) {
        break;
      }
      ++position_;
    }
    if (position_ == start) {
      Invalid({input_.text.substr(start, 1), input_.offset + start, property}, "expected a token");
    }
    const Token token{input_.text.substr(start, position_ - start), input_.offset + start, property};
    if (depth != 0) Invalid(token, "unterminated function");
    return token;
  }

private:
  bool StartsComment() const {
    return input_.text.substr(position_).starts_with("/*");
  }

  void ConsumeComment() {
    const std::size_t end = input_.text.find("*/", position_ + 2);
    if (end == std::string_view::npos) {
      Invalid({"/*", input_.offset + position_, input_.property}, "unterminated comment");
    }
    position_ = end + 2;
  }

  void SkipTrivia() {
    while (position_ < input_.text.size()) {
      if (IsWhitespace(input_.text[position_])) {
        ++position_;
      } else if (StartsComment()) {
        ConsumeComment();
      } else {
        break;
      }
    }
  }

  Token input_;
  std::size_t position_ = 0;
};

bool ReadDeclaration(ValueReader& reader, Declaration& declaration) {
  while (reader.Consume(';')) {}
  if (reader.Empty()) return false;
  declaration = {.name = reader.Read()};
  if (!reader.Consume(':')) Invalid(declaration.name, "expected ':' after the property name");
  while (!reader.Empty() && !reader.Consume(';')) {
    if (reader.At(',')) Invalid(declaration.name, "expected a single value or layer");
    if (declaration.value_count == declaration.values.size()) Invalid(declaration.name, "too many values");
    declaration.values[declaration.value_count++] = reader.Read(declaration.name.text);
  }
  if (declaration.value_count == 0) Invalid(declaration.name, "expected a value");
  return true;
}

struct FunctionValue {
  std::string_view name;
  Token arguments;
};

std::optional<FunctionValue> AsFunction(Token token) {
  const std::size_t open = token.text.find('(');
  if (open == std::string_view::npos || !token.text.ends_with(')')) return std::nullopt;
  return FunctionValue{
      token.text.substr(0, open),
      {token.text.substr(open + 1, token.text.size() - open - 2), token.offset + open + 1, token.property},
  };
}

bool HasSuffix(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && MatchesKeyword(text.substr(text.size() - suffix.size()), suffix);
}

std::optional<float> ReadNumber(std::string_view number) {
  const auto point = number.find('.');
  if (point != std::string_view::npos &&
      (point + 1 == number.size() || number[point + 1] < '0' || number[point + 1] > '9')) {
    return std::nullopt;
  }

  // The SDK supports macOS versions whose libc++ does not provide floating-point from_chars.
  std::istringstream stream{std::string(number)};
  stream.imbue(std::locale::classic());
  float result = 0.0F;
  if (!(stream >> std::noskipws >> result) || !stream.eof() || !std::isfinite(result)) return std::nullopt;
  return result;
}

float ParseLength(Token value, bool signed_length = false) {
  auto number = value.text;
  const bool pixels = HasSuffix(number, "px");
  if (pixels) number.remove_suffix(2);

  const auto result = ReadNumber(number);
  if (!result || (!signed_length && *result < 0.0F) || (!pixels && *result != 0.0F)) {
    Invalid(value, signed_length ? "expected a signed px length or zero" : "expected a non-negative px length or zero");
  }
  return *result;
}

float ParseNumber(Token value, std::size_t unit_size = 0) {
  const auto number = ReadNumber(value.text.substr(0, value.text.size() - unit_size));
  if (!number) Invalid(value, "expected a finite number");
  return *number;
}

float ParsePercentage(Token value) {
  if (!value.text.ends_with('%')) Invalid(value, "expected a percentage");
  return ParseNumber(value, 1) / 100.0F;
}

float ParseColorComponent(Token value, float number_scale) {
  const float fraction = value.text.ends_with('%') ? ParsePercentage(value) : ParseNumber(value) / number_scale;
  return std::clamp(fraction, 0.0F, 1.0F);
}

float ParseHue(Token value) {
  constexpr std::pair<std::string_view, float> angle_units[] = {
      {"grad", 400.0F}, {"deg", 360.0F}, {"rad", 2.0F * std::numbers::pi_v<float>}, {"turn", 1.0F},
  };
  for (const auto& [unit, period] : angle_units) {
    if (HasSuffix(value.text, unit)) return std::fmod(ParseNumber(value, unit.size()), period) / period;
  }
  return std::fmod(ParseNumber(value), 360.0F) / 360.0F;
}

Color ParseFunctionalColor(const FunctionValue& function, Token value, bool rgb) {
  ValueReader reader(function.arguments);
  const Token first = reader.Read();
  const bool comma_separated = reader.Consume(',');
  const Token second = reader.Read();
  if (comma_separated && !reader.Consume(',')) Invalid(value, "expected ',' between color components");
  const Token third = reader.Read();

  float alpha = 1.0F;
  if (reader.Consume(comma_separated ? ',' : '/')) alpha = ParseColorComponent(reader.Read(), 1.0F);
  if (!reader.Empty()) Invalid(value, "unexpected color component or separator");

  if (rgb) {
    const bool percent = first.text.ends_with('%');
    if (comma_separated && (second.text.ends_with('%') != percent || third.text.ends_with('%') != percent)) {
      Invalid(value, "comma-separated RGB channels must use the same unit");
    }
    return {
        ParseColorComponent(first, 255.0F), ParseColorComponent(second, 255.0F),
        ParseColorComponent(third, 255.0F), alpha,
    };
  }

  const float turns = ParseHue(first);
  const float hue = turns < 0.0F ? turns + 1.0F : turns;
  const float saturation = std::clamp(ParsePercentage(second), 0.0F, 1.0F);
  const float lightness = std::clamp(ParsePercentage(third), 0.0F, 1.0F);
  const float chroma = saturation * std::min(lightness, 1.0F - lightness);
  const auto channel = [&](float position) {
    const float k = std::fmod(position + hue * 12.0F, 12.0F);
    return lightness - chroma * std::max(-1.0F, std::min({k - 3.0F, 9.0F - k, 1.0F}));
  };
  return {channel(0.0F), channel(8.0F), channel(4.0F), alpha};
}

Color ParseColor(Token value) {
  const auto text = value.text;
  if (MatchesKeyword(text, "black")) return Color::Black();
  if (MatchesKeyword(text, "white")) return Color::White();
  if (MatchesKeyword(text, "transparent")) return Color::Transparent();
  if (const auto function = AsFunction(value)) {
    if (MatchesKeyword(function->name, "rgb") || MatchesKeyword(function->name, "rgba")) {
      return ParseFunctionalColor(*function, value, true);
    }
    if (MatchesKeyword(function->name, "hsl") || MatchesKeyword(function->name, "hsla")) {
      return ParseFunctionalColor(*function, value, false);
    }
  }

  if (!text.starts_with('#') || (text.size() != 4 && text.size() != 5 && text.size() != 7 && text.size() != 9)) {
    Invalid(value, "expected a supported CSS color");
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

std::array<float, 4> ParseFourLengths(const Declaration& declaration) {
  const auto values = declaration.Arguments();
  if (values.size() > 4) Invalid(declaration.name, "too many values");
  const float first = ParseLength(values[0]);
  const float second = values.size() > 1 ? ParseLength(values[1]) : first;
  const float third = values.size() > 2 ? ParseLength(values[2]) : first;
  const float fourth = values.size() > 3 ? ParseLength(values[3]) : second;
  return {first, second, third, fourth};
}

Border ParseBorder(const Declaration& declaration) {
  const auto values = declaration.Arguments();
  if (values.size() == 1 && MatchesKeyword(values.front().text, "none")) {
    return {.color = Color::Transparent(), .width = 0.0F};
  }
  if (values.size() != 3 || !MatchesKeyword(values[1].text, "solid")) {
    Invalid(declaration.name, "expected '<width> solid <color>' or none");
  }
  return {.color = ParseColor(values[2]), .width = ParseLength(values[0])};
}

Shadow ParseShadow(const Declaration& declaration) {
  auto values = declaration.Arguments();
  if (values.size() == 1 && MatchesKeyword(values.front().text, "none")) {
    return {.color = Color::Transparent()};
  }
  if (values.size() < 3) Invalid(declaration.name, "expected two offsets and a color for a single outer shadow");
  const char first = LowerAscii(values.front().text.front());
  const bool color_first = first == '#' || (first >= 'a' && first <= 'z');
  const Color color = ParseColor(color_first ? values.front() : values.back());
  values = color_first ? values.subspan(1) : values.first(values.size() - 1);
  return {
      .color = color,
      .offset = {ParseLength(values[0], true), ParseLength(values[1], true)},
      .blur_radius = values.size() > 2 ? ParseLength(values[2]) : 0.0F,
      .spread = values.size() > 3 ? ParseLength(values[3], true) : 0.0F,
  };
}

template <class Enum, std::size_t Size>
Enum ParseKeyword(Token value, const std::pair<std::string_view, Enum> (&choices)[Size]) {
  for (const auto& [keyword, result] : choices) {
    if (MatchesKeyword(value.text, keyword)) return result;
  }
  Invalid(value, "unsupported keyword");
}

struct GradientAxis {
  Point start;
  Point end;
};

constexpr GradientAxis downwards{{0.5F, 0.0F}, {0.5F, 1.0F}};

constexpr std::pair<std::string_view, GradientAxis> gradient_axes[] = {
    {"top", {{0.5F, 1.0F}, {0.5F, 0.0F}}},
    {"right", {{0.0F, 0.5F}, {1.0F, 0.5F}}},
    {"bottom", downwards},
    {"left", {{1.0F, 0.5F}, {0.0F, 0.5F}}},
};

void ResolveGradientOffsets(std::vector<GradientStop>& stops) {
  // Negative offsets mark omitted stop positions. The explicit range is [0, 1].
  if (stops.front().offset < 0.0F) stops.front().offset = 0.0F;
  if (stops.back().offset < 0.0F) stops.back().offset = 1.0F;
  std::size_t previous = 0;
  for (std::size_t next = 1; next < stops.size(); ++next) {
    if (stops[next].offset < 0.0F) continue;
    const float start = stops[previous].offset;
    const float end = std::max(start, stops[next].offset);
    stops[next].offset = end;
    for (std::size_t index = previous + 1; index < next; ++index) {
      const float fraction = static_cast<float>(index - previous) / static_cast<float>(next - previous);
      stops[index].offset = std::lerp(start, end, fraction);
    }
    previous = next;
  }
}

Background ParseBackground(Token value) {
  const auto function = AsFunction(value);
  if (!function || !MatchesKeyword(function->name, "linear-gradient")) return Background(ParseColor(value));

  ValueReader reader(function->arguments);
  GradientAxis axis = downwards;
  Token color = reader.Read();
  if (MatchesKeyword(color.text, "to")) {
    axis = ParseKeyword(reader.Read(), gradient_axes);
    if (!reader.Consume(',')) Invalid(color, "expected ',' after the gradient direction");
    color = reader.Read();
  }
  LinearGradient gradient{.start = axis.start, .end = axis.end, .stops = {}};
  for (;;) {
    const Color stop_color = ParseColor(color);
    float offset = -1.0F;
    if (!reader.Empty() && !reader.At(',')) {
      const Token position = reader.Read();
      offset = ParsePercentage(position);
      if (offset < 0.0F || offset > 1.0F) Invalid(position, "gradient stop must be between 0% and 100%");
    }
    gradient.stops.push_back({offset, stop_color});
    if (reader.Empty()) break;
    if (!reader.Consume(',')) Invalid(color, "expected ',' between gradient stops");
    color = reader.Read();
  }
  if (gradient.stops.size() < 2) Invalid(value, "gradient requires at least two color stops");
  ResolveGradientOffsets(gradient.stops);
  return Background(std::move(gradient));
}

constexpr std::pair<std::string_view, MainAxisAlignment> main_alignments[] = {
    {"start", MainAxisAlignment::Start}, {"flex-start", MainAxisAlignment::Start},
    {"end", MainAxisAlignment::End}, {"flex-end", MainAxisAlignment::End},
    {"center", MainAxisAlignment::Center}, {"space-between", MainAxisAlignment::SpaceBetween},
    {"space-around", MainAxisAlignment::SpaceAround}, {"space-evenly", MainAxisAlignment::SpaceEvenly},
};

constexpr std::pair<std::string_view, CrossAxisAlignment> cross_alignments[] = {
    {"start", CrossAxisAlignment::Start}, {"flex-start", CrossAxisAlignment::Start},
    {"end", CrossAxisAlignment::End}, {"flex-end", CrossAxisAlignment::End},
    {"center", CrossAxisAlignment::Center}, {"stretch", CrossAxisAlignment::Stretch},
};

constexpr std::pair<std::string_view, PointerCursorKind> cursors[] = {
    {"default", PointerCursorKind::Default}, {"text", PointerCursorKind::Text}, {"pointer", PointerCursorKind::Hand},
    {"crosshair", PointerCursorKind::Crosshair}, {"move", PointerCursorKind::Move},
    {"grab", PointerCursorKind::Grab}, {"grabbing", PointerCursorKind::Grabbing},
    {"ew-resize", PointerCursorKind::ResizeHorizontal}, {"ns-resize", PointerCursorKind::ResizeVertical},
    {"nesw-resize", PointerCursorKind::ResizeNorthEastSouthWest},
    {"nwse-resize", PointerCursorKind::ResizeNorthWestSouthEast},
    {"not-allowed", PointerCursorKind::NotAllowed}, {"wait", PointerCursorKind::Wait},
};

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
  if (MatchesKeyword(name, "padding")) {
    const auto [top, right, bottom, left] = ParseFourLengths(declaration);
    return detail::MakeModifierSpec(Padding(EdgeInsets{top, right, bottom, left}));
  }
  if (MatchesKeyword(name, "border")) return detail::MakeModifierSpec(ParseBorder(declaration));
  if (MatchesKeyword(name, "border-radius")) {
    const auto [top_left, top_right, bottom_right, bottom_left] = ParseFourLengths(declaration);
    return detail::MakeModifierSpec(CornerRadius(top_left, top_right, bottom_right, bottom_left));
  }
  if (MatchesKeyword(name, "box-shadow")) return detail::MakeModifierSpec(ParseShadow(declaration));
  if (MatchesKeyword(name, "background")) return detail::MakeModifierSpec(ParseBackground(declaration.SingleValue()));
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
  if (MatchesKeyword(name, "justify-content")) {
    return detail::MakeModifierSpec(MainAlign(ParseKeyword(declaration.SingleValue(), main_alignments)));
  }
  if (MatchesKeyword(name, "align-items")) {
    return detail::MakeModifierSpec(CrossAlign(ParseKeyword(declaration.SingleValue(), cross_alignments)));
  }
  if (MatchesKeyword(name, "cursor")) {
    return detail::MakeModifierSpec(PointerCursor(ParseKeyword(declaration.SingleValue(), cursors)));
  }
  Invalid(declaration.name, "unsupported CSS property");
}

} // namespace

Css::Css(std::string_view declarations) {
  auto impl = std::make_shared<Impl>();
  Frame frame;
  Token min_width;
  Token min_height;
  ValueReader reader(Token{declarations});
  Declaration declaration{};
  while (ReadDeclaration(reader, declaration)) {
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
