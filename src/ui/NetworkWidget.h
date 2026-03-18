#pragma once

/**
 * @brief Network Widget - Server status and selection UI
 * 
 * Displays GW2 authentication and asset servers with ping times.
 * Allows selecting specific servers for launch and adding custom IPs.
 */

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QTableWidget>
#include <QPushButton>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QComboBox>
#include <QShowEvent>
#include <QTimer>
#include <algorithm>

#include "core/ServerManager.h"

class NetworkWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit NetworkWidget(QWidget* parent = nullptr);
    
    ServerManager* serverManager() { return m_serverManager; }
    
protected:
    void showEvent(QShowEvent* event) override;
    
private slots:
    void onRefresh();
    void onServersUpdated();
    void onAddAuthServer();
    void onAddAssetServer();
    void onAuthSelectionChanged();
    void onAssetSelectionChanged();
    
signals:
    void applyToAllProfiles();  // Emitted when user clicks "Apply to All Profiles"
    
private:
    void setupUI();
    void updateAuthTable();
    void updateAssetTable();
    void updateLaunchArgsInfo();
    QString pingToString(int ping);
    QString statusIcon(int ping);
    
    ServerManager* m_serverManager;
    bool m_hasLoaded = false;
    
    QTableWidget* m_authTable;
    QTableWidget* m_assetTable;
    QPushButton* m_refreshBtn;
    QLabel* m_authCountLabel;
    QLabel* m_assetCountLabel;
    QLabel* m_selectedAuthLabel;
    QLabel* m_selectedAssetLabel;
    QLabel* m_launchArgsLabel;
    QComboBox* m_portCombo;
};

