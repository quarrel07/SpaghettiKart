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

// Gate Tour: checklist of the 19 uncovered vanilla-gated pieces, with a
// live IN-VIEW highlight from each piece's data-file bounding box and a
// spotted checkbox persisted per piece.
class GateTourWindow : public Ship::GuiWindow {
  public:
    using Ship::GuiWindow::GuiWindow;
    ~GateTourWindow() override = default;

  protected:
    void InitElement() override {};
    void DrawElement() override;
    void UpdateElement() override {};
};
} // namespace GeometryEditor
