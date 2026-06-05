#pragma once

/**
 * @file ChildBigMap.h
 * @deprecated Replaced by ChildMapRenderer.h (Phase 5.11). Kept for history.
 * @brief Big map child process — renders full-screen map markers + trails
 *
 * Active only when MumbleLink::isMapOpen() is true.
 * Suspends rendering when map closes (idles, does not terminate).
 */

#include "ChildProcess.h"

class ChildBigMap : public ChildProcess {
  Q_OBJECT

public:
  ChildBigMap(const QString &profileId,
              const QString &mumbleName,
              qint64 gw2Pid,
              const QString &pipeName,
              const QString &profileName,
              QObject *parent = nullptr);
  ~ChildBigMap() override = default;

protected:
  bool onInitialize() override;
  void onShutdown() override;
  void onMapEntered(uint32_t mapId) override;
  void onMapLeft() override;
  void onFocusChanged(bool focused) override;
  void onSettingsReceived(const QJsonObject &settings) override;
};
