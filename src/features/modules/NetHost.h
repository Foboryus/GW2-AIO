// REVIEW BEFORE BETA: all implementations inline (308 lines) — split to .h/.cpp pair.
#pragma once

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QString>
#include <QLibrary>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/**
 * @brief Hosts .NET Core runtime for executing managed DLLs
 * 
 * Uses the hostfxr API to initialize CoreCLR and load assemblies.
 * Requires .NET Runtime to be installed on the system.
 * 
 * Reference: https://learn.microsoft.com/en-us/dotnet/core/tutorials/netcore-hosting
 */
class NetHost
{
public:
    NetHost();
    ~NetHost();
    
    /**
     * @brief Initialize the .NET runtime
     * @return true if successful
     */
    bool initialize();
    
    /**
     * @brief Check if runtime is initialized
     */
    bool isInitialized() const { return m_initialized; }
    
    /**
     * @brief Load a managed assembly
     * @param assemblyPath Path to .dll file
     * @return Handle to loaded assembly, or nullptr on failure
     */
    void* loadAssembly(const QString& assemblyPath);
    
    /**
     * @brief Get a function pointer to a managed method
     * @param assemblyHandle Handle from loadAssembly
     * @param typeName Full type name (e.g., "MyNamespace.MyClass")
     * @param methodName Method name
     * @return Function pointer or nullptr
     */
    void* getMethod(void* assemblyHandle, 
                    const QString& typeName,
                    const QString& methodName);
    
    /**
     * @brief Shutdown the runtime
     */
    void shutdown();
    
    /**
     * @brief Get last error message
     */
    QString lastError() const { return m_lastError; }
    
    /**
     * @brief Check if .NET runtime is available
     */
    static bool isRuntimeAvailable();
    
    /**
     * @brief Get .NET runtime version
     */
    static QString runtimeVersion();
    
private:
    bool loadHostFxr();
    bool initializeRuntime();
    
    bool m_initialized = false;
    QString m_lastError;
    
#ifdef Q_OS_WIN
    // hostfxr function pointers
    HMODULE m_hostfxrHandle = nullptr;
    void* m_hostContextHandle = nullptr;
    
    // hostfxr API functions
    typedef int (*hostfxr_initialize_for_runtime_config_fn)(
        const wchar_t* runtime_config_path,
        void* parameters,
        void** host_context_handle);
    
    typedef int (*hostfxr_get_runtime_delegate_fn)(
        void* host_context_handle,
        int type,
        void** delegate);
    
    typedef int (*hostfxr_close_fn)(void* host_context_handle);
    
    hostfxr_initialize_for_runtime_config_fn m_initFn = nullptr;
    hostfxr_get_runtime_delegate_fn m_getDelegateFn = nullptr;
    hostfxr_close_fn m_closeFn = nullptr;
    
    // Runtime delegate for loading assemblies
    typedef int (*load_assembly_and_get_function_pointer_fn)(
        const wchar_t* assembly_path,
        const wchar_t* type_name,
        const wchar_t* method_name,
        const wchar_t* delegate_type_name,
        void* reserved,
        void** delegate);
    
    load_assembly_and_get_function_pointer_fn m_loadAssemblyFn = nullptr;
#endif
};

// Implementation
inline NetHost::NetHost()
{
}

inline NetHost::~NetHost()
{
    shutdown();
}

inline bool NetHost::initialize()
{
    if (m_initialized) return true;
    
#ifdef Q_OS_WIN
    if (!loadHostFxr()) {
        return false;
    }
    
    if (!initializeRuntime()) {
        return false;
    }
    
    m_initialized = true;
    qInfo() << "NetHost initialized successfully";
    return true;
#else
    m_lastError = ".NET hosting only supported on Windows";
    return false;
#endif
}

inline bool NetHost::loadHostFxr()
{
#ifdef Q_OS_WIN
    // Try to find hostfxr.dll
    // Usually in: C:\Program Files\dotnet\host\fxr\<version>\hostfxr.dll
    
    // First try the environment
    QString dotnetRoot = qEnvironmentVariable("DOTNET_ROOT");
    if (dotnetRoot.isEmpty()) {
        dotnetRoot = "C:/Program Files/dotnet";
    }
    
    // Find latest fxr version
    QDir fxrDir(dotnetRoot + "/host/fxr");
    QStringList versions = fxrDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    if (versions.isEmpty()) {
        m_lastError = ".NET runtime not found. Install .NET 6+ from https://dotnet.microsoft.com";
        return false;
    }
    
    // Sort and get latest
    versions.sort();
    QString latestVersion = versions.last();
    QString hostfxrPath = fxrDir.filePath(latestVersion + "/hostfxr.dll");
    
    m_hostfxrHandle = LoadLibraryW(hostfxrPath.toStdWString().c_str());
    if (!m_hostfxrHandle) {
        m_lastError = "Failed to load hostfxr.dll from: " + hostfxrPath;
        return false;
    }
    
    // Get function pointers
    m_initFn = (hostfxr_initialize_for_runtime_config_fn)
        GetProcAddress(m_hostfxrHandle, "hostfxr_initialize_for_runtime_config");
    m_getDelegateFn = (hostfxr_get_runtime_delegate_fn)
        GetProcAddress(m_hostfxrHandle, "hostfxr_get_runtime_delegate");
    m_closeFn = (hostfxr_close_fn)
        GetProcAddress(m_hostfxrHandle, "hostfxr_close");
    
    if (!m_initFn || !m_getDelegateFn || !m_closeFn) {
        m_lastError = "Failed to get hostfxr function pointers";
        FreeLibrary(m_hostfxrHandle);
        m_hostfxrHandle = nullptr;
        return false;
    }
    
    qInfo() << "Loaded hostfxr from:" << hostfxrPath;
    return true;
#else
    return false;
#endif
}

inline bool NetHost::initializeRuntime()
{
#ifdef Q_OS_WIN
    // We need a runtimeconfig.json to initialize
    // For now, we'll create a minimal one in temp
    
    QString configPath = QDir::temp().filePath("gw2aio_runtime.runtimeconfig.json");
    QFile configFile(configPath);
    
    if (configFile.open(QIODevice::WriteOnly)) {
        configFile.write(R"({
  "runtimeOptions": {
    "tfm": "net6.0",
    "framework": {
      "name": "Microsoft.NETCore.App",
      "version": "6.0.0"
    }
  }
})");
        configFile.close();
    }
    
    // Initialize runtime
    int result = m_initFn(configPath.toStdWString().c_str(), nullptr, &m_hostContextHandle);
    
    if (result != 0 && result != 1) {  // 0 = success, 1 = already initialized
        m_lastError = QString("hostfxr_initialize failed with code: %1").arg(result);
        return false;
    }
    
    // Get the load_assembly delegate
    const int hdt_load_assembly_and_get_function_pointer = 5;
    result = m_getDelegateFn(m_hostContextHandle, 
                             hdt_load_assembly_and_get_function_pointer,
                             (void**)&m_loadAssemblyFn);
    
    if (result != 0 || !m_loadAssemblyFn) {
        m_lastError = QString("Failed to get load_assembly delegate: %1").arg(result);
        return false;
    }
    
    return true;
#else
    return false;
#endif
}

inline void* NetHost::loadAssembly(const QString& assemblyPath)
{
    Q_UNUSED(assemblyPath);
    // This is a placeholder - full implementation would use m_loadAssemblyFn
    m_lastError = "Assembly loading not yet fully implemented";
    return nullptr;
}

inline void* NetHost::getMethod(void* assemblyHandle, 
                                 const QString& typeName,
                                 const QString& methodName)
{
    Q_UNUSED(assemblyHandle);
    Q_UNUSED(typeName);
    Q_UNUSED(methodName);
    return nullptr;
}

inline void NetHost::shutdown()
{
#ifdef Q_OS_WIN
    if (m_hostContextHandle && m_closeFn) {
        m_closeFn(m_hostContextHandle);
        m_hostContextHandle = nullptr;
    }
    
    if (m_hostfxrHandle) {
        FreeLibrary(m_hostfxrHandle);
        m_hostfxrHandle = nullptr;
    }
#endif
    
    m_initialized = false;
}

inline bool NetHost::isRuntimeAvailable()
{
    QString dotnetRoot = qEnvironmentVariable("DOTNET_ROOT");
    if (dotnetRoot.isEmpty()) {
        dotnetRoot = "C:/Program Files/dotnet";
    }
    
    return QDir(dotnetRoot + "/host/fxr").exists();
}

inline QString NetHost::runtimeVersion()
{
    QString dotnetRoot = qEnvironmentVariable("DOTNET_ROOT");
    if (dotnetRoot.isEmpty()) {
        dotnetRoot = "C:/Program Files/dotnet";
    }
    
    QDir fxrDir(dotnetRoot + "/host/fxr");
    QStringList versions = fxrDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    if (versions.isEmpty()) return QString();
    
    versions.sort();
    return versions.last();
}
