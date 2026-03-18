#pragma once

#include <QString>
#include <QByteArray>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#include <dpapi.h>
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Advapi32.lib")
#endif

/**
 * @brief Secure credential storage using Windows DPAPI and Credential Manager
 * 
 * Provides encrypted storage for sensitive data like account passwords.
 * Uses Windows DPAPI for encryption tied to the current user.
 */
class CredentialManager
{
public:
    /**
     * @brief Store a credential securely
     * @param key Unique identifier for the credential
     * @param username Account username
     * @param password Account password (will be encrypted)
     * @return true if stored successfully
     */
    static bool storeCredential(const QString& key, const QString& username, 
                                 const QString& password);
    
    /**
     * @brief Retrieve a stored credential
     * @param key Unique identifier
     * @param username Output: stored username
     * @param password Output: decrypted password
     * @return true if found and decrypted
     */
    static bool retrieveCredential(const QString& key, QString& username, 
                                    QString& password);
    
    /**
     * @brief Delete a stored credential
     */
    static bool deleteCredential(const QString& key);
    
    /**
     * @brief Check if a credential exists
     */
    static bool hasCredential(const QString& key);
    
    /**
     * @brief Encrypt data using DPAPI (user-scope)
     */
    static QByteArray encrypt(const QByteArray& data);
    
    /**
     * @brief Decrypt data using DPAPI
     */
    static QByteArray decrypt(const QByteArray& encryptedData);
    
    /**
     * @brief Generate a key for a GW2 account
     */
    static QString accountKey(const QString& accountName) {
        return QString("GW2AIO_Account_%1").arg(accountName);
    }
};

// Implementation
#ifdef Q_OS_WIN

inline bool CredentialManager::storeCredential(const QString& key, 
                                                const QString& username,
                                                const QString& password)
{
    // Encrypt the password
    QByteArray encryptedPassword = encrypt(password.toUtf8());
    if (encryptedPassword.isEmpty()) {
        return false;
    }
    
    // Prepare credential structure
    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(key.toStdWString().c_str());
    cred.UserName = const_cast<LPWSTR>(username.toStdWString().c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(encryptedPassword.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(encryptedPassword.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    
    // Also store in comment for retrieval
    QString comment = QString("GW2AIO|%1").arg(username);
    std::wstring commentW = comment.toStdWString();
    cred.Comment = const_cast<LPWSTR>(commentW.c_str());
    
    return CredWriteW(&cred, 0) == TRUE;
}

inline bool CredentialManager::retrieveCredential(const QString& key,
                                                   QString& username,
                                                   QString& password)
{
    PCREDENTIALW pCred = nullptr;
    
    if (!CredReadW(key.toStdWString().c_str(), CRED_TYPE_GENERIC, 0, &pCred)) {
        return false;
    }
    
    // Extract username
    if (pCred->UserName) {
        username = QString::fromWCharArray(pCred->UserName);
    }
    
    // Decrypt password
    if (pCred->CredentialBlob && pCred->CredentialBlobSize > 0) {
        QByteArray encrypted(reinterpret_cast<char*>(pCred->CredentialBlob),
                            pCred->CredentialBlobSize);
        QByteArray decrypted = decrypt(encrypted);
        password = QString::fromUtf8(decrypted);
    }
    
    CredFree(pCred);
    return true;
}

inline bool CredentialManager::deleteCredential(const QString& key)
{
    return CredDeleteW(key.toStdWString().c_str(), CRED_TYPE_GENERIC, 0) == TRUE;
}

inline bool CredentialManager::hasCredential(const QString& key)
{
    PCREDENTIALW pCred = nullptr;
    bool exists = CredReadW(key.toStdWString().c_str(), CRED_TYPE_GENERIC, 0, &pCred) == TRUE;
    if (pCred) CredFree(pCred);
    return exists;
}

inline QByteArray CredentialManager::encrypt(const QByteArray& data)
{
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(data.data()));
    input.cbData = static_cast<DWORD>(data.size());
    
    DATA_BLOB output;
    
    // Use DPAPI with user scope
    if (!CryptProtectData(&input, L"GW2AIO", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return QByteArray();
    }
    
    QByteArray result(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    
    return result;
}

inline QByteArray CredentialManager::decrypt(const QByteArray& encryptedData)
{
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encryptedData.data()));
    input.cbData = static_cast<DWORD>(encryptedData.size());
    
    DATA_BLOB output;
    LPWSTR description = nullptr;
    
    if (!CryptUnprotectData(&input, &description, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return QByteArray();
    }
    
    QByteArray result(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    if (description) LocalFree(description);
    
    return result;
}

#else
// Non-Windows stubs
inline bool CredentialManager::storeCredential(const QString&, const QString&, const QString&) { return false; }
inline bool CredentialManager::retrieveCredential(const QString&, QString&, QString&) { return false; }
inline bool CredentialManager::deleteCredential(const QString&) { return false; }
inline bool CredentialManager::hasCredential(const QString&) { return false; }
inline QByteArray CredentialManager::encrypt(const QByteArray&) { return QByteArray(); }
inline QByteArray CredentialManager::decrypt(const QByteArray&) { return QByteArray(); }
#endif
