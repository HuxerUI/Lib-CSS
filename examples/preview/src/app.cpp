#include <huxerui/huxerui.h>
#include <huxerui/css.h>

#include <string_view>
#include <utility>

namespace {

using namespace huxerui;
using huxerui::css::Css;

const Css sample_style(R"(
  padding: 6px 14px;
  background: linear-gradient(to right, hsl(224 72% 41%) 0%, rgb(29 78 216) 100%);
  color: white;
  font-size: 16px;
  border: 1px solid hsl(224 62% 25%);
  border-radius: 6px 14px;
  box-shadow: 0 3px 7px rgba(15, 31, 66, .25);
  cursor: pointer;
)");

const Css alignment_style(R"(
  width: 264px;
  height: 60px;
  padding: 6px 10px;
  gap: 12px;
  justify-content: space-between;
  align-items: center;
  background-color: rgb(233 240 252);
  border-radius: 8px;
)");

const Css feedback_style(R"(
  padding: 8px 16px;
  background-color: #3730a3;
  color: white;
  border: 2px solid #3730a3;
  border-radius: 8px;
  cursor: pointer;
  &:hover { background-color: #4f46e5 }
  &:active { background-color: #27215e; border: 2px solid #27215e }
  &:focus-visible { border: 2px solid #a5b4fc }
)");

View Sample(std::string_view title, std::string_view declaration, View content) {
  return Column {
    Text(title).With(FontSize(16.0F), Foreground(Color::Rgb(25, 38, 66))),
    Text(declaration).With(FontSize(13.0F), Foreground(Color::Rgb(74, 88, 110))),
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
      Sample("CSS declarations", "Gradient, rounded corners, shadow and pointer cursor",
          Text("Hover here").With(sample_style)),
      Sample("Padding override", ".With(sample_style, Padding(18.0F))",
          Text("Preview").With(sample_style, Padding(18.0F))),
      Sample("Background override", ".With(sample_style, Background(...))",
          Text("Preview").With(sample_style, Background(Color::Rgb(4, 120, 87)))),
      Sample("Interaction styles", "Hover, press, or focus with Tab",
          Button("Try it").OnClick([] {}).With(feedback_style)),
      Sample(
          "Container alignment",
          "justify-content: space-between; align-items: center",
          Row {
            Text("First").With(sample_style),
            Text("Last").With(sample_style),
          }.With(alignment_style)
      ),
    }.With(Padding(20.0F), Spacing(12.0F), CrossAlign(CrossAxisAlignment::Stretch))),
  };
}

} // namespace

const huxerui::Application application{
    App,
    {.window = {.title = "CSS Preview"}, .show_debug_overlay = false},
};
