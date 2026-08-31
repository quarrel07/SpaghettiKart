#pragma once

#include <libultraship/libultraship.h>

namespace GeometryEditor {
class GeometryEditorWindow : public Ship::GuiWindow {
  public:
    using Ship::GuiWindow::GuiWindow;

    ~GeometryEditorWindow() override = default;

  protected:
    void InitElement() override {};
    void DrawElement() override;
    void UpdateElement() override {};
};

class SceneryWindow : public Ship::GuiWindow {
  public:
    using Ship::GuiWindow::GuiWindow;
    ~SceneryWindow() override = default;

  protected:
    void InitElement() override {};
    void DrawElement() override;
    void UpdateElement() override {};
};
} // namespace GeometryEditor
