#pragma once

#include <QObject>
#include <QString>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QMessageBox>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")
#endif

/**
 * @brief Crash handler with minidump generation
 * 
 * Captures crashes and generates diagnostic information.
 */
class CrashHandler : public QObject
{
    Q_OBJECT
    
public:
    static CrashHandler& instance();
    
    /**
     * @brief Install crash handlers
     */
    void install();
    
    /**
     * @brief Get crash dump directory
     */
    QString crashDumpDir() const { return m_crashDir; }
    
    /**
     * @brief Check for previous crash
     */
    bool hadPreviousCrash() const;
    
    /**
     * @brief Get previous crash info
     */
    QString previousCrashInfo() const;
    
    /**
     * @brief Clear crash flag
     */
    void clearCrashFlag();

#ifdef Q_OS_WIN
    /**
     * @brief Raw Win32 exception filter — safe to call during Qt teardown
     */
    static LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo);
#endif
    
private:
    CrashHandler();
    
    void writeCrashFlag();
    void generateCrashReport(const QString& reason);

#ifdef Q_OS_WIN
    static CrashHandler* s_instance;
#endif
    
    QString m_crashDir;
    QString m_crashFlagPath;
};

// Implementation
inline CrashHandler& CrashHandler::instance()
{
    static CrashHandler instance;
    return instance;
}

inline CrashHandler::CrashHandler()
{
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_crashDir = QDir(appData).filePath("crashes");
    m_crashFlagPath = QDir(appData).filePath(".crash_flag");
    
    QDir().mkpath(m_crashDir);
}

inline void CrashHandler::install()
{
#ifdef Q_OS_WIN
    s_instance = this;
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
#endif
    
    // Set crash flag (cleared on clean exit)
    writeCrashFlag();
    
    qInfo() << "Crash handler installed";
}

inline bool CrashHandler::hadPreviousCrash() const
{
    return QFile::exists(m_crashFlagPath);
}

inline QString CrashHandler::previousCrashInfo() const
{
    QFile file(m_crashFlagPath);
    if (file.open(QIODevice::ReadOnly)) {
        return QString::fromUtf8(file.readAll());
    }
    return QString();
}

inline void CrashHandler::clearCrashFlag()
{
    QFile::remove(m_crashFlagPath);
}

inline void CrashHandler::writeCrashFlag()
{
    QFile file(m_crashFlagPath);
    if (file.open(QIODevice::WriteOnly)) {
        QString info = QString("Started: %1\nVersion: %2\n")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                 QCoreApplication::applicationVersion());
        file.write(info.toUtf8());
    }
}

inline void CrashHandler::generateCrashReport(const QString& reason)
{
    QString filename = QString("crash_%1.txt")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    QString path = QDir(m_crashDir).filePath(filename);
    
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream out(&file);
        out << "GW2 AIO Manager Crash Report\n";
        out << "============================\n\n";
        out << "Time: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        out << "Version: " << QCoreApplication::applicationVersion() << "\n";
        out << "Reason: " << reason << "\n\n";
        
        // TODO: Add more diagnostic info
        // - Stack trace
        // - Memory usage
        // - Loaded modules
    }
}

#ifdef Q_OS_WIN
inline CrashHandler* CrashHandler::s_instance = nullptr;

inline LONG WINAPI CrashHandler::unhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo)
{
    // CRITICAL: This handler fires during Qt teardown — NO Qt APIs allowed.
    // Only raw Win32 calls are safe here.

    // Build dump path: <exe_dir>\crashes\crash_YYYYMMDD_HHMMSS.dmp
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    // Strip filename to get directory
    wchar_t *lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    // Ensure crashes subdirectory exists
    wchar_t crashDir[MAX_PATH] = {};
    wsprintfW(crashDir, L"%scrashes", exePath);
    CreateDirectoryW(crashDir, nullptr);

    // Timestamp for unique filename
    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t dumpPath[MAX_PATH] = {};
    wsprintfW(dumpPath, L"%s\\crash_%04d%02d%02d_%02d%02d%02d.dmp",
              crashDir, st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = exceptionInfo;
        mei.ClientPointers = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          MiniDumpWithDataSegs, &mei, nullptr, nullptr);
        CloseHandle(hFile);
    }

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
