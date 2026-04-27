#include "ChildRadial.h"

ChildRadial::ChildRadial(const QString &profileId,
                         const QString &mumbleName,
                         qint64 gw2Pid,
                         const QString &pipeName,
                         const QString &profileName,
                         QObject *parent)
    : ChildProcess(profileId, mumbleName, gw2Pid, pipeName, profileName, parent)
{
}

bool ChildRadial::onInitialize()
{
    qInfo() << "ChildRadial: Initializing for" << profileName();
    // TODO Phase 8b: create RadialController
    qInfo() << "ChildRadial: Stub — no radial menu yet";
    return true;
}

void ChildRadial::onShutdown()
{
    qInfo() << "ChildRadial: Shutting down for" << profileName();
}

void ChildRadial::onMapEntered(uint32_t mapId)
{
    Q_UNUSED(mapId);
    // Radial menu doesn't care about map changes
}

void ChildRadial::onMapLeft()
{
    // Radial menu doesn't care about map changes
}

void ChildRadial::onFocusChanged(bool focused)
{
    qInfo() << "ChildRadial: Focus" << (focused ? "gained" : "lost")
            << "for" << profileName();
}

void ChildRadial::onSettingsReceived(const QJsonObject &settings)
{
    Q_UNUSED(settings);
    qInfo() << "ChildRadial: Settings received (stub)";
}
