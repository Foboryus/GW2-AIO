#pragma once

/**
 * @brief API Key Manager — secure storage and validation of GW2 API keys
 *
 * Uses Windows Credential Manager (via CredentialManager) for encrypted
 * per-profile API key storage. Validates keys via /v2/tokeninfo.
 *
 * DO NOT ADD:
 * - UI code (belongs in widgets)
 * - Inline implementations (use APIKeyManager.cpp)
 */

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

class GW2APIClient;

class APIKeyManager : public QObject {
  Q_OBJECT

public:
  explicit APIKeyManager(GW2APIClient *apiClient, QObject *parent = nullptr);

  // --- Credential Manager operations ---

  /**
   * @brief Store an API key securely for a profile
   * @param profileId UUID of the profile
   * @param apiKey The GW2 API key to store
   * @return true if stored successfully
   */
  bool storeKey(const QString &profileId, const QString &apiKey);

  /**
   * @brief Retrieve the stored API key for a profile
   * @param profileId UUID of the profile
   * @return The API key, or empty string if not found
   */
  QString retrieveKey(const QString &profileId) const;

  /**
   * @brief Delete the stored API key for a profile
   * @param profileId UUID of the profile
   * @return true if deleted (or didn't exist)
   */
  bool removeKey(const QString &profileId);

  /**
   * @brief Check if a profile has a stored API key
   */
  bool hasKey(const QString &profileId) const;

  // --- Validation ---

  /**
   * @brief Validate an API key by calling /v2/tokeninfo
   *
   * Results are returned asynchronously via signals.
   * Also fetches /v2/account for the account name if key has account scope.
   *
   * Multiple concurrent validations are tracked independently by profileId.
   */
  void validateKey(const QString &profileId, const QString &apiKey);

  // --- Migration ---

  /**
   * @brief Migrate a plaintext API key from profile JSON to Credential Manager
   * @param profileId UUID of the profile
   * @param plaintextKey The key from the old gw2ApiKey JSON field
   */
  void migrateFromJson(const QString &profileId, const QString &plaintextKey);

  // --- Token info ---

  struct TokenInfo {
    QString name;           // User-assigned key name
    QStringList permissions; // e.g., ["account", "characters", "wallet"]
  };

signals:
  /**
   * @brief Emitted when key validation succeeds
   */
  void keyValidated(const QString &profileId, const TokenInfo &tokenInfo,
                    const QString &accountName);

  /**
   * @brief Emitted when key validation fails
   */
  void keyValidationFailed(const QString &profileId, const QString &error);

  /**
   * @brief Emitted when key is stored/removed (for UI refresh)
   */
  void keyStateChanged(const QString &profileId, bool hasKey);

private:
  /**
   * @brief Generate the Credential Manager target name for a profile
   */
  static QString credentialTarget(const QString &profileId);

  GW2APIClient *m_apiClient;

  // Per-request validation tracking — keyed by API key being validated
  // Supports multiple concurrent validations without cross-wiring
  struct PendingValidation {
    QString profileId;
    QString apiKey;
    TokenInfo tokenInfo; // Populated after tokeninfo response
    bool awaitingAccount = false; // Waiting for account fetch
  };
  QMap<QString, PendingValidation> m_pendingValidations; // key = apiKey
};
