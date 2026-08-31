#pragma once

#include <libultraship/libultraship.h>

namespace Freecam {

// Compact floating window with the freecam view options, so render modes can
// be compared without the full-screen menu dimming the scene.
class FreecamOptionsWindow : public Ship::GuiWindow {
  public:
    using Ship::GuiWindow::GuiWindow;
    ~FreecamOptionsWindow() override = default;

  protected:
    void InitElement() override {};
    void DrawElement() override;
    void UpdateElement() override {};
};

} // namespace Freecam
