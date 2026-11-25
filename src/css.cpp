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
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <huxerui/animation.h>
#include <huxerui/indication.h>

namespace huxerui::css {

namespace {

using Property = std::variant<
    Padding, Border, CornerRadius, Shadow, Background, Foreground, FontSize, Spacing, MainAlign, CrossAlign,
    PointerCursor>;

using TransformOperation = std::variant<Offset, Scale, Rotation>;
using TransformList = std::vector<TransformOperation>;

struct BaseRule {
  std::vector<Property> properties;
  Frame frame;
  std::optional<float> opacity;
  std::optional<TransformList> transform;
};

} // namespace

struct Css::Impl {
  BaseRule base;
  Indication indication;
};

struct CssClass::Impl {
  std::vector<std::string> names;
};

namespace {

struct SheetSource {
  std::string_view name;
  std::string_view text;
  std::string_view selector;
};

struct Token {
  std::string_view text;
  std::size_t offset = 0;
  std::string_view property{};
  const SheetSource* source = nullptr;
  bool fixed_offset = false;

  Token Slice(std::size_t start, std::size_t length, std::string_view property_name) const {
    return {text.substr(start, length), offset + (fixed_offset ? 0 : start), property_name, source, fixed_offset};
  }
};

[[noreturn]] void Invalid(Token token, std::string_view reason) {
  std::string message = "Lib-CSS ";
  if (token.source) {
    const auto before = token.source->text.substr(0, token.offset);
    const auto line = std::ranges::count(before, '\n') + 1;
    const auto line_break = before.find_last_of('\n');
    const auto column = line_break == std::string_view::npos ? before.size() + 1 : before.size() - line_break;
    message += token.source->name;
    message += ":" + std::to_string(line) + ":" + std::to_string(column);
    if (!token.source->selector.empty()) {
      message += " in rule `";
      message += token.source->selector;
      message += '`';
    }
  } else {
    message += "at byte " + std::to_string(token.offset);
  }
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

bool IsClassName(std::string_view name) {
  const auto is_name_start = [](char character) {
    const char lower = LowerAscii(character);
    return (lower >= 'a' && lower <= 'z') || character == '_' || character == '-';
  };
  if (name.starts_with('-')) name.remove_prefix(1);
  return !name.empty() && is_name_start(name.front()) && std::ranges::all_of(name.substr(1), [&](char character) {
    return is_name_start(character) || (character >= '0' && character <= '9');
  });
}

bool IsVariableName(std::string_view name) {
  return name.size() > 2 && name.starts_with("--") && IsClassName(name);
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

  Token Remaining() {
    SkipTrivia();
    return input_.Slice(position_, std::string_view::npos, input_.property);
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
                 (IsWhitespace(character) || character == ':' || character == ';' || character == ',' ||
                  character == '/' || character == '{' || character == '}' || character == '&')) {
        break;
      }
      ++position_;
    }
    if (position_ == start) {
      Invalid(input_.Slice(start, 1, property), "expected a token");
    }
    const Token token = input_.Slice(start, position_ - start, property);
    if (depth != 0) Invalid(token, "unterminated function");
    return token;
  }

  Token ReadValue(std::string_view property) {
    SkipTrivia();
    const auto start = position_;
    while (!Empty() && !At(';') && !At('}')) {
      if (!Consume(',') && !Consume('/')) Read(property);
    }
    return input_.Slice(start, position_ - start, property);
  }

private:
  bool StartsComment() const {
    return input_.text.substr(position_).starts_with("/*");
  }

  void ConsumeComment() {
    const std::size_t end = input_.text.find("*/", position_ + 2);
    if (end == std::string_view::npos) {
      Invalid(input_.Slice(position_, 2, input_.property), "unterminated comment");
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

Token ReadDeclarationName(ValueReader& reader) {
  const Token name = reader.Read();
  if (!reader.Consume(':')) Invalid(name, "expected ':' after the property name");
  return name;
}

Declaration ReadDeclarationValues(ValueReader& reader, Token name) {
  Declaration declaration{.name = name};
  if (MatchesKeyword(name.text, "transform")) {
    const auto value = reader.ReadValue(name.text);
    if (value.text.empty()) Invalid(name, "expected a value");
    declaration.values[0] = value;
    declaration.value_count = 1;
    return declaration;
  }
  while (!reader.Empty() && !reader.At('}') && !reader.Consume(';')) {
    if (reader.At(',')) Invalid(declaration.name, "expected a single value or layer");
    if (declaration.value_count == declaration.values.size()) Invalid(declaration.name, "too many values");
    declaration.values[declaration.value_count++] = reader.Read(declaration.name.text);
  }
  if (declaration.value_count == 0) Invalid(declaration.name, "expected a value");
  return declaration;
}

Declaration ReadDeclaration(ValueReader& reader) {
  return ReadDeclarationValues(reader, ReadDeclarationName(reader));
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
      token.Slice(open + 1, token.text.size() - open - 2, token.property),
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

float ParseAngleTurns(Token value, bool unitless_degrees = false) {
  constexpr std::pair<std::string_view, float> angle_units[] = {
      {"grad", 400.0F}, {"deg", 360.0F}, {"rad", 2.0F * std::numbers::pi_v<float>}, {"turn", 1.0F},
  };
  for (const auto& [unit, period] : angle_units) {
    if (HasSuffix(value.text, unit)) return std::fmod(ParseNumber(value, unit.size()), period) / period;
  }
  const float degrees = ParseNumber(value);
  if (!unitless_degrees && degrees != 0.0F) Invalid(value, "expected an angle with a unit or zero");
  return std::fmod(degrees, 360.0F) / 360.0F;
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

  const float turns = ParseAngleTurns(first, true);
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

CornerRadii ParseCornerRadii(const Declaration& declaration) {
  const auto [top_left, top_right, bottom_right, bottom_left] = ParseFourLengths(declaration);
  return {top_left, top_right, bottom_right, bottom_left};
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

TransformOperation ParseTransformFunction(Token token) {
  const auto function = AsFunction(token);
  if (!function) Invalid(token, "expected a transform function");
  const auto name = function->name;
  const bool translate = MatchesKeyword(name, "translate");
  const bool translate_x = MatchesKeyword(name, "translatex");
  const bool translate_y = MatchesKeyword(name, "translatey");
  const bool scale = MatchesKeyword(name, "scale");
  if (!translate && !translate_x && !translate_y && !scale && !MatchesKeyword(name, "rotate")) {
    Invalid(token, "unsupported CSS transform function");
  }

  ValueReader arguments(function->arguments);
  const auto first = arguments.Read();
  if (translate) {
    const float x = ParseLength(first, true);
    const float y = arguments.Consume(',') ? ParseLength(arguments.Read(), true) : 0.0F;
    if (!arguments.Empty()) Invalid(token, "expected one or two comma-separated translation lengths");
    return Offset(Point{x, y});
  }
  if (!arguments.Empty()) Invalid(token, "expected one transform argument");
  if (translate_x || translate_y) {
    const float distance = ParseLength(first, true);
    return Offset(translate_x ? Point{distance, 0.0F} : Point{0.0F, distance});
  }
  if (scale) {
    const float factor = ParseNumber(first);
    if (factor < 0.0F) Invalid(first, "expected a non-negative scale");
    return Scale(factor);
  }
  return Rotation(ParseAngleTurns(first) * 360.0F);
}

TransformList ParseTransform(Token value) {
  ValueReader reader(value);
  const auto first = reader.Read();
  if (MatchesKeyword(first.text, "none")) {
    if (!reader.Empty()) Invalid(first, "transform none must appear alone");
    return {};
  }
  TransformList transform;
  transform.push_back(ParseTransformFunction(first));
  while (!reader.Empty()) {
    transform.push_back(ParseTransformFunction(reader.Read()));
  }
  return transform;
}

Property ParseProperty(const Declaration& declaration) {
  const auto name = declaration.name.text;
  if (MatchesKeyword(name, "padding")) {
    const auto [top, right, bottom, left] = ParseFourLengths(declaration);
    return Padding(EdgeInsets{top, right, bottom, left});
  }
  if (MatchesKeyword(name, "border")) return ParseBorder(declaration);
  if (MatchesKeyword(name, "border-radius")) return CornerRadius(ParseCornerRadii(declaration));
  if (MatchesKeyword(name, "box-shadow")) return ParseShadow(declaration);
  if (MatchesKeyword(name, "background")) return ParseBackground(declaration.SingleValue());
  if (MatchesKeyword(name, "background-color")) {
    return Background(ParseColor(declaration.SingleValue()));
  }
  if (MatchesKeyword(name, "color")) return Foreground(ParseColor(declaration.SingleValue()));
  if (MatchesKeyword(name, "font-size")) {
    const Token value = declaration.SingleValue();
    const float size = ParseLength(value);
    if (size == 0.0F) Invalid(value, "font size must be positive");
    return FontSize(size);
  }
  if (MatchesKeyword(name, "gap")) return Spacing(ParseLength(declaration.SingleValue()));
  if (MatchesKeyword(name, "justify-content")) {
    return MainAlign(ParseKeyword(declaration.SingleValue(), main_alignments));
  }
  if (MatchesKeyword(name, "align-items")) {
    return CrossAlign(ParseKeyword(declaration.SingleValue(), cross_alignments));
  }
  if (MatchesKeyword(name, "cursor")) {
    return PointerCursor(ParseKeyword(declaration.SingleValue(), cursors));
  }
  Invalid(declaration.name, "unsupported CSS property");
}

void ApplyStateProperty(IndicationLayer& layer, const Declaration& declaration) {
  const auto name = declaration.name.text;
  if (MatchesKeyword(name, "background")) {
    layer.fill = ParseBackground(declaration.SingleValue()).fill;
  } else if (MatchesKeyword(name, "background-color")) {
    layer.fill = ParseColor(declaration.SingleValue());
  } else if (MatchesKeyword(name, "color")) {
    layer.foreground = ParseColor(declaration.SingleValue());
  } else if (MatchesKeyword(name, "border")) {
    layer.border = ParseBorder(declaration);
  } else if (MatchesKeyword(name, "border-radius")) {
    layer.corner_radii = ParseCornerRadii(declaration);
  } else {
    Invalid(declaration.name, "unsupported property in an interaction state block");
  }
}

FrameField AddBaseProperty(const Declaration& declaration, BaseRule& rule) {
  const auto name = declaration.name.text;
  if (MatchesKeyword(name, "opacity")) {
    const auto value = declaration.SingleValue();
    rule.opacity = value.text.ends_with('%') ? ParsePercentage(value) : ParseNumber(value);
    return nullptr;
  }
  if (MatchesKeyword(name, "transform")) {
    rule.transform = ParseTransform(declaration.SingleValue());
    return nullptr;
  }
  const auto dimension = std::ranges::find_if(dimensions, [&](const auto& entry) {
    return MatchesKeyword(name, entry.first);
  });
  if (dimension == dimensions.end()) {
    rule.properties.push_back(ParseProperty(declaration));
    return nullptr;
  }
  const FrameField field = dimension->second;
  rule.frame.*field = ParseLength(declaration.SingleValue());
  return field;
}

using StateField = std::optional<IndicationLayer> Indication::*;

constexpr std::pair<std::string_view, StateField> states[] = {
    {"hover", &Indication::hover}, {"active", &Indication::press}, {"focus-visible", &Indication::focus},
};

template <class Apply> void ReadBlock(ValueReader& reader, Token selector, Apply apply) {
  if (!reader.Consume('{')) Invalid(selector, "expected '{' after the selector");
  while (!reader.Consume('}')) {
    if (reader.Empty()) Invalid(selector, "unterminated CSS block");
    if (reader.Consume(';')) continue;
    apply(reader);
  }
}

void AddStateProperty(std::optional<IndicationLayer>& layer, const Declaration& declaration) {
  if (!layer) {
    layer.emplace(
        IndicationLayer{.placement = IndicationPlacement::BehindContent, .enter = SnapSpec{}, .exit = SnapSpec{}}
    );
  }
  ApplyStateProperty(*layer, declaration);
}

void ReadStateBlock(ValueReader& reader, Token selector, std::optional<IndicationLayer>& layer) {
  ReadBlock(reader, selector, [&](ValueReader& body) {
    AddStateProperty(layer, ReadDeclaration(body));
  });
}

void ApplyProperties(ModifierContext& context, const std::vector<Property>& properties) {
  for (const auto& property : properties) {
    std::visit([&context](const auto& value) { context.Apply(value); }, property);
  }
}

void ApplyMergedProperties(ModifierContext& context, const Frame& frame, std::optional<float> opacity,
                           const TransformList* transform, const Indication& indication) {
  if (frame != Frame{}) {
    context.Apply(frame);
  }
  if (opacity) {
    context.Apply(Opacity(*opacity));
  }
  if (transform) {
    // CSS applies the rightmost function first. HuxerUI applies native transforms in modifier order.
    for (auto operation = transform->rbegin(); operation != transform->rend(); ++operation) {
      std::visit([&context](const auto& value) { context.Apply(value); }, *operation);
    }
  }
  if (indication.hover || indication.press || indication.focus) {
    context.Apply(indication);
  }
}

void MergeLayer(std::optional<IndicationLayer>& target, const std::optional<IndicationLayer>& source) {
  if (!source) return;
  if (!target) {
    target = source;
    return;
  }
  if (source->fill) target->fill = source->fill;
  if (source->foreground) target->foreground = source->foreground;
  if (source->border) target->border = source->border;
  if (source->corner_radii) target->corner_radii = source->corner_radii;
}

struct StateRule {
  StateField field;
  std::optional<IndicationLayer> layer{};
};

using Rule = std::variant<BaseRule, StateRule>;

struct RawDeclaration {
  Token name;
  Token value;
};

RawDeclaration ReadRawDeclaration(ValueReader& reader) {
  const auto name = ReadDeclarationName(reader);
  const auto value = reader.ReadValue(name.text);
  if (value.text.empty()) Invalid(name, "expected a value");
  return {name, value};
}

class SheetVariables {
public:
  void Set(Token name, Token value) {
    if (!IsVariableName(name.text)) Invalid(name, "expected a custom property name");
    variables_.insert_or_assign(name.text, Variable{.value = value});
  }

  std::optional<std::string> Expand(Token value) {
    ValueReader reader(value);
    std::optional<std::string> result;
    std::size_t copied = 0;
    while (!reader.Empty()) {
      if (reader.Consume(',') || reader.Consume('/')) continue;
      const auto token = reader.Read();
      const auto function = AsFunction(token);
      if (!function) continue;

      const bool is_reference = MatchesKeyword(function->name, "var");
      const auto replaced = is_reference ? token : function->arguments;
      const auto replacement = is_reference
                                 ? std::optional<std::string>(ResolveReference(token, function->arguments))
                                 : Expand(function->arguments);
      if (!replacement) continue;

      if (!result) result.emplace();
      const auto start = replaced.offset - value.offset;
      result->append(value.text.substr(copied, start - copied));
      // Keep substituted tokens separate from adjacent literals.
      if (is_reference) result->push_back(' ');
      result->append(*replacement);
      if (is_reference) result->push_back(' ');
      copied = start + replaced.text.size();
    }
    if (result) result->append(value.text.substr(copied));
    return result;
  }

private:
  struct Variable {
    Token value;
    std::optional<std::string> resolved{};
    bool resolving = false;
  };

  std::string ResolveReference(Token reference, Token arguments) {
    ValueReader reader(arguments);
    const auto name = reader.Read();
    if (!IsVariableName(name.text)) Invalid(name, "expected a custom property name in var()");
    const bool has_fallback = reader.Consume(',');
    if (!has_fallback && !reader.Empty()) Invalid(name, "expected ',' before the var() fallback");

    const auto found = variables_.find(name.text);
    if (found == variables_.end()) {
      if (!has_fallback) Invalid(name, "undefined CSS variable");
      const auto fallback = reader.Remaining();
      auto replacement = Expand(fallback);
      return replacement ? std::move(*replacement) : std::string(fallback.text);
    }

    auto& variable = found->second;
    if (variable.resolving) Invalid(reference, "cyclic CSS variable reference");
    if (!variable.resolved) {
      variable.resolving = true;
      auto replacement = Expand(variable.value);
      variable.resolved = replacement ? std::move(*replacement) : std::string(variable.value.text);
      variable.resolving = false;
    }
    return *variable.resolved;
  }

  std::unordered_map<std::string_view, Variable> variables_;
};

struct Selector {
  std::string_view name;
  StateField state = nullptr;
};

Selector ReadSelector(ValueReader& reader) {
  const auto selector = reader.Read();
  if (!selector.text.starts_with('.') || !IsClassName(selector.text.substr(1))) {
    Invalid(selector, "expected a class selector");
  }
  Selector result{selector.text.substr(1)};
  if (reader.Consume(':')) {
    const auto state = reader.Read();
    if (state.offset != selector.offset + selector.text.size() + 1) {
      Invalid(state, "expected the state immediately after the class name");
    }
    result.state = ParseKeyword(state, states);
  }
  return result;
}

struct SheetRule {
  std::string_view selector_text;
  std::vector<Selector> selectors;
  std::vector<RawDeclaration> declarations;
};

class StyleSheet {
public:
  StyleSheet(std::string name, std::string_view text) : name_(std::move(name)) {
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    SheetSource source{name_, text, {}};
    const SheetSource variable_source{name_, text, ":root"};
    ValueReader reader(Token{.text = text, .source = &source});
    SheetVariables variables;
    std::vector<SheetRule> parsed;
    for (;;) {
      source.selector = {};
      const auto start = reader.Remaining();
      if (start.text.empty()) break;
      if (reader.Consume(':')) {
        const auto name = reader.Read();
        if (!MatchesKeyword(name.text, "root") || name.offset != start.offset + 1) {
          Invalid(name, "expected :root");
        }
        source.selector = ":root";
        ReadBlock(reader, name, [&](ValueReader& body) {
          auto declaration = ReadRawDeclaration(body);
          declaration.value.source = &variable_source;
          variables.Set(declaration.name, declaration.value);
        });
        continue;
      }

      SheetRule rule;
      do {
        rule.selectors.push_back(ReadSelector(reader));
      } while (reader.Consume(','));
      rule.selector_text = text.substr(start.offset, reader.Remaining().offset - start.offset);
      while (!rule.selector_text.empty() && IsWhitespace(rule.selector_text.back())) {
        rule.selector_text.remove_suffix(1);
      }
      source.selector = rule.selector_text;
      ReadBlock(reader, start.Slice(0, rule.selector_text.size(), {}), [&](ValueReader& body) {
        rule.declarations.push_back(ReadRawDeclaration(body));
      });
      parsed.push_back(std::move(rule));
    }

    for (const auto& rule : parsed) {
      source.selector = rule.selector_text;
      CompileRule(rule, variables);
    }
  }

  void Apply(ModifierContext& context, const std::vector<std::string>& names) const {
    if (names.size() == 1) {
      ApplyRules(context, FindRules(names.front()));
      return;
    }
    std::vector<std::size_t> selected;
    for (const auto& name : names) {
      const auto& indices = FindRules(name);
      selected.insert(selected.end(), indices.begin(), indices.end());
    }
    std::ranges::sort(selected);
    const auto duplicates = std::ranges::unique(selected);
    selected.erase(duplicates.begin(), duplicates.end());
    ApplyRules(context, selected);
  }

private:
  void CompileRule(const SheetRule& rule, SheetVariables& variables) {
    const bool has_base = std::ranges::any_of(rule.selectors, [](const auto& selector) { return !selector.state; });
    const bool has_state = std::ranges::any_of(rule.selectors, [](const auto& selector) { return selector.state; });
    BaseRule base;
    std::optional<IndicationLayer> layer;
    for (const auto& declaration : rule.declarations) {
      auto value = declaration.value;
      const auto expanded = variables.Expand(value);
      if (expanded) {
        // Expanded values have no byte-for-byte source mapping. Report errors at the original declaration value.
        value.text = *expanded;
        value.fixed_offset = true;
      }
      ValueReader reader(value);
      const auto resolved = ReadDeclarationValues(reader, declaration.name);
      if (has_base) AddBaseProperty(resolved, base);
      if (has_state) AddStateProperty(layer, resolved);
    }
    if (has_base) {
      IndexRule(rule.selectors, nullptr);
      rules_.emplace_back(std::move(base));
    }
    for (const auto& entry : states) {
      if (IndexRule(rule.selectors, entry.second)) rules_.emplace_back(StateRule{entry.second, layer});
    }
  }

  bool IndexRule(const std::vector<Selector>& selectors, StateField state) {
    bool matched = false;
    for (const auto& selector : selectors) {
      if (selector.state != state) continue;
      auto& indices = classes_[std::string(selector.name)];
      if (indices.empty() || indices.back() != rules_.size()) indices.push_back(rules_.size());
      matched = true;
    }
    return matched;
  }

  const std::vector<std::size_t>& FindRules(const std::string& name) const {
    const auto found = classes_.find(name);
    if (found == classes_.end()) {
      throw std::invalid_argument("Lib-CSS unknown class `" + name + "` in " + name_);
    }
    return found->second;
  }

  void ApplyRules(ModifierContext& context, std::span<const std::size_t> selected) const {
    Frame frame;
    std::optional<float> opacity;
    const TransformList* transform = nullptr;
    Indication indication;
    for (const auto index : selected) {
      const auto& rule = rules_[index];
      if (const auto* state = std::get_if<StateRule>(&rule)) {
        MergeLayer(indication.*state->field, state->layer);
      } else {
        const auto& base = std::get<BaseRule>(rule);
        ApplyProperties(context, base.properties);
        for (const auto& dimension : dimensions) {
          const auto field = dimension.second;
          if (base.frame.*field) frame.*field = base.frame.*field;
        }
        if (base.opacity) opacity = base.opacity;
        if (base.transform) transform = &*base.transform;
      }
    }
    // The SDK validates frame dimensions on each Apply, so intermediate rule values must be merged first.
    ApplyMergedProperties(context, frame, opacity, transform, indication);
  }

  std::string name_;
  std::unordered_map<std::string, std::vector<std::size_t>> classes_;
  std::vector<Rule> rules_;
};

} // namespace

ApplicationHook InstallStyleSheet(RawResource resource) {
  return [resource = std::move(resource)](ApplicationContext& context) {
    const auto asset = UseService<Resources>()->GetRawResource(resource);
    context.Provide(std::make_shared<StyleSheet>(resource.ToString(), asset.ReadString()));
  };
}

Css::Css(std::string_view declarations) {
  auto impl = std::make_shared<Impl>();
  Token min_width;
  Token min_height;
  ValueReader reader(Token{declarations});
  while (!reader.Empty()) {
    if (reader.Consume(';')) continue;
    if (reader.Consume('&')) {
      if (!reader.Consume(':')) Invalid(reader.Read(), "expected ':' after '&'");
      const Token state = reader.Read();
      ReadStateBlock(reader, state, impl->indication.*ParseKeyword(state, states));
      continue;
    }
    const Declaration declaration = ReadDeclaration(reader);
    const FrameField field = AddBaseProperty(declaration, impl->base);
    if (field == &Frame::min_width) min_width = declaration.name;
    if (field == &Frame::min_height) min_height = declaration.name;
  }
  const auto& frame = impl->base.frame;
  ValidateBounds(frame.min_width, frame.max_width, min_width);
  ValidateBounds(frame.min_height, frame.max_height, min_height);
  impl_ = std::move(impl);
}

void Css::Configure(ModifierContext& context) const {
  if (!impl_) return;
  const auto& base = impl_->base;
  ApplyProperties(context, base.properties);
  ApplyMergedProperties(context, base.frame, base.opacity, base.transform ? &*base.transform : nullptr,
                        impl_->indication);
}

CssClass::CssClass(std::initializer_list<std::string_view> names) {
  auto impl = std::make_shared<Impl>();
  impl->names.reserve(names.size());
  for (const auto name : names) {
    if (!IsClassName(name)) {
      throw std::invalid_argument("Lib-CSS invalid class name `" + std::string(name) + "`");
    }
    impl->names.emplace_back(name);
  }
  std::ranges::sort(impl->names);
  const auto duplicates = std::ranges::unique(impl->names);
  impl->names.erase(duplicates.begin(), duplicates.end());
  impl_ = std::move(impl);
}

void CssClass::Configure(ModifierContext& context) const {
  if (!impl_) return;
  std::shared_ptr<StyleSheet> sheet;
  try {
    sheet = UseService<StyleSheet>();
  } catch (const std::logic_error&) {
    throw std::logic_error("Lib-CSS CssClass requires InstallStyleSheet() in application_hooks");
  }
  sheet->Apply(context, impl_->names);
}

} // namespace huxerui::css
