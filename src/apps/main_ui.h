#pragma once

namespace ne {

class BasicApp;

class MainUI {
public:
  void draw(BasicApp& iApp);

  bool isVisible() const { return mVisible; }
  void setVisible(bool iVisible) { mVisible = iVisible; }
  void toggleVisible() { mVisible = !mVisible; }

private:
  void drawMainMenuBar(BasicApp& iApp);
  void drawDiagnostics(BasicApp& iApp);
  void drawCameraSettings(BasicApp& iApp);
  void drawLighting(BasicApp& iApp);
  void drawAboutModal();
  void resetCamera(BasicApp& iApp);

  bool mVisible{true};
  bool mShowDiagnostics{true};
  bool mShowCameraSettings{false};
  bool mShowLighting{false};
  bool mShowDemoWindow{false};
  bool mShowAboutModal{false};
};

} // namespace ne
