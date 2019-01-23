#pragma once

#include "widgets/widget.hpp"

#include <switch.h>

class WidgetComment : public Widget {
public:
  WidgetComment(Interpreter *saveParser, std::string text);

  ~WidgetComment();

  void draw(Gui *gui, u16 x, u16 y);

  void onInput(u32 kdown);
  void onTouch(touchPosition &touch);

private:
  std::string m_strOnValue, m_text;

};
