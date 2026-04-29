#pragma once

/**
 * @file ChildRadial.h
 * @brief Radial menu child process — D3D11 radial wheel rendering
 *
 * Owns: RadialController (which owns RadialOverlayWindow).
 * Receives per-profile radial settings via IPC and renders the
 * radial wheel overlay on the GW2 window.
 */

#include "ChildProcess.h"

class RadialController;
class RadialSettingsManager;

class ChildRadial : public ChildProcess {
  Q_OBJECT

public:
  ChildRadial(const QString &profileId,
              const QString &mumbleName,
              qint64 gw2Pid,
              const QString &pipeName,
              const QString &profileName,
              QObject *parent = nullptr);
  ~ChildRadial() override = default;

protected:
  bool onInitialize() override;
  void onShutdown() override;
  void onMapEntered(uint32_t mapId) override;
  void onMapLeft() override;
  void onFocusChanged(bool focused) override;
  void onSettingsReceived(const QJsonObject &settings) override;

private:
  RadialController *m_controller = nullptr;
  RadialSettingsManager *m_radialSettings = nullptr;
};
