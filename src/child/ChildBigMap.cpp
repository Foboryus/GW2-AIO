#include "ChildBigMap.h"

ChildBigMap::ChildBigMap(const QString &profileId,
                         const QString &mumbleName,
                         qint64 gw2Pid,
                         const QString &pipeName,
                         const QString &profileName,
                         QObject *parent)
    : ChildProcess(profileId, mumbleName, gw2Pid, pipeName, profileName, parent)
{
}

bool ChildBigMap::onInitialize()
{
    qInfo() << "ChildBigMap: Initializing for" << profileName();
    // TODO Phase 8b: create big map renderer (active only when M key map open)
    qInfo() << "ChildBigMap: Stub — no rendering yet";
    return true;
}

void ChildBigMap::onShutdown()
{
    qInfo() << "ChildBigMap: Shutting down for" << profileName();
}

void ChildBigMap::onMapEntered(uint32_t mapId)
{
    qInfo() << "ChildBigMap: Map entered:" << mapId << "for" << profileName();
}

void ChildBigMap::onMapLeft()
{
    qInfo() << "ChildBigMap: Map left for" << profileName();
}

void ChildBigMap::onFocusChanged(bool focused)
{
    qInfo() << "ChildBigMap: Focus" << (focused ? "gained" : "lost")
            << "for" << profileName();
}

void ChildBigMap::onSettingsReceived(const QJsonObject &settings)
{
    Q_UNUSED(settings);
    qInfo() << "ChildBigMap: Settings received (stub)";
}
