#include "MainWindow.h"

#include "ApiClient.h"
#include "Common.h"
#include "ConfigEditor.h"
#include "LoginDialog.h"
#include "PriorityTab.h"
#include "ProfilesTab.h"
#include "StatsTab.h"
#include "StatusTab.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QHideEvent>
#include <QJsonArray>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QRectF>
#include <QShowEvent>
#include <QStatusBar>
#include <QTabWidget>

namespace {
constexpr int kPollVisibleMs = 1000;
constexpr int kPollHiddenMs = 3000;
constexpr int kCoreGoneMs = 10000;
} // namespace

MainWindow::MainWindow(ApiClient* api, QWidget* parent)
    : QMainWindow(parent)
    , m_api(api)
{
    setWindowTitle(tr("Proton VPN Client"));
    resize(1120, 740);

    buildTabs();
    buildTray();

    statusBar()->showMessage(tr("Ready."));

    m_pollTimer.setInterval(kPollHiddenMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &MainWindow::poll);
}

MainWindow::~MainWindow()
{
    m_pollTimer.stop();
    // The login dialog owns a QWebEngineProfile; destroy it explicitly and early.
    delete m_login;
    m_login = nullptr;
    if (m_tray) {
        m_tray->hide();
    }
}

void MainWindow::buildTabs()
{
    m_tabs = new QTabWidget(this);

    m_statusTab = new StatusTab(m_tabs);
    m_profilesTab = new ProfilesTab(m_api, m_tabs);
    m_priorityTab = new PriorityTab(m_api, m_tabs);
    m_configEditor = new ConfigEditor(m_api, m_tabs);
    m_statsTab = new StatsTab(m_api, m_tabs);

    m_tabs->addTab(m_statusTab, tr("Status"));
    m_tabs->addTab(m_profilesTab, tr("Profiles"));
    m_tabs->addTab(m_priorityTab, tr("Priority"));
    m_tabs->addTab(m_configEditor, tr("Settings"));
    m_tabs->addTab(m_statsTab, tr("Stats"));
    setCentralWidget(m_tabs);

    connect(m_statusTab, &StatusTab::modeSelected, this, &MainWindow::setMode);
    connect(m_statusTab, &StatusTab::benchmarkToggled, this, &MainWindow::setBenchmark);
    connect(m_statusTab, &StatusTab::disconnectRequested, this, &MainWindow::disconnectProfile);
    connect(m_statusTab, &StatusTab::loginRequested, this, &MainWindow::openLogin);
    connect(m_statusTab, &StatusTab::logoutRequested, this, &MainWindow::logout);

    connect(m_profilesTab, &ProfilesTab::message, this, &MainWindow::showMessage);
    connect(m_priorityTab, &PriorityTab::message, this, &MainWindow::showMessage);
    connect(m_configEditor, &ConfigEditor::message, this, &MainWindow::showMessage);
    connect(m_statsTab, &StatsTab::message, this, &MainWindow::showMessage);
}

void MainWindow::buildTray()
{
    m_tray = new QSystemTrayIcon(this);
    m_tray->setIcon(makeIcon(TrayState::Unknown));
    m_tray->setToolTip(tr("Proton VPN Client"));

    m_trayMenu = new QMenu(this);

    QAction* openAct = m_trayMenu->addAction(tr("Open"));
    connect(openAct, &QAction::triggered, this, &MainWindow::showWindow);

    m_modeMenu = m_trayMenu->addMenu(tr("Mode"));
    m_modeGroup = new QActionGroup(this);
    m_modeGroup->setExclusive(true);
    for (const Common::ModeInfo& m : Common::modes()) {
        QAction* a = m_modeMenu->addAction(m.label);
        a->setCheckable(true);
        a->setData(m.id);
        m_modeGroup->addAction(a);
        m_modeActions.insert(m.id, a);
        const QString id = m.id;
        connect(a, &QAction::triggered, this, [this, id]() { setMode(id); });
    }

    m_trayMenu->addSeparator();
    QAction* quitAct = m_trayMenu->addAction(tr("Quit"));
    connect(quitAct, &QAction::triggered, this, &MainWindow::requestQuit);

    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
}

void MainWindow::start()
{
    m_tray->show();
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        // Without a tray there would be no way to reach the UI at all.
        showWindow();
    }
    applyPollInterval();
    m_pollTimer.start();
    poll();
}

// ---------------------------------------------------------------------------
// Window / tray behaviour
// ---------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_quitting || QGuiApplication::isSavingSession()) {
        event->accept();
        return;
    }
    // Closing the window only hides it; the tray icon stays.
    hide();
    event->ignore();
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    m_windowShown = true;
    applyPollInterval();
    pollSoon();
}

void MainWindow::hideEvent(QHideEvent* event)
{
    QMainWindow::hideEvent(event);
    m_windowShown = false;
    applyPollInterval();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        toggleWindow();
    }
}

void MainWindow::toggleWindow()
{
    if (isVisible() && !isMinimized()) {
        hide();
    } else {
        showWindow();
    }
}

void MainWindow::showWindow()
{
    if (isMinimized()) {
        showNormal();
    }
    show();
    raise();
    activateWindow();
}

void MainWindow::showMessage(const QString& text)
{
    statusBar()->showMessage(text, 10000);
}

void MainWindow::quitApp()
{
    if (m_quitting) {
        return;
    }
    m_quitting = true;
    m_pollTimer.stop();
    if (m_tray) {
        m_tray->hide();
    }
    QApplication::quit();
}

void MainWindow::requestQuit()
{
    if (m_quitting) {
        return;
    }
    m_pollTimer.stop(); // avoid flagging the core as "unreachable" while it shuts down
    // Quit as soon as the core answered (or went away), with a hard fallback.
    m_api->post(QStringLiteral("/quit"), QJsonObject(), this,
                [this](const ApiClient::Response&) { quitApp(); }, 3000);
    QTimer::singleShot(4000, this, [this]() { quitApp(); });
}

// ---------------------------------------------------------------------------
// Polling and state
// ---------------------------------------------------------------------------

void MainWindow::applyPollInterval()
{
    // While the core is failing, poll fast regardless of visibility so the 10 s rule is accurate.
    const int ms = (m_windowShown || m_failing) ? kPollVisibleMs : kPollHiddenMs;
    if (m_pollTimer.interval() != ms) {
        m_pollTimer.setInterval(ms);
    }
}

void MainWindow::pollSoon()
{
    QTimer::singleShot(100, this, [this]() { poll(); });
}

void MainWindow::poll()
{
    if (m_pollInFlight || m_quitting) {
        return;
    }
    m_pollInFlight = true;
    m_api->get(QStringLiteral("/state"), this,
               [this](const ApiClient::Response& r) {
                   m_pollInFlight = false;
                   const QJsonObject st = r.doc.isObject() ? r.doc.object() : QJsonObject();
                   onStateReply(r.ok && r.doc.isObject(), r.networkError, st, r.error);
               },
               4000);
}

void MainWindow::onStateReply(bool ok, bool networkError, const QJsonObject& state, const QString& error)
{
    if (m_quitting) {
        return;
    }

    if (networkError) {
        if (!m_failing) {
            m_failing = true;
            m_failCount = 0;
            m_firstFailure.start();
            applyPollInterval();
        }
        ++m_failCount;
        setTrayState(TrayState::Unreachable, tr("Proton VPN Client - core unreachable"));
        m_statusTab->setUnreachable();
        // Require a few failed attempts as well as 10 s, so a single stale timer after
        // a suspend/resume cannot make the GUI quit on its own.
        if (m_failCount >= 3 && m_firstFailure.hasExpired(kCoreGoneMs)) {
            quitApp();
        }
        return;
    }

    if (m_failing) {
        m_failing = false;
        m_failCount = 0;
        applyPollInterval();
    }

    if (!ok) {
        // The core answered, but with an HTTP error (for example a wrong token).
        showMessage(tr("GET /state failed: %1").arg(error));
        return;
    }

    m_statusTab->setState(state);

    const QString mode = state.value(QStringLiteral("mode")).toString().toLower();
    updateModeChecks(mode);

    const QJsonArray conns = state.value(QStringLiteral("connections")).toArray();
    int connected = 0;
    for (const QJsonValue& v : conns) {
        const QString s = v.toObject().value(QStringLiteral("state")).toString().toLower();
        if (s == QLatin1String("connected")) {
            ++connected;
        }
    }

    TrayState ts = TrayState::Connecting; // amber: connecting or no connection at all
    if (connected > 0) {
        ts = TrayState::Connected;
    }
    QString modeLabel = mode;
    for (const Common::ModeInfo& m : Common::modes()) {
        if (m.id == mode) {
            modeLabel = m.label;
        }
    }
    setTrayState(ts,
                 tr("Proton VPN Client - mode %1, %2 of %3 connection(s) up")
                     .arg(modeLabel)
                     .arg(connected)
                     .arg(conns.size()));
}

void MainWindow::updateModeChecks(const QString& mode)
{
    QAction* a = m_modeActions.value(mode, nullptr);
    if (a && !a->isChecked()) {
        a->setChecked(true);
    }
}

// ---------------------------------------------------------------------------
// Tray icon
// ---------------------------------------------------------------------------

QIcon MainWindow::makeIcon(TrayState st)
{
    QColor fill;
    switch (st) {
    case TrayState::Connected:
        fill = QColor(0x2e, 0xa0, 0x43); // green
        break;
    case TrayState::Unreachable:
        fill = QColor(0xcf, 0x22, 0x2e); // red
        break;
    case TrayState::Connecting:
    case TrayState::Unknown:
    default:
        fill = QColor(0xd2, 0x99, 0x22); // amber
        break;
    }

    QIcon icon;
    const int sizes[] = {16, 24, 32, 48, 64, 128};
    for (int s : sizes) {
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        const qreal m = s / 16.0;
        const QRectF circle(m, m, s - 2 * m, s - 2 * m);
        p.setPen(QPen(fill.darker(160), qMax<qreal>(1.0, s / 24.0)));
        p.setBrush(fill);
        p.drawEllipse(circle);

        QFont f;
        f.setBold(true);
        f.setPixelSize(qMax(8, static_cast<int>(s * 0.58)));
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(circle, Qt::AlignCenter, QStringLiteral("P"));
        p.end();

        icon.addPixmap(pm);
    }
    return icon;
}

void MainWindow::setTrayState(TrayState st, const QString& tooltip)
{
    if (st != m_trayState || !m_icons.contains(static_cast<int>(st))) {
        if (!m_icons.contains(static_cast<int>(st))) {
            m_icons.insert(static_cast<int>(st), makeIcon(st));
        }
        m_tray->setIcon(m_icons.value(static_cast<int>(st)));
        m_trayState = st;
    }
    if (m_tray->toolTip() != tooltip) {
        m_tray->setToolTip(tooltip);
    }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void MainWindow::setMode(const QString& mode)
{
    QJsonObject body;
    body.insert(QStringLiteral("mode"), mode);
    m_api->post(QStringLiteral("/mode"), body, this, [this, mode](const ApiClient::Response& r) {
        if (r.ok) {
            showMessage(tr("Mode set to %1.").arg(mode));
        } else {
            showMessage(tr("Setting mode failed: %1").arg(r.error));
        }
        pollSoon(); // also reverts the tray check mark if the core refused
    });
}

void MainWindow::setBenchmark(bool enabled)
{
    QJsonObject body;
    body.insert(QStringLiteral("enabled"), enabled);
    m_api->post(QStringLiteral("/benchmark"), body, this, [this](const ApiClient::Response& r) {
        if (!r.ok) {
            showMessage(tr("Setting benchmark mode failed: %1").arg(r.error));
        }
        pollSoon();
    });
}

void MainWindow::disconnectProfile(const QString& id)
{
    QJsonObject body;
    body.insert(QStringLiteral("id"), id);
    m_api->post(QStringLiteral("/disconnect"), body, this, [this, id](const ApiClient::Response& r) {
        if (r.ok) {
            showMessage(tr("Disconnected %1.").arg(id));
        } else {
            showMessage(tr("Disconnect of %1 failed: %2").arg(id, r.error));
        }
        pollSoon();
    });
}

void MainWindow::logout()
{
    m_api->post(QStringLiteral("/auth/logout"), QJsonObject(), this, [this](const ApiClient::Response& r) {
        if (r.ok) {
            showMessage(tr("Logged out."));
            // Drop the browser session too, so stale cookies are not offered again.
            ensureLoginDialog()->clearBrowserData();
        } else {
            showMessage(tr("Logout failed: %1").arg(r.error));
        }
        pollSoon();
    });
}

LoginDialog* MainWindow::ensureLoginDialog()
{
    if (!m_login) {
        // No parent: the dialog must be usable while the main window is hidden.
        m_login = new LoginDialog(m_api, nullptr);
        connect(m_login, &LoginDialog::loggedIn, this, [this]() {
            showMessage(tr("Proton session sent to the core."));
            pollSoon();
        });
    }
    return m_login;
}

void MainWindow::openLogin()
{
    ensureLoginDialog()->startLogin();
}
