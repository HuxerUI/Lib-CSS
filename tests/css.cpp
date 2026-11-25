#include <huxerui/css.h>
#include <huxerui/huxerui.h>
#include <huxerui/testing/ui_test.h>

#include <css_fixture_resources.h>

#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace huxerui;
using huxerui::css::Css;
using huxerui::css::CssClass;
using huxerui::css::InstallStyleSheet;
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

UiTestOptions TestOptions() {
  return {.viewport = {320.0F, 240.0F}};
}

void CheckSnapshot(const UiTest& actual, const UiTest& expected) {
  const auto snapshot = actual.CaptureSnapshot();
  const auto reference = expected.CaptureSnapshot();
  if (snapshot != reference) throw std::runtime_error(snapshot.Diff(reference));
}

void CheckSnapshot(const UiTest& actual, RootFactory native) {
  const Application reference_app(native, {.show_debug_overlay = false});
  UiTest expected(reference_app, TestOptions());
  CheckSnapshot(actual, expected);
}

void CheckEquivalent(RootFactory root, RootFactory native) {
  const Application app(root, {.show_debug_overlay = false});
  UiTest actual(app, TestOptions());
  CheckSnapshot(actual, native);
}

void CheckBar(RootFactory root, RootFactory native, Size size, Point inset) {
  const Application actual_app(root, {.show_debug_overlay = false});
  UiTest actual(actual_app, TestOptions());
  const auto box = actual.Find(UiSelector::Key("bar")).One().bounds;
  const auto marker = actual.Find(UiSelector::Key("marker")).One().bounds;
  Check(Size{box.width, box.height} == size, "Unexpected CSS box size");
  Check(Point{marker.x - box.x, marker.y - box.y} == inset, "Unexpected content inset");

  CheckSnapshot(actual, native);
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
  UiTest actual(css, TestOptions());
  CheckSnapshot(actual, NativeDetails);

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
  UiTest test(app, TestOptions());
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

View Tile() {
  return Canvas([](PaintContext&, Size) {}).With(Frame{.width = 80.0F, .height = 27.0F});
}

View CssPaint() {
  return Column {
    Tile().With(Css(R"(
      background: RGB(100% /* red */ 128 0 / 25%);
      border: 2px solid hsl(120deg 100% 50%);
      border-radius: 3px 7px;
      box-shadow: -2px 4px 8px -1px rgba(0, 0, 0, .25);
    )")),
    Tile().With(Css(R"(
      background-color: hsla(-60deg, 100%, 50%, 50%);
      border-radius: 2px 4px 6px;
      box-shadow: rgb(0 0 0 / .4) 1px -2px;
    )")),
    Tile().With(
        CornerRadius(1.0F),
        Border{.color = Color::Black()},
        Shadow{.color = Color::Black(), .blur_radius = 2.0F},
        Css("background: hsl(.5turn 100% 50%); border-radius: 1px 2px 3px 4px; border: none; box-shadow: none")
    ),
    Tile().With(
        Css("background: rgba(100%, 0%, 100%, .75); border-radius: 4px 8px; box-shadow: 1px 2px black"),
        CornerRadius(6.0F),
        Shadow{.color = Color::Transparent()}
    ),
    Text("Foreground").With(Css("color: rgb(-10 300 128 / 140%)")),
  }.With(Spacing(10.0F));
}

View NativePaint() {
  return Column {
    Tile().With(
        Background(Color::Rgb(255, 128, 0, 0.25F)),
        Border{.color = Color::Rgb(0, 255, 0), .width = 2.0F},
        CornerRadius(3.0F, 7.0F, 3.0F, 7.0F),
        Shadow{.color = Color::Rgb(0, 0, 0, 0.25F), .offset = {-2.0F, 4.0F}, .blur_radius = 8.0F, .spread = -1.0F}
    ),
    Tile().With(
        Background(Color::Rgb(255, 0, 255, 0.5F)),
        CornerRadius(2.0F, 4.0F, 6.0F, 4.0F),
        Shadow{.color = Color::Rgb(0, 0, 0, 0.4F), .offset = {1.0F, -2.0F}}
    ),
    Tile().With(Background(Color::Rgb(0, 255, 255)), CornerRadius(1.0F, 2.0F, 3.0F, 4.0F)),
    Tile().With(Background(Color::Rgb(255, 0, 255, 0.75F)), CornerRadius(6.0F)),
    Text("Foreground").With(Foreground(Color::Rgb(0, 255, 128))),
  }.With(Spacing(10.0F));
}

View CssGradients() {
  return Column {
    Tile().With(Css("background-color: black; background: linear-gradient(to right, white, rgba(0, 0, 0, .5), black)")),
    Tile().With(Css("background: linear-gradient(to left, white 70%, black 20%, white)")),
    Tile().With(Css("background: linear-gradient(to top, white 10%, black 90%)")),
    Tile().With(Css("background: linear-gradient(to bottom, white, black)")),
    Tile().With(Background(blue), Css("background: linear-gradient(white, black)")),
    Tile().With(Css("background: linear-gradient(white, black); background-color: white"), Background(green)),
    Tile().With(Css("background: linear-gradient(white, black); background-color: black")),
  };
}

View NativeGradients() {
  const auto white = Color::White();
  const auto black = Color::Black();
  const LinearGradient vertical{
      .start = {0.5F, 0.0F}, .end = {0.5F, 1.0F}, .stops = {{0.0F, white}, {1.0F, black}},
  };
  return Column {
    Tile().With(Background(LinearGradient{
        .stops = {{0.0F, white}, {0.5F, Color::Rgb(0, 0, 0, 0.5F)}, {1.0F, black}},
    })),
    Tile().With(Background(LinearGradient{
        .start = {1.0F, 0.5F}, .end = {0.0F, 0.5F}, .stops = {{0.7F, white}, {0.7F, black}, {1.0F, white}},
    })),
    Tile().With(Background(LinearGradient{
        .start = {0.5F, 1.0F}, .end = {0.5F, 0.0F}, .stops = {{0.1F, white}, {0.9F, black}},
    })),
    Tile().With(Background(vertical)),
    Tile().With(Background(vertical)),
    Tile().With(Background(green)),
    Tile().With(Background(black)),
  };
}

Row AlignmentRow() {
  return Row {
    Marker("first"),
    Marker("second"),
  }.With(Frame{.width = 120.0F, .height = 30.0F});
}

View CssAlignment() {
  return Column {
    AlignmentRow().With(
        MainAlign(MainAxisAlignment::Center),
        CrossAlign(CrossAxisAlignment::End),
        Css("justify-content: space-between; align-items: center")
    ).Key("css"),
    AlignmentRow().With(
        Css("justify-content: space-between; align-items: center"),
        MainAlign(MainAxisAlignment::Center),
        CrossAlign(CrossAxisAlignment::End)
    ).Key("overrides"),
  }.With(CrossAlign(CrossAxisAlignment::Start));
}

View NativeAlignment() {
  return Column {
    AlignmentRow().With(MainAlign(MainAxisAlignment::SpaceBetween), CrossAlign(CrossAxisAlignment::Center)).Key("css"),
    AlignmentRow().With(MainAlign(MainAxisAlignment::Center), CrossAlign(CrossAxisAlignment::End)).Key("overrides"),
  }.With(CrossAlign(CrossAxisAlignment::Start));
}

void CheckAlignment() {
  const Application app(CssAlignment, {.show_debug_overlay = false});
  UiTest actual(app, TestOptions());
  CheckSnapshot(actual, NativeAlignment);

  const auto check = [&](std::string_view key, Point first, Point second) {
    const auto row = actual.Find(UiSelector::Key(key));
    const auto row_bounds = row.One().bounds;
    const auto a = row.Find(UiSelector::Key("first")).One().bounds;
    const auto b = row.Find(UiSelector::Key("second")).One().bounds;
    Check(Point{a.x - row_bounds.x, a.y - row_bounds.y} == first &&
        Point{b.x - row_bounds.x, b.y - row_bounds.y} == second, key);
  };
  check("css", {0.0F, 10.0F}, {104.0F, 10.0F});
  check("overrides", {44.0F, 20.0F}, {60.0F, 20.0F});
}

View CursorContent() {
  const auto cell = [] { return Tile().With(Frame{.width = 32.0F}); };
  return Row {
    cell().With(PointerCursor(PointerCursorKind::Text), Css("cursor: pointer")),
    cell().With(Css("cursor: pointer"), PointerCursor(PointerCursorKind::Text)),
    cell().With(Css("cursor: text; cursor: grab")),
    cell().With(PointerCursor(PointerCursorKind::Hand), Css("cursor: default")),
  };
}

class CursorHost final : public Runtime, public UiWindow {
public:
  CursorHost() : Runtime([](auto) {}) {}

  ~CursorHost() override {
    UiWindow::Retire();
    Runtime::Retire();
  }

  void Open(const Application& app) {
    InitializeApplication(app);
    InitializeWindow(*this, app.root_factory, app.options.window, std::nullopt, WindowLifecycleState::Active);
    SetWindowMetrics({.viewport = {240.0F, 80.0F}});
    BuildFrame();
  }

  PointerCursorKind CursorAt(Point position) {
    HandlePointerEvent({.type = PointerEventType::Move, .position = position, .device_kind = PointerDeviceKind::Mouse});
    return cursor_;
  }

  void RequestFrameAt(double) override {}
  FontMetrics Metrics(const Font&) override { return {}; }
  TextRunMetrics MeasureRun(std::string_view, const TextStyle&, const TextShapingOptions&) override { return {}; }
  TextLayoutMetrics MeasureText(const AttributedText&, const TextStyle&, float, const TextLayoutOptions&) override {
    return {};
  }

private:
  void OnRuntimeStopped() override {}

  void SetPointerCursor(PointerCursorKind kind) override {
    cursor_ = kind;
  }

  PointerCursorKind cursor_ = PointerCursorKind::Default;
};

void CheckCursor() {
  const Application app(CursorContent, {.show_debug_overlay = false});
  CursorHost host;
  host.Open(app);
  Check(host.CursorAt({16.0F, 10.0F}) == PointerCursorKind::Hand, "CSS should override the prior cursor");
  Check(host.CursorAt({48.0F, 10.0F}) == PointerCursorKind::Text, "Native cursor should override CSS");
  Check(host.CursorAt({80.0F, 10.0F}) == PointerCursorKind::Grab, "Last CSS cursor declaration should win");
  Check(host.CursorAt({112.0F, 10.0F}) == PointerCursorKind::Default, "CSS default should reset the cursor");
}

View FeedbackPage(View button) {
  auto theme_spec = ThemeSpec::Default();
  theme_spec.interactions.indication = Indication{
      .hover = IndicationLayer{.foreground = marker_color},
      .ripple = RippleEffect{.color = marker_color, .expansion = SnapSpec{}},
  };
  auto style = ButtonStyle::Default();
  style.indication = theme_spec.interactions.indication;
  ThemeDefinition theme(theme_spec);
  theme.Set(style);
  return Theme {std::move(theme), TestPage(std::move(button).With(Focusable()).Key("feedback"))};
}

View CssFeedback(View control, bool click_first) {
  static const Css style(R"(
    background-color: black;
    border: 1px solid white;
    &:hover { background-color: black; color: #c1dfed }
    &:active { background: #059669; color: black; border: none }
    &:focus-visible { border: 2px solid white; border-radius: 2px 4px }
    &:hover { background-color: #2563eb }
    color: white;
  )");
  if (click_first) return FeedbackPage(std::move(control).OnClick([] {}).With(style));
  return FeedbackPage(std::move(control).With(style).OnClick([] {}));
}

View NativeFeedback(View control) {
  return FeedbackPage(std::move(control).OnClick([] {}).With(
      Background(Color::Black()),
      Border{.color = Color::White(), .width = 1.0F},
      Foreground(Color::White()),
      Indication{
          .focus = IndicationLayer{.border = Border{.color = Color::White(), .width = 2.0F},
                                   .corner_radii = CornerRadii{2.0F, 4.0F, 2.0F, 4.0F},
                                   .placement = IndicationPlacement::BehindContent,
                                   .enter = SnapSpec{}, .exit = SnapSpec{}},
          .hover = IndicationLayer{.fill = blue, .placement = IndicationPlacement::BehindContent,
                                   .enter = SnapSpec{}, .exit = SnapSpec{}, .foreground = Color::Rgb(193, 223, 237)},
          .press = IndicationLayer{.fill = green, .border = Border{.color = Color::Transparent(), .width = 0.0F},
                                   .placement = IndicationPlacement::BehindContent,
                                   .enter = SnapSpec{}, .exit = SnapSpec{}, .foreground = Color::Black()},
      }
  ));
}

void CheckFeedback(const Application& css, RootFactory reference, UiTestOptions options = TestOptions()) {
  const Application native(reference, {.show_debug_overlay = false});
  UiTest actual(css, options);
  UiTest expected(native, TestOptions());
  CheckSnapshot(actual, expected);

  const auto target = actual.Find(UiSelector::Key("feedback"));
  const auto bounds = target.One().bounds;
  const auto check = [&](auto input) {
    input(actual);
    input(expected);
    CheckSnapshot(actual, expected);
    Check(target.One().bounds == bounds, "Interaction styles should preserve layout");
  };
  check([](UiTest& ui) { ui.PressKey(Key::Tab); });

  PointerEvent mouse{.position = {bounds.x + bounds.width / 2.0F, bounds.y + bounds.height / 2.0F}};
  const auto send = [&](UiTest& ui) {
    ui.SendPointer(mouse);
    ui.Pump();
  };
  check(send);
  mouse.type = PointerEventType::Down;
  mouse.changed_button = PointerButton::Primary;
  mouse.pressed_buttons = PointerButton::Primary;
  check(send);
  mouse.type = PointerEventType::Up;
  mouse.pressed_buttons = PointerButton::None;
  check(send);
  mouse = {.position = {200.0F, 200.0F}};
  check(send);
}

class StyleResources final : public PlatformResources {
public:
  ResourceConfiguration Configuration() const override { return {}; }

  std::optional<InputStream> OpenRead(std::string_view path) override {
    if (path == "huxerui/css_fixture/raw/styles.css") ++sheet_reads;
    auto stream = File(std::string(LIB_CSS_FIXTURE_PACKAGE) + "/" + std::string(path)).OpenRead();
    if (!stream.Succeeded()) return std::nullopt;
    return std::move(stream).Value();
  }

  int sheet_reads = 0;
};

void CheckStyleSheet() {
  const auto provider = std::make_shared<StyleResources>();
  auto options = TestOptions();
  options.resource_provider = provider;
  const AppOptions style_options{
      .show_debug_overlay = false,
      .application_hooks = {InstallStyleSheet(css_fixture::raw::styles_css)},
  };
  const Application cascade([]() -> View {
    return Column {
      Bar().With(CssClass("base", "accent", "base")).Key("first"),
      Bar().With(CssClass("accent", "base")).Key("second"),
      Bar().With(CssClass("base", "accent"), Padding(11.0F)).Key("override"),
      Bar().With(CssClass(std::string("single"))).Key("single"),
    }.With(CrossAlign(CrossAxisAlignment::Start));
  }, style_options);
  UiTest actual(cascade, options);
  const auto check = [&](std::string_view key, Size size, float padding) {
    const auto bar = actual.Find(UiSelector::Key(key));
    const auto bounds = bar.One().bounds;
    const auto marker = bar.Find(UiSelector::Key("marker")).One().bounds;
    Check(Size{bounds.width, bounds.height} == size, "Unexpected class frame");
    Check(Point{marker.x - bounds.x, marker.y - bounds.y} == Point{padding, padding}, "Unexpected class padding");
  };
  check("first", {100.0F, 24.0F}, 7.0F);
  check("second", {100.0F, 24.0F}, 7.0F);
  check("override", {100.0F, 32.0F}, 11.0F);
  check("single", {80.0F, 20.0F}, 5.0F);
  actual.Pump();
  Check(provider->sheet_reads == 1, "Style sheet should load once for each application");

  const Application feedback([] {
    return FeedbackPage(Button("Action").OnClick([] {}).With(CssClass("primary", "action")));
  }, style_options);
  CheckFeedback(feedback, [] { return NativeFeedback(Button("Action")); }, options);
  Check(provider->sheet_reads == 2, "Independent applications should each load their style sheet");
}

template <class Error> void ExpectError(auto&& run, std::string_view diagnostic) {
  try {
    run();
  } catch (const Error& error) {
    const std::string_view message(error.what());
    Check(message.find(diagnostic) != std::string_view::npos, message);
    return;
  }
  throw std::runtime_error("Expected CSS error: " + std::string(diagnostic));
}

void CheckStyleSheetErrors() {
  auto options = TestOptions();
  options.resource_provider = std::make_shared<StyleResources>();
  const auto root = [] { return Text("Class lookup").With(CssClass("unknown")); };
  const Application missing(root, {.show_debug_overlay = false});
  ExpectError<std::logic_error>([&] { const UiTest test(missing, options); }, "requires InstallStyleSheet()");

  const Application unknown(root, {
      .show_debug_overlay = false,
      .application_hooks = {InstallStyleSheet(css_fixture::raw::styles_css)},
  });
  ExpectError<std::invalid_argument>([&] { const UiTest test(unknown, options); }, "unknown class `unknown`");

  const Application invalid(root, {
      .show_debug_overlay = false,
      .application_hooks = {InstallStyleSheet(css_fixture::raw::invalid_css)},
  });
  ExpectError<std::invalid_argument>(
      [&] { const UiTest test(invalid, options); }, "css_fixture:raw/invalid.css:2:10 in rule `.button:hover`");
  ExpectError<std::invalid_argument>([] { const CssClass rejected(".button"); }, "invalid class name");
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
    CheckEquivalent(CssPaint, NativePaint);
    CheckEquivalent(CssGradients, NativeGradients);
    CheckAlignment();
    CheckCursor();
    const AppOptions plain{.show_debug_overlay = false};
    CheckFeedback(
        Application([] { return CssFeedback(Button("Action"), true); }, plain),
        [] { return NativeFeedback(Button("Action")); }
    );
    CheckFeedback(
        Application([] { return CssFeedback(Text("Action"), false); }, plain),
        [] { return NativeFeedback(Text("Action")); }
    );
    CheckStyleSheet();
    CheckStyleSheetErrors();

    ExpectInvalid("padding -2px", "expected ':'");
    ExpectInvalid("padding:", "expected a value");
    ExpectInvalid("padding: 2px -3px", "byte 13 in `padding`");
    ExpectInvalid("padding: 1px 2px 3px 4px 5px", "too many values");
    ExpectInvalid("background-color: #00g", "invalid hexadecimal");
    ExpectInvalid("font-size: 0", "font size must be positive");
    ExpectInvalid("width: 50%", "expected a non-negative px length");
    ExpectInvalid("min-width: 60px; max-width: 40px", "minimum size exceeds maximum size");
    ExpectInvalid("border: 1px dashed black", "expected '<width> solid <color>'");
    ExpectInvalid("color: rgb(255, 50%, 0)", "must use the same unit");
    ExpectInvalid("color: rgb(255 0 0, .5)", "unexpected color component");
    ExpectInvalid("color: rgba(0,0,0,", "unterminated function");
    ExpectInvalid("color: hsl(20 80 50%)", "expected a percentage");
    ExpectInvalid("border-radius: 1px 2px 3px 4px 5px", "too many values");
    ExpectInvalid("box-shadow: 1px 2px -3px black", "non-negative px length");
    ExpectInvalid("box-shadow: 1px 2px black, 2px 3px white", "single value or layer");
    ExpectInvalid("box-shadow: inset 1px 2px black", "supported CSS color");
    ExpectInvalid("background: linear-gradient(to top right, black, white)", "gradient direction");
    ExpectInvalid("background: linear-gradient(black)", "at least two color stops");
    ExpectInvalid("background: linear-gradient(black -10%, white)", "between 0% and 100%");
    ExpectInvalid("align-items: baseline", "unsupported keyword");
    ExpectInvalid("justify-content: stretch", "unsupported keyword");
    ExpectInvalid("cursor: auto", "unsupported keyword");
    ExpectInvalid("opacity: 0.5", "unsupported CSS property");
    ExpectInvalid("padding: 2px; /*", "unterminated comment");
    ExpectInvalid("&:visited { color: white }", "unsupported keyword");
    ExpectInvalid("&:hover color: white", "expected '{'");
    ExpectInvalid("&:hover { color: white", "unterminated CSS block");
    ExpectInvalid("&:active { padding: 1px }", "unsupported property in an interaction state block");
    ExpectInvalid("&:focus-visible { color: }", "expected a value");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
