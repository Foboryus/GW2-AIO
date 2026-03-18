/**
 * @file ServerManager.cpp
 * @brief Server Manager - Ported from LaunchBuddy ServerManager.cs
 *
 * Fetches and manages GW2 authentication and asset servers.
 * Provides ping checking and server selection for launch arguments.
 *
 * DO NOT ADD:
 * - Launch logic (belongs in LaunchManager)
 * - Profile management (belongs in ProfileManager)
 */

#include "ServerManager.h"

#include <QDebug>
#include <QHostAddress>
#include <QMutexLocker>

ServerManager::ServerManager(QObject *parent) : QObject(parent) {}

void ServerManager::refreshServers() {
  emit refreshStarted();

  m_authServers.clear();
  m_assetServers.clear();
  m_pendingLookups = 3;

  // Lookup auth1
  QHostInfo::lookupHost(AUTH1_HOST, this, SLOT(onAuthDnsLookup(QHostInfo)));
  QHostInfo::lookupHost(AUTH2_HOST, this, SLOT(onAuthDnsLookup(QHostInfo)));
  QHostInfo::lookupHost(ASSET_HOST, this, SLOT(onAssetDnsLookup(QHostInfo)));
}

void ServerManager::onAuthDnsLookup(const QHostInfo &host) {
  QMutexLocker lock(&m_mutex);

  if (host.error() != QHostInfo::NoError) {
    qWarning() << "DNS lookup failed:" << host.errorString();
  } else {
    QString type = host.hostName().contains("auth1") ? "auth1" : "auth2";

    for (const QHostAddress &addr : host.addresses()) {
      if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        GW2Server server;
        server.ip = addr.toString();
        server.port = DEFAULT_AUTH_PORT;
        server.type = type;
        server.ping = -1;
        m_authServers.append(server);
      }
    }
  }

  m_pendingLookups--;
  if (m_pendingLookups == 0) {
    emit serversUpdated();
    emit refreshFinished();
    // Auto-check pings
    QTimer::singleShot(100, this, &ServerManager::checkServerPings);
  }
}

void ServerManager::onAssetDnsLookup(const QHostInfo &host) {
  QMutexLocker lock(&m_mutex);

  if (host.error() != QHostInfo::NoError) {
    qWarning() << "DNS lookup failed:" << host.errorString();
  } else {
    for (const QHostAddress &addr : host.addresses()) {
      if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        GW2Server server;
        server.ip = addr.toString();
        server.port = DEFAULT_ASSET_PORT;
        server.type = "asset";
        server.ping = -1;
        m_assetServers.append(server);
      }
    }
  }

  m_pendingLookups--;
  if (m_pendingLookups == 0) {
    emit serversUpdated();
    emit refreshFinished();
    QTimer::singleShot(100, this, &ServerManager::checkServerPings);
  }
}

void ServerManager::checkServerPings() {
  // Check auth server pings (TCP ping)
  for (int i = 0; i < m_authServers.size(); i++) {
    int ping = tcpPing(m_authServers[i].ip, m_authServers[i].port);
    m_authServers[i].ping = ping;
    m_authServers[i].online = (ping < 9999);
    emit pingUpdated(m_authServers[i].ip, ping);
  }

  // Check asset server pings
  for (int i = 0; i < m_assetServers.size(); i++) {
    int ping = tcpPing(m_assetServers[i].ip, m_assetServers[i].port);
    m_assetServers[i].ping = ping;
    m_assetServers[i].online = (ping < 9999);
    emit pingUpdated(m_assetServers[i].ip, ping);
  }

  emit serversUpdated();
}

int ServerManager::tcpPing(const QString &ip, int port, int timeoutMs) {
  QTcpSocket socket;
  QElapsedTimer timer;

  timer.start();
  socket.connectToHost(ip, port);

  if (socket.waitForConnected(timeoutMs)) {
    int elapsed = static_cast<int>(timer.elapsed());
    socket.disconnectFromHost();
    return elapsed;
  }

  return 9999; // Timeout
}

void ServerManager::addAuthServer(const QString &ip, int port) {
  GW2Server server;
  server.ip = ip;
  server.port = port;
  server.type = "manual";
  server.ping = tcpPing(ip, port);
  server.online = (server.ping < 9999);

  m_authServers.append(server);
  emit serversUpdated();
}

void ServerManager::addAssetServer(const QString &ip, int port) {
  GW2Server server;
  server.ip = ip;
  server.port = port;
  server.type = "manual";
  server.ping = tcpPing(ip, port);
  server.online = (server.ping < 9999);

  m_assetServers.append(server);
  emit serversUpdated();
}

void ServerManager::removeAuthServer(int index) {
  if (index >= 0 && index < m_authServers.size()) {
    m_authServers.removeAt(index);
    emit serversUpdated();
  }
}

void ServerManager::removeAssetServer(int index) {
  if (index >= 0 && index < m_assetServers.size()) {
    m_assetServers.removeAt(index);
    emit serversUpdated();
  }
}

void ServerManager::setSelectedAuthServer(int index) {
  if (index >= 0 && index < m_authServers.size()) {
    m_selectedAuth = &m_authServers[index];
  } else {
    m_selectedAuth = nullptr;
  }
}

void ServerManager::setSelectedAssetServer(int index) {
  if (index >= 0 && index < m_assetServers.size()) {
    m_selectedAsset = &m_assetServers[index];
  } else {
    m_selectedAsset = nullptr;
  }
}

QString ServerManager::authServerArg() const {
  if (m_selectedAuth) {
    return QString("-authsrv %1").arg(m_selectedAuth->toArgument());
  }
  return QString();
}

QString ServerManager::assetServerArg() const {
  if (m_selectedAsset) {
    return QString("-portal %1").arg(m_selectedAsset->toArgument());
  }
  return QString();
}

QString ServerManager::clientPortArg() const {
  if (m_clientPort > 0) {
    return QString("-clientport %1").arg(m_clientPort);
  }
  return QString();
}
