#include "BadgeDataProvider.h"

#include "APICache.h"
#include "BadgeRegistry.h"
#include "GW2APIClient.h"

#include <QJsonDocument>

BadgeDataProvider::BadgeDataProvider(APICache *cache, GW2APIClient *apiClient,
                                     QObject *parent)
    : QObject(parent), m_cache(cache), m_apiClient(apiClient) {
  // Connect to generic API response signal for ALL endpoint results
  connect(m_apiClient, &GW2APIClient::requestComplete, this,
          &BadgeDataProvider::onApiResponse);
}

void BadgeDataProvider::refreshProfile(const QString &profileId,
                                       const QString &apiKey,
                                       const QStringList &selectedBadges) {
  if (selectedBadges.isEmpty() || apiKey.isEmpty()) {
    qInfo() << "BadgeDataProvider: No badges selected or no API key for"
            << profileId;
    return;
  }

  // Determine ALL unique endpoints needed for selected badges
  QStringList endpoints = BadgeRegistry::requiredEndpoints(selectedBadges);

  if (endpoints.isEmpty()) {
    return;
  }

  // Map badge endpoint names to actual GW2APIClient request endpoints
  // (characters endpoint uses ?page=0)
  QMap<QString, QString> endpointMapping;
  for (const QString &ep : endpoints) {
    if (ep == "characters") {
      endpointMapping[ep] = "characters?page=0";
    } else {
      endpointMapping[ep] = ep;
    }
  }

  // Set up pending tracking for ALL endpoints
  PendingRefresh pending;
  pending.selectedBadges = selectedBadges;
  pending.apiKey = apiKey;
  for (const QString &ep : endpoints) {
    pending.pendingEndpoints.insert(ep);
  }
  m_pending[profileId] = pending;

  // Set API key and fire off ALL endpoint requests
  m_apiClient->setApiKey(apiKey);
  for (auto it = endpointMapping.begin(); it != endpointMapping.end(); ++it) {
    qInfo() << "BadgeDataProvider: Fetching" << it.value()
            << "for profile" << profileId;
    m_apiClient->request(it.value(), true);
  }
}

QMap<QString, QString>
BadgeDataProvider::cachedValues(const QString &profileId) const {
  return m_badgeValues.value(profileId);
}

QMap<QString, QString>
BadgeDataProvider::aggregateFromCache(const QString &profileId,
                                     const QStringList &selectedBadges) {
  QMap<QString, QString> result;

  for (const QString &badgeId : selectedBadges) {
    BadgeDefinition def = BadgeRegistry::badge(badgeId);
    if (def.id.isEmpty())
      continue;

    // Read from disk cache
    QJsonDocument doc = m_cache->read(profileId, def.apiEndpoint);
    QString formatted = BadgeRegistry::formatValue(badgeId, doc);
    result[badgeId] = formatted;
  }

  return result;
}

void BadgeDataProvider::onApiResponse(const QString &endpoint,
                                      const QJsonDocument &data) {
  // Normalize the response endpoint to match our badge endpoint names
  // e.g., "characters?page=0" -> "characters"
  QString normalizedEndpoint = endpoint;
  int queryIdx = normalizedEndpoint.indexOf('?');
  if (queryIdx >= 0) {
    normalizedEndpoint = normalizedEndpoint.left(queryIdx);
  }

  QStringList completedProfiles;

  for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
    const QString &profileId = it.key();
    PendingRefresh &pending = it.value();

    if (pending.pendingEndpoints.contains(normalizedEndpoint)) {
      // Cache the response using the normalized endpoint name
      m_cache->write(profileId, normalizedEndpoint, data);
      pending.pendingEndpoints.remove(normalizedEndpoint);

      qInfo() << "BadgeDataProvider: Cached" << normalizedEndpoint
              << "for" << profileId
              << "- remaining:" << pending.pendingEndpoints.size();

      // If all endpoints are done, aggregate and emit
      if (pending.pendingEndpoints.isEmpty()) {
        QMap<QString, QString> values =
            aggregateFromCache(profileId, pending.selectedBadges);
        m_badgeValues[profileId] = values;
        emit badgeDataReady(profileId, values);
        completedProfiles.append(profileId);
        qInfo() << "BadgeDataProvider: All badge data ready for" << profileId;
      }
    }
  }

  // Clean up completed pending entries
  for (const QString &id : completedProfiles) {
    m_pending.remove(id);
  }
}
