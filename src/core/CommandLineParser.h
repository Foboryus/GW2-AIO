#pragma once

#include <QObject>
#include <QStringList>
#include <QCommandLineParser>
#include <QCommandLineOption>

/**
 * @brief Command-line argument parser
 * 
 * Supports automation and advanced usage.
 */
class CommandLineParser : public QObject
{
    Q_OBJECT
    
public:
    struct Options {
        // Launch options
        bool autoLaunchGW2 = false;
        QString launchProfile;
        
        // UI options
        bool startMinimized = false;
        bool noOverlay = false;
        bool noTray = false;
        
        // Debug options
        bool verbose = false;
        bool consoleLog = false;
        QString logLevel = "info";
        
        // Feature toggles
        bool noRadial = false;
        bool noDPS = false;
        bool noMarkers = false;
        bool noModules = false;
        
        // Paths
        QString configPath;
        QString gw2Path;
    };
    
    explicit CommandLineParser(QObject* parent = nullptr);
    
    /**
     * @brief Parse command line arguments
     */
    bool parse(const QStringList& arguments);
    
    /**
     * @brief Get parsed options
     */
    const Options& options() const { return m_options; }
    
    /**
     * @brief Show help and exit
     */
    void showHelp();
    
    /**
     * @brief Get error message if parse failed
     */
    QString errorText() const { return m_parser.errorText(); }
    
private:
    void setupOptions();
    
    QCommandLineParser m_parser;
    Options m_options;
};

// Implementation
inline CommandLineParser::CommandLineParser(QObject* parent)
    : QObject(parent)
{
    setupOptions();
}

inline void CommandLineParser::setupOptions()
{
    m_parser.setApplicationDescription("GW2 AIO Manager - All-in-one overlay for Guild Wars 2");
    m_parser.addHelpOption();
    m_parser.addVersionOption();
    
    // Launch options
    m_parser.addOption({
        {"l", "launch"},
        "Auto-launch GW2 on startup"
    });
    
    m_parser.addOption({
        {"p", "profile"},
        "Launch with specific profile",
        "profile_name"
    });
    
    // UI options
    m_parser.addOption({
        {"m", "minimized"},
        "Start minimized to tray"
    });
    
    m_parser.addOption({
        "no-overlay",
        "Disable overlay features"
    });
    
    m_parser.addOption({
        "no-tray",
        "Disable system tray icon"
    });
    
    // Debug options
    m_parser.addOption({
        {"v", "verbose"},
        "Enable verbose logging"
    });
    
    m_parser.addOption({
        "console",
        "Show console window for logging"
    });
    
    m_parser.addOption({
        "log-level",
        "Set log level (debug|info|warning|error)",
        "level",
        "info"
    });
    
    // Feature toggles
    m_parser.addOption({
        "no-radial",
        "Disable radial menus"
    });
    
    m_parser.addOption({
        "no-dps",
        "Disable DPS tracker"
    });
    
    m_parser.addOption({
        "no-markers",
        "Disable marker system"
    });
    
    m_parser.addOption({
        "no-modules",
        "Disable Blish modules"
    });
    
    // Paths
    m_parser.addOption({
        "config",
        "Use custom config directory",
        "path"
    });
    
    m_parser.addOption({
        "gw2-path",
        "Override GW2 installation path",
        "path"
    });
}

inline bool CommandLineParser::parse(const QStringList& arguments)
{
    if (!m_parser.parse(arguments)) {
        return false;
    }
    
    // Extract options
    m_options.autoLaunchGW2 = m_parser.isSet("launch");
    m_options.launchProfile = m_parser.value("profile");
    
    m_options.startMinimized = m_parser.isSet("minimized");
    m_options.noOverlay = m_parser.isSet("no-overlay");
    m_options.noTray = m_parser.isSet("no-tray");
    
    m_options.verbose = m_parser.isSet("verbose");
    m_options.consoleLog = m_parser.isSet("console");
    m_options.logLevel = m_parser.value("log-level");
    
    m_options.noRadial = m_parser.isSet("no-radial");
    m_options.noDPS = m_parser.isSet("no-dps");
    m_options.noMarkers = m_parser.isSet("no-markers");
    m_options.noModules = m_parser.isSet("no-modules");
    
    m_options.configPath = m_parser.value("config");
    m_options.gw2Path = m_parser.value("gw2-path");
    
    return true;
}

inline void CommandLineParser::showHelp()
{
    m_parser.showHelp();
}
