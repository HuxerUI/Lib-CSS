#include <huxerui/css.h>
#include <huxerui/huxerui.h>
#include <huxerui/testing/ui_test.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace huxerui;
using huxerui::css::Css;
using huxerui::testing::UiSelector;
using huxerui::testing::UiTest;
using huxerui::testing::UiTestOptions;

constexpr auto blue = Color::Rgb(37, 99, 235);
constexpr auto green = Color::Rgb(5, 150, 105);
constexpr auto marker_color = Color::Rgb(255, 205, 45);
constexpr auto bar_style =
    "width:120px; padding:4px 12px; background-color:#2563eb; border-radius:6px";

void Check(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

View Marker(std::string_view key) {
  return Canvas([](PaintContext& paint, Size size) {
    paint.DrawRect({0.0F, 0.0F, size.width, size.height}, marker_color);
  }).With(Frame{.width = 16.0F, .height = 10.0F}).Key(key);
}

Row Bar() {
  return Row {
    Marker("marker"),
  }.With(MainAlign(MainAxisAlignment::Start), CrossAlign(CrossAxisAlignment::Start));
}

View TestPage(View content) {
  return Column {
    std::move(content),
  }.With(CrossAlign(CrossAxisAlignment::Start));
}

View CssOnly() {
  return TestPage(Bar().With(Css(bar_style)).Key("bar"));
}

View CssThenPadding() {
  return TestPage(Bar().With(Css(bar_style), Padding(18.0F)).Key("bar"));
}

View PaddingThenCss() {
  return TestPage(Bar().With(Padding(18.0F), Css(bar_style)).Key("bar"));
}

View CssThenBackground() {
  return TestPage(Bar().With(Css(bar_style), Background(green)).Key("bar"));
}

View NativeBar(float vertical, float horizontal, Color background) {
  return TestPage(Bar().With(
      Frame{.width = 120.0F},
      Padding(EdgeInsets::Symmetric(horizontal, vertical)),
      Background(background),
      CornerRadius(6.0F)
  ).Key("bar"));
}

void CheckBar(RootFactory root, RootFactory native, Size size, Point inset) {
  const Application actual_app(root, {.show_debug_overlay = false});
  const Application expected_app(native, {.show_debug_overlay = false});
  const UiTestOptions options{.viewport = {320.0F, 240.0F}};
  UiTest actual(actual_app, options);
  const auto box = actual.Find(UiSelector::Key("bar")).One().bounds;
  const auto marker = actual.Find(UiSelector::Key("marker")).One().bounds;
  Check(Size{box.width, box.height} == size, "Unexpected CSS box size");
  Check(Point{marker.x - box.x, marker.y - box.y} == inset, "Unexpected content inset");

  const auto snapshot = actual.CaptureSnapshot();
  UiTest expected(expected_app, options);
  const auto reference = expected.CaptureSnapshot();
  Check(snapshot == reference, snapshot.Diff(reference));
}

View CssDetails() {
  return TestPage(Column {
    Text("CSS paint").With(Css("Color: #FfFa; font-size: 19PX")),
    Row {
      Marker("first"),
      Marker("second"),
    }.With(Css("gap: 7px")),
  }.With(Css(R"(
    /* Intermediate dimensions are replaced by later declarations. */
    width: 160px; width: 100px;
    min-width: 200px; max-width: 180px; min-width: 120px;
    height: 80px; min-height: 90px; max-height: 110px;
    padding: 9px; padding: 1px 2px 3px 4px;
    background-color: transparent; background-color: #1684;
    border: 2px solid BLACK; border-radius: 0;
  )")).Key("details"));
}

View NativeDetails() {
  return TestPage(Column {
    Text("CSS paint").With(Foreground(Color::Rgba32(0xffffffaa)), FontSize(19.0F)),
    Row {
      Marker("first"),
      Marker("second"),
    }.With(Spacing(7.0F)),
  }.With(
      Frame{.width = 100.0F, .height = 80.0F, .min_width = 120.0F, .max_width = 180.0F,
            .min_height = 90.0F, .max_height = 110.0F},
      Padding(EdgeInsets{1.0F, 2.0F, 3.0F, 4.0F}),
      Background(Color::Rgba32(0x11668844)),
      Border{.color = Color::Black(), .width = 2.0F},
      CornerRadius(0.0F)
  ).Key("details"));
}

void CheckDetails() {
  const Application css(CssDetails, {.show_debug_overlay = false});
  const Application native(NativeDetails, {.show_debug_overlay = false});
  UiTest actual(css, {.viewport = {320.0F, 240.0F}});
  UiTest expected(native, {.viewport = {320.0F, 240.0F}});
  const auto snapshot = actual.CaptureSnapshot();
  const auto reference = expected.CaptureSnapshot();
  Check(snapshot == reference, snapshot.Diff(reference));

  const auto details = actual.Find(UiSelector::Key("details")).One().bounds;
  Check(Size{details.width, details.height} == Size{120.0F, 90.0F}, "Unexpected CSS min/max frame");
  const auto first = actual.Find(UiSelector::Key("first")).One().bounds;
  const auto second = actual.Find(UiSelector::Key("second")).One().bounds;
  Check(second.x - first.x - first.width == 7.0F, "Unexpected CSS gap");
}

View CssPaddingForms() {
  const Css reusable = [] {
    const std::string declarations = "padding: 5px;";
    return Css(declarations);
  }();
  return Column {
    Bar().With(reusable).Key("one"),
    Bar().With(Css(" /* before */ PADDING /* name */ : 2px /* value */ 7px;;")).Key("two"),
    Bar().With(Css("padding: 2px 7px 3px;")).Key("three"),
    Bar().With(Css("padding: 2px 7px 3px 9px;")).Key("four"),
    Bar().With(reusable, Css(" /* empty */ ")).Key("reused"),
  }.With(CrossAlign(CrossAxisAlignment::Start));
}

void CheckPaddingForms() {
  const Application app(CssPaddingForms, {.show_debug_overlay = false});
  UiTest test(app, {.viewport = {320.0F, 240.0F}});
  const auto check = [&](std::string_view key, Size size, Point inset) {
    const auto bar = test.Find(UiSelector::Key(key));
    const auto box = bar.One().bounds;
    const auto marker = bar.Find(UiSelector::Key("marker")).One().bounds;
    Check(Size{box.width, box.height} == size && Point{marker.x - box.x, marker.y - box.y} == inset, key);
  };
  check("one", {26.0F, 20.0F}, {5.0F, 5.0F});
  check("two", {30.0F, 14.0F}, {7.0F, 2.0F});
  check("three", {30.0F, 15.0F}, {7.0F, 2.0F});
  check("four", {32.0F, 15.0F}, {9.0F, 2.0F});
  check("reused", {26.0F, 20.0F}, {5.0F, 5.0F});
}

void ExpectInvalid(std::string_view declarations, std::string_view diagnostic) {
  try {
    const Css rejected(declarations);
  } catch (const std::invalid_argument& error) {
    const std::string_view message(error.what());
    Check(message.starts_with("Lib-CSS at byte ") && message.find(diagnostic) != std::string_view::npos, message);
    return;
  }
  throw std::runtime_error("Accepted invalid CSS: " + std::string(declarations));
}

} // namespace

int main() {
  try {
    const auto native_blue = [] { return NativeBar(4.0F, 12.0F, blue); };
    CheckBar(CssOnly, native_blue, {120.0F, 18.0F}, {12.0F, 4.0F});
    CheckBar(CssThenPadding, [] { return NativeBar(18.0F, 18.0F, blue); }, {120.0F, 46.0F}, {18.0F, 18.0F});
    CheckBar(PaddingThenCss, native_blue, {120.0F, 18.0F}, {12.0F, 4.0F});
    CheckBar(CssThenBackground, [] { return NativeBar(4.0F, 12.0F, green); }, {120.0F, 18.0F}, {12.0F, 4.0F});
    CheckDetails();
    CheckPaddingForms();

    ExpectInvalid("padding -2px", "expected ':'");
    ExpectInvalid("padding:", "expected a value");
    ExpectInvalid("padding: 2px -3px", "byte 13 in `padding`");
    ExpectInvalid("padding: 1px 2px 3px 4px 5px", "too many values");
    ExpectInvalid("background-color: #00g", "invalid hexadecimal");
    ExpectInvalid("font-size: 0", "font size must be positive");
    ExpectInvalid("width: 50%", "expected a non-negative px length");
    ExpectInvalid("min-width: 60px; max-width: 40px", "minimum size exceeds maximum size");
    ExpectInvalid("border: 1px dashed black", "expected '<width> solid <color>'");
    ExpectInvalid("opacity: 0.5", "unsupported CSS property");
    ExpectInvalid("padding: 2px; /*", "unterminated comment");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
