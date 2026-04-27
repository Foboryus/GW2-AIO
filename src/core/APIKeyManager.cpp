/**
 * @file APIKeyManager.cpp
 * @brief API Key Manager implementation
 *
 * Stores GW2 API keys in Windows Credential Manager via CredentialManager.
 * Validates keys by calling /v2/tokeninfo and /v2/account.
 *
 * Uses per-request tracking (QMap keyed by API key) to support multiple
 * concurrent validations without cross-wiring responses.
 *
 * DO NOT ADD:
 * - UI code (belongs in widgets)
 * - Direct QSettings usage
 */

#include "APIKeyManager.h"

#include <QDebug>

#include "CredentialManager.h"
#include "GW2APIClient.h"

APIKeyManager::APIKeyManager(GW2APIClient *apiClient, QObject *parent)
    : QObject(parent), m_apiClient(apiClient) {
  // Listen for token info responses — match by API key in pending map
  connect(m_apiClient, &GW2APIClient::tokenInfoFetched, this,
          [this](const QStringList &permissions, const QString &tokenName,
                 const QString &apiKey) {
            if (!m_pendingValidations.contains(apiKey))
              return;

            auto &pending = m_pendingValidations[apiKey];
            pending.tokenInfo.name = tokenName;
            pending.tokenInfo.permissions = permissions;

            // If account permission is available, also fetch account name
            if (permissions.contains("account")) {
              pending.awaitingAccount = true;
              m_apiClient->setApiKey(apiKey);
              m_apiClient->fetchAccount();
            } else {
              // No account permission — emit without account name
              emit keyValidated(pending.profileId, pending.tokenInfo,
                                QString());
              m_pendingValidations.remove(apiKey);
            }
          });

  // Listen for account responses — complete the validation
  connect(m_apiClient, &GW2APIClient::accountFetched, this,
          [this](const GW2APIClient::AccountInfo &account) {
            // Find the pending validation that's awaiting account
            for (auto it = m_pendingValidations.begin();
                 it != m_pendingValidations.end(); ++it) {
              if (it->awaitingAccount) {
                emit keyValidated(it->profileId, it->tokenInfo, account.name);
                m_pendingValidations.erase(it);
                return;
              }
            }
          });

  // Listen for errors — fail the matching validation
  connect(m_apiClient, &GW2APIClient::errorOccurred, this,
          [this](const QString &endpoint, const QString &error) {
            if (endpoint == "tokeninfo") {
              // tokeninfo failed — fail all pending validations
              // (we can't match by key since the error doesn't include it)
              for (auto it = m_pendingValidations.begin();
                   it != m_pendingValidations.end();) {
                if (!it->awaitingAccount) {
                  emit keyValidationFailed(it->profileId, error);
                  it = m_pendingValidations.erase(it);
                } else {
                  ++it;
                }
              }
            } else if (endpoint == "account") {
              // account failed — fail pending validations awaiting account
              for (auto it = m_pendingValidations.begin();
                   it != m_pendingValidations.end();) {
                if (it->awaitingAccount) {
                  emit keyValidationFailed(it->profileId, error);
                  it = m_pendingValidations.erase(it);
                } else {
                  ++it;
                }
              }
            }
          });
}

// ---------------------------------------------------------------------------
// Credential Manager operations
// ---------------------------------------------------------------------------

bool APIKeyManager::storeKey(const QString &profileId, const QString &apiKey) {
  QString target = credentialTarget(profileId);

  // CredentialManager stores username + encrypted password
  // We use "GW2APIKey" as username, the actual key as password
  bool ok = CredentialManager::storeCredential(target, "GW2APIKey", apiKey);
  if (ok) {
    qInfo() << "APIKeyManager: Stored API key for profile:" << profileId;
    emit keyStateChanged(profileId, true);
  } else {
    qWarning() << "APIKeyManager: Failed to store API key for profile:"
               << profileId;
  }
  return ok;
}

QString APIKeyManager::retrieveKey(const QString &profileId) const {
  QString target = credentialTarget(profileId);
  QString username, password;

  if (CredentialManager::retrieveCredential(target, username, password)) {
    return password; // The API key is stored as the "password"
  }
  return QString();
}

bool APIKeyManager::removeKey(const QString &profileId) {
  QString target = credentialTarget(profileId);

  // Don't warn if it doesn't exist — removing non-existent is success
  if (!CredentialManager::hasCredential(target)) {
    emit keyStateChanged(profileId, false);
    return true;
  }

  bool ok = CredentialManager::deleteCredential(target);
  if (ok) {
    qInfo() << "APIKeyManager: Removed API key for profile:" << profileId;
    emit keyStateChanged(profileId, false);
  }
  return ok;
}

bool APIKeyManager::hasKey(const QString &profileId) const {
  return CredentialManager::hasCredential(credentialTarget(profileId));
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

void APIKeyManager::validateKey(const QString &profileId,
                                const QString &apiKey) {
  if (apiKey.isEmpty()) {
    emit keyValidationFailed(profileId, "API key is empty");
    return;
  }

  // Cancel any existing validation for this profile
  for (auto it = m_pendingValidations.begin();
       it != m_pendingValidations.end();) {
    if (it->profileId == profileId) {
      it = m_pendingValidations.erase(it);
    } else {
      ++it;
    }
  }

  // Register this validation request
  PendingValidation pending;
  pending.profileId = profileId;
  pending.apiKey = apiKey;
  m_pendingValidations.insert(apiKey, pending);

  // Call /v2/tokeninfo with the key as query parameter
  m_apiClient->fetchTokenInfo(apiKey);
}

// ---------------------------------------------------------------------------
// Migration
// ---------------------------------------------------------------------------

void APIKeyManager::migrateFromJson(const QString &profileId,
                                    const QString &plaintextKey) {
  if (plaintextKey.isEmpty())
    return;

  // Only migrate if not already in Credential Manager
  if (hasKey(profileId)) {
    qInfo() << "APIKeyManager: Key already in Credential Manager for profile:"
            << profileId << "— skipping migration";
    return;
  }

  if (storeKey(profileId, plaintextKey)) {
    qInfo() << "APIKeyManager: Migrated plaintext key to Credential Manager "
               "for profile:"
            << profileId;
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString APIKeyManager::credentialTarget(const QString &profileId) {
  return QStringLiteral("GW2AIO_APIKey_%1").arg(profileId);
}
