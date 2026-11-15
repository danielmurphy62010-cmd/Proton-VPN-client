#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QIcon>
#include <QJsonObject>
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTimer>

class ApiClient;
class ConfigEditor;
class LoginDialog;
class PriorityTab;
class ProfilesTab;
class StatsTab;
class StatusTab;
class QAction;
class QActionGroup;
class QCloseEvent;
class QHideEvent;
class QMenu;
class QShowEvent;
class QTabWidget;

// Main window plus the tray icon. The tray icon always exists; the window is only
// shown on demand and hides instead of closing.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ApiClient* api, QWidget* parent = nullptr);
    ~MainWindow() override;

    // Starts polling and shows the tray icon. The window itself is NOT shown
    // (unless the platform has no system tray at all).
    void start();

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void toggleWindow();
    void showWindow();
    void poll();

private:
    enum class TrayState { Unknown, Connected, Connecting, Unreachable };

    void buildTray();
    void buildTabs();
    void setTrayState(TrayState st, const QString& tooltip);
    static QIcon makeIcon(TrayState st);

    void onStateReply(bool ok, bool networkError, const QJsonObject& state, const QString& error);
    void updateModeChecks(const QString& mode);
    void applyPollInterval();
    void pollSoon();

    void setMode(const QString& mode);
    void setBenchmark(bool enabled);
    void disconnectProfile(const QString& id);
    void logout();
    void openLogin();
    LoginDialog* ensureLoginDialog();
    void showMessage(const QString& text);
    void quitApp();
    void requestQuit();

    ApiClient* m_api;

    QTabWidget* m_tabs = nullptr;
    StatusTab* m_statusTab = nullptr;
    ProfilesTab* m_profilesTab = nullptr;
    PriorityTab* m_priorityTab = nullptr;
    ConfigEditor* m_configEditor = nullptr;
    StatsTab* m_statsTab = nullptr;
    LoginDialog* m_login = nullptr;

    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    QMenu* m_modeMenu = nullptr;
    QActionGroup* m_modeGroup = nullptr;
    QHash<QString, QAction*> m_modeActions;
    TrayState m_trayState = TrayState::Unknown;
    QHash<int, QIcon> m_icons;

    QTimer m_pollTimer;
    bool m_pollInFlight = false;
    bool m_windowShown = false;

    // Core liveness: quit the GUI when /state has failed continuously for 10 seconds.
    bool m_failing = false;
    int m_failCount = 0;
    QElapsedTimer m_firstFailure;

    bool m_quitting = false;
};
