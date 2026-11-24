#include <huxerui/huxerui.h>
#include <huxerui/css.h>

#include <string_view>
#include <utility>

namespace {

using namespace huxerui;
using huxerui::css::Css;

const Css sample_style(R"(
  padding: 6px 14px;
  background-color: #1d4ed8;
  color: white;
  font-size: 16px;
  border: 1px solid #122b60;
  border-radius: 8px;
)");

View Sample(std::string_view title, std::string_view declaration, View content) {
  return Column {
    Text(std::string(title)).With(FontSize(16.0F), Foreground(Color::Rgb(25, 38, 66))),
    Text(std::string(declaration)).With(FontSize(13.0F), Foreground(Color::Rgb(74, 88, 110))),
    std::move(content),
  }.With(
      Padding(16.0F),
      Spacing(10.0F),
      CrossAlign(CrossAxisAlignment::Start),
      Background(Color::White()),
      Border{.color = Color::Rgb(220, 228, 239)},
      CornerRadius(12.0F)
  );
}

View App() {
  return FlatTheme {
    ScrollView(Column {
      Text("CSS modifier").With(FontSize(26.0F), Foreground(Color::Rgb(25, 38, 66))),
      Sample("CSS declarations", ".With(sample_style)", Text("Preview").With(sample_style)),
      Sample("Padding override", ".With(sample_style, Padding(18.0F))",
          Text("Preview").With(sample_style, Padding(18.0F))),
      Sample("Background override", ".With(sample_style, Background(...))",
          Text("Preview").With(sample_style, Background(Color::Rgb(4, 120, 87)))),
    }.With(Padding(20.0F), Spacing(12.0F), CrossAlign(CrossAxisAlignment::Stretch))),
  };
}

} // namespace

const huxerui::Application application{
    App,
    {.window = {.title = "CSS Preview"}, .show_debug_overlay = false},
};
