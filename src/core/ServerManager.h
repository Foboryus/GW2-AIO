#pragma once

/**
 * @brief Server Manager - Ported from LaunchBuddy ServerManager.cs
 *
 * Fetches and manages GW2 authentication and asset servers.
 * Provides ping checking and server selection for launch arguments.
 *
 * DO NOT ADD:
 * - Inline implementations (use ServerManager.cpp)
 */

#include <QElapsedTimer>
#include <QHostInfo>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QTimer>


/**
 * @brief Represents a GW2 server (auth or asset)
 */
struct GW2Server {
  QString ip;
  int port = 6112;
  QString type;  // "auth1", "auth2", "asset", "manual"
  int ping = -1; // -1 = unknown, 9999 = timeout
  bool enabled = false;
  bool online = false;

  QString toArgument() const { return QString("%1:%2").arg(ip).arg(port); }
};

class ServerManager : public QObject {
  Q_OBJECT

public:
  explicit ServerManager(QObject *parent = nullptr);

  // Server lists
  QList<GW2Server> authServers() const { return m_authServers; }
  QList<GW2Server> assetServers() const { return m_assetServers; }

  // Selected servers for launch
  GW2Server *selectedAuthServer() { return m_selectedAuth; }
  GW2Server *selectedAssetServer() { return m_selectedAsset; }

  void setSelectedAuthServer(int index);
  void setSelectedAssetServer(int index);

  // Custom servers
  void addAuthServer(const QString &ip, int port = 6112);
  void addAssetServer(const QString &ip, int port = 80);
  void removeAuthServer(int index);
  void removeAssetServer(int index);

  // Client port
  int clientPort() const { return m_clientPort; }
  void setClientPort(int port) { m_clientPort = port; }

  // Build launch arguments
  QString authServerArg() const;
  QString assetServerArg() const;
  QString clientPortArg() const;

public slots:
  void refreshServers();
  void checkServerPings();

signals:
  void serversUpdated();
  void pingUpdated(const QString &ip, int ping);
  void refreshStarted();
  void refreshFinished();
  void error(const QString &message);

private slots:
  void onAuthDnsLookup(const QHostInfo &host);
  void onAssetDnsLookup(const QHostInfo &host);

private:
  int tcpPing(const QString &ip, int port, int timeoutMs = 3000);

  QList<GW2Server> m_authServers;
  QList<GW2Server> m_assetServers;
  GW2Server *m_selectedAuth = nullptr;
  GW2Server *m_selectedAsset = nullptr;
  int m_clientPort = 0; // 0 = default

  int m_pendingLookups = 0;
  QMutex m_mutex;

  // DNS hostnames
  static constexpr const char *AUTH1_HOST = "auth1.101.ArenaNetworks.com";
  static constexpr const char *AUTH2_HOST = "auth2.101.ArenaNetworks.com";
  static constexpr const char *ASSET_HOST = "assetcdn.101.ArenaNetworks.com";

  static constexpr int DEFAULT_AUTH_PORT = 6112;
  static constexpr int DEFAULT_ASSET_PORT = 80;
};
