#pragma once

/**
 * @file ChildRadial.h
 * @brief Radial menu child process
 *
 * Lightest child — Qt widgets only, no D3D11.
 * Owns: RadialController (future).
 */

#include "ChildProcess.h"

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
};
