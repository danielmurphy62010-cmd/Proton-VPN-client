#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QEventLoop>
#include <QIcon>
#include <QStyle>
#include <QStringList>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPalette>
#include <QPointer>
#include <QScrollArea>
#include <QStackedWidget>
#include <QAbstractItemView>
#include <QFrame>
#include <QHash>
#include <QList>
#include <QPair>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <utility>

class TorLoginDialog final : public QDialog {
    Q_OBJECT
public:
    explicit TorLoginDialog(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("Proton sign-in (Tor)");
        resize(1050, 760);
        auto *layout = new QVBoxLayout(this);
        auto *toolbar = new QHBoxLayout();
        auto *back = new QPushButton("Back");
        auto *forward = new QPushButton("Forward");
        auto *reload = new QPushButton("Reload");
        m_address = new QLineEdit("https://account.proton.me/login");
        auto *go = new QPushButton("Go");
        toolbar->addWidget(back);
        toolbar->addWidget(forward);
        toolbar->addWidget(reload);
        toolbar->addWidget(m_address, 1);
        toolbar->addWidget(go);
        layout->addLayout(toolbar);

        m_view = new QWebEngineView(this);
        const QString appData = QCoreApplication::applicationDirPath() + "/data";
        QDir().mkpath(appData);
        m_profile = new QWebEngineProfile("ProtonTorLogin", this);
        m_profile->setPersistentStoragePath(appData + "/tor-login-profile");
        m_profile->setCachePath(appData + "/tor-login-cache");
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
        m_view->setPage(new QWebEnginePage(m_profile, m_view));
        layout->addWidget(m_view, 1);

        connect(back, &QPushButton::clicked, m_view, &QWebEngineView::back);
        connect(forward, &QPushButton::clicked, m_view, &QWebEngineView::forward);
        connect(reload, &QPushButton::clicked, m_view, &QWebEngineView::reload);
        connect(go, &QPushButton::clicked, this, [this] { m_view->load(QUrl::fromUserInput(m_address->text())); });
        connect(m_address, &QLineEdit::returnPressed, go, &QPushButton::click);
        connect(m_view, &QWebEngineView::urlChanged, this, [this](const QUrl &url) { m_address->setText(url.toString()); });
        m_view->load(QUrl(m_address->text()));
    }
private:
    QLineEdit *m_address{};
    QWebEngineView *m_view{};
    QWebEngineProfile *m_profile{};
};

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QString apiBase, QString apiToken, QString proxyAddress)
        : m_apiBase(std::move(apiBase)), m_apiToken(std::move(apiToken)), m_proxyAddress(std::move(proxyAddress)) {
        setWindowTitle("Proton VPN Client");
        resize(1280, 900);
        setMinimumSize(1000, 700);
        buildUi();
        buildTray();
        loadConfig();
        refreshStatus();
        refreshMetrics();
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this] { refreshStatus(); refreshMetrics(); });
        m_timer->start(2500);
    }

protected:
    void closeEvent(QCloseEvent *event) override {
        event->ignore();
        hide();
        if (m_tray) m_tray->showMessage("Proton VPN Client", "Still running in the system tray.", QSystemTrayIcon::Information, 2500);
    }

private:
    QString m_apiBase;
    QString m_apiToken;
    QString m_proxyAddress;
    QNetworkAccessManager m_network;
    QSystemTrayIcon *m_tray{};
    QTimer *m_timer{};
    QPointer<TorLoginDialog> m_loginBrowser;
    QStackedWidget *m_pages{};
    QLabel *m_headerDot{};
    QLabel *m_subtitle{};
    QLabel *m_proxyModeValue{};
    QLabel *m_fallbackValue{};
    QLabel *m_activeConnection{};
    QTableWidget *m_metrics{};
    QList<QPushButton *> m_navButtons;
    QList<QPushButton *> m_modeButtons;
    QList<QPushButton *> m_fallbackButtons;

    QLabel *m_status{};
    QLabel *m_torStatus{};
    QComboBox *m_mode{};
    QComboBox *m_profileChoice{};
    QLineEdit *m_countries{};
    QTableWidget *m_profiles{};
    QTableWidget *m_servers{};
    QLineEdit *m_apiBaseEdit{};
    QLineEdit *m_proxyBind{};
    QLineEdit *m_torExe{};
    QLineEdit *m_openVpnExe{};
    QLineEdit *m_wireguardGo{};
    QLineEdit *m_wgExe{};
    QLineEdit *m_stealthExe{};
    QSpinBox *m_fallbackAttempts{};
    QComboBox *m_finalFallback{};
    QSpinBox *m_pingInterval{};
    QCheckBox *m_externalCommands{};
    QCheckBox *m_benchmarkEnabled{};
    QSpinBox *m_benchmarkPing{};
    QSpinBox *m_benchmarkRotate{};
    QLineEdit *m_accessToken{};
    QLineEdit *m_uid{};
    QLabel *m_authLabel{};
    QComboBox *m_importProtocol{};
    QLineEdit *m_importCountry{};

    struct WeightFields { QDoubleSpinBox *latency{}; QDoubleSpinBox *success{}; QDoubleSpinBox *failure{}; QDoubleSpinBox *connections{}; QDoubleSpinBox *ratio{}; };
    WeightFields m_normalWeights;
    WeightFields m_benchmarkWeights;

    QJsonDocument request(const QString &method, const QString &path, const QJsonObject &body = {}, bool quiet = false) {
        QNetworkRequest req(QUrl(m_apiBase + path));
        req.setRawHeader("x-client-token", m_apiToken.toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply *reply = nullptr;
        const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
        if (method == "GET") reply = m_network.get(req);
        else if (method == "POST") reply = m_network.post(req, payload);
        else reply = m_network.sendCustomRequest(req, method.toUtf8(), payload);

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(12000);
        loop.exec();
        if (!timeout.isActive()) reply->abort();
        timeout.stop();

        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->readAll();
        const auto error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || code >= 400) {
            QString detail = QString::fromUtf8(bytes);
            if (detail.isEmpty()) detail = errorText;
            if (!quiet) QMessageBox::warning(this, "Request failed", QString("%1 %2 failed (HTTP %3):\n%4").arg(method, path).arg(code).arg(detail));
            return {};
        }
        QJsonParseError parseError;
        auto doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError && !bytes.trimmed().isEmpty()) {
            if (!quiet) QMessageBox::warning(this, "Invalid response", parseError.errorString());
            return {};
        }
        return doc;
    }

    void buildUi() {
        auto *central = new QWidget(this);
        central->setObjectName("appBackground");
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(24, 22, 24, 22);
        root->setSpacing(16);

        auto *header = new QFrame;
        header->setObjectName("headerCard");
        auto *headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(20, 16, 20, 16);
        headerLayout->setSpacing(18);
        auto *identity = new QHBoxLayout;
        identity->setSpacing(12);
        m_headerDot = new QLabel("●");
        m_headerDot->setObjectName("statusDot");
        identity->addWidget(m_headerDot, 0, Qt::AlignVCenter);
        auto *titleColumn = new QVBoxLayout;
        titleColumn->setSpacing(3);
        auto *title = new QLabel("Proton VPN Client");
        title->setObjectName("pageTitle");
        m_subtitle = new QLabel("Local proxy: " + m_proxyAddress + "  |  Starting…");
        m_subtitle->setObjectName("mutedText");
        titleColumn->addWidget(title);
        titleColumn->addWidget(m_subtitle);
        identity->addLayout(titleColumn);
        headerLayout->addLayout(identity);
        headerLayout->addStretch(1);

        auto *nav = new QFrame;
        nav->setObjectName("navFrame");
        auto *navLayout = new QHBoxLayout(nav);
        navLayout->setContentsMargins(6, 5, 6, 5);
        navLayout->setSpacing(4);
        const QStringList navNames{"Connections", "Analytics", "Profiles && Servers", "Config"};
        for (int i = 0; i < navNames.size(); ++i) {
            auto *button = new QPushButton(navNames[i]);
            button->setObjectName("navButton");
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            button->setMinimumHeight(34);
            m_navButtons.push_back(button);
            navLayout->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, i] {
                m_pages->setCurrentIndex(i);
                for (int j = 0; j < m_navButtons.size(); ++j)
                    m_navButtons[j]->setChecked(j == i);
            });
        }
        m_navButtons[0]->setChecked(true);
        headerLayout->addWidget(nav);
        root->addWidget(header);

        auto makeCard = [](const QString &name, QWidget *parent = nullptr) {
            auto *card = new QFrame(parent);
            card->setObjectName(name);
            card->setAttribute(Qt::WA_StyledBackground, true);
            return card;
        };
        auto makeEyebrow = [](const QString &text) {
            auto *value = new QLabel(text);
            value->setObjectName("eyebrow");
            return value;
        };
        auto makeValue = [](const QString &text, const QString &objectName = "summaryValue") {
            auto *value = new QLabel(text);
            value->setObjectName(objectName);
            return value;
        };

        auto *summary = new QWidget;
        auto *summaryLayout = new QHBoxLayout(summary);
        summaryLayout->setContentsMargins(0, 0, 0, 0);
        summaryLayout->setSpacing(16);

        auto *modeCard = makeCard("summaryCard");
        modeCard->setMinimumHeight(98);
        auto *modeLayout = new QHBoxLayout(modeCard);
        modeLayout->setContentsMargins(17, 13, 17, 13);
        modeLayout->setSpacing(12);
        auto *modeInfo = new QVBoxLayout;
        modeInfo->setSpacing(4);
        modeInfo->addWidget(makeEyebrow("PROXY MODE"));
        m_proxyModeValue = makeValue("Smart VPN");
        modeInfo->addWidget(m_proxyModeValue);
        modeLayout->addLayout(modeInfo, 1);
        auto *modeActions = new QHBoxLayout;
        modeActions->setSpacing(5);
        const QList<QPair<QString, QString>> quickModes{
            {"VPN", "smart"}, {"Tor", "tor"}, {"LAN", "lan"}
        };
        for (const auto &item : quickModes) {
            auto *button = new QPushButton(item.first);
            button->setObjectName("modeButton");
            button->setProperty("mode", item.second);
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            button->setMinimumHeight(32);
            m_modeButtons.push_back(button);
            modeActions->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, mode = item.second] {
                setCombo(m_mode, mode);
                saveConfig();
            });
        }
        modeLayout->addLayout(modeActions);
        summaryLayout->addWidget(modeCard, 2);

        auto *fallbackCard = makeCard("summaryCard");
        fallbackCard->setMinimumHeight(98);
        auto *fallbackLayout = new QHBoxLayout(fallbackCard);
        fallbackLayout->setContentsMargins(17, 13, 17, 13);
        auto *fallbackInfo = new QVBoxLayout;
        fallbackInfo->setSpacing(4);
        fallbackInfo->addWidget(makeEyebrow("FALLBACK STRATEGY"));
        m_fallbackValue = makeValue("Tor Fallback");
        fallbackInfo->addWidget(m_fallbackValue);
        fallbackLayout->addLayout(fallbackInfo, 1);
        for (const QString &mode : {QString("tor"), QString("lan")}) {
            auto *button = new QPushButton(mode.toUpper());
            button->setObjectName("fallbackButton");
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            m_fallbackButtons.push_back(button);
            fallbackLayout->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, mode] {
                setCombo(m_finalFallback, mode);
                saveConfig();
            });
        }
        summaryLayout->addWidget(fallbackCard, 1);

        auto *activeCard = makeCard("summaryCard");
        activeCard->setMinimumHeight(98);
        auto *activeLayout = new QVBoxLayout(activeCard);
        activeLayout->setContentsMargins(17, 13, 17, 13);
        activeLayout->setSpacing(4);
        activeLayout->addWidget(makeEyebrow("ACTIVE CONNECTION"));
        m_activeConnection = makeValue("—", "summaryAccentValue");
        activeLayout->addWidget(m_activeConnection);
        summaryLayout->addWidget(activeCard, 1);
        root->addWidget(summary);

        m_pages = new QStackedWidget;
        m_pages->setObjectName("pageStack");
        root->addWidget(m_pages, 1);

        // Connections page
        auto *connectionsPage = new QWidget;
        auto *connectionsLayout = new QVBoxLayout(connectionsPage);
        connectionsLayout->setContentsMargins(0, 0, 0, 0);
        auto *connectionCard = makeCard("contentCard");
        auto *connectionBody = new QVBoxLayout(connectionCard);
        connectionBody->setContentsMargins(22, 19, 22, 20);
        connectionBody->setSpacing(12);
        auto *connectionTitle = new QLabel("Connection Overview");
        connectionTitle->setObjectName("sectionTitle");
        connectionBody->addWidget(connectionTitle);
        m_status = new QLabel("Starting…");
        m_status->setObjectName("connectionStatus");
        m_status->setWordWrap(true);
        connectionBody->addWidget(m_status);
        m_torStatus = new QLabel("Tor: starting");
        m_torStatus->setObjectName("mutedText");
        connectionBody->addWidget(m_torStatus);

        auto *connectionControls = new QHBoxLayout;
        connectionControls->addWidget(new QLabel("Mode"));
        m_mode = new QComboBox;
        addMode(m_mode, "Smart VPN", "smart");
        addMode(m_mode, "OpenVPN", "open_vpn");
        addMode(m_mode, "WireGuard", "wire_guard");
        addMode(m_mode, "Stealth (external backend)", "stealth");
        addMode(m_mode, "Tor", "tor");
        addMode(m_mode, "LAN (no VPN)", "lan");
        connectionControls->addWidget(m_mode, 1);
        connectionControls->addWidget(new QLabel("Profile"));
        m_profileChoice = new QComboBox;
        connectionControls->addWidget(m_profileChoice, 2);
        connectionBody->addLayout(connectionControls);

        auto *connectionButtons = new QHBoxLayout;
        auto *connectButton = new QPushButton("Connect");
        connectButton->setObjectName("primaryButton");
        auto *disconnectButton = new QPushButton("Disconnect");
        disconnectButton->setObjectName("dangerButton");
        auto *saveButton = new QPushButton("Save settings");
        connectionButtons->addWidget(connectButton);
        connectionButtons->addWidget(disconnectButton);
        connectionButtons->addWidget(saveButton);
        connectionButtons->addStretch(1);
        connectionBody->addLayout(connectionButtons);
        connect(connectButton, &QPushButton::clicked, this, [this] { connectProfile(); });
        connect(disconnectButton, &QPushButton::clicked, this, [this] {
            request("POST", "/api/disconnect");
            refreshStatus();
        });
        connect(saveButton, &QPushButton::clicked, this, [this] { saveConfig(); });

        auto *proxyNote = new QLabel(QString("Local HTTP proxy: %1\nTraffic is routed only for applications explicitly configured to use this proxy. It is not a system-wide TUN/VPN adapter.").arg(m_proxyAddress));
        proxyNote->setObjectName("mutedText");
        proxyNote->setWordWrap(true);
        connectionBody->addWidget(proxyNote);
        connectionsLayout->addWidget(connectionCard);
        connectionsLayout->addStretch(1);
        m_pages->addWidget(connectionsPage);

        // Analytics page
        auto *analyticsPage = new QWidget;
        auto *analyticsLayout = new QVBoxLayout(analyticsPage);
        analyticsLayout->setContentsMargins(0, 0, 0, 0);
        auto *analyticsCard = makeCard("contentCard");
        auto *analyticsBody = new QVBoxLayout(analyticsCard);
        analyticsBody->setContentsMargins(22, 19, 22, 20);
        analyticsBody->setSpacing(8);
        auto *analyticsTitle = new QLabel("Profile Success & Failure Metrics");
        analyticsTitle->setObjectName("sectionTitle");
        analyticsBody->addWidget(analyticsTitle);
        auto *analyticsDescription = new QLabel("Rolling measurements and connection totals reported by the Rust supervisor.");
        analyticsDescription->setObjectName("mutedText");
        analyticsBody->addWidget(analyticsDescription);
        m_metrics = new QTableWidget(0, 6);
        m_metrics->setObjectName("analyticsTable");
        m_metrics->setHorizontalHeaderLabels({"Profile", "Connections", "Successful", "Failed", "Avg. Ping", "Samples"});
        m_metrics->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_metrics->setSelectionMode(QAbstractItemView::NoSelection);
        m_metrics->setShowGrid(false);
        m_metrics->verticalHeader()->setVisible(false);
        m_metrics->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        analyticsBody->addWidget(m_metrics, 1);
        analyticsLayout->addWidget(analyticsCard);
        m_pages->addWidget(analyticsPage);

        // Profiles and Proton server list page
        auto *profilesPage = new QWidget;
        auto *profilesOuter = new QHBoxLayout(profilesPage);
        profilesOuter->setContentsMargins(0, 0, 0, 0);
        profilesOuter->setSpacing(16);

        auto *profileCard = makeCard("contentCard");
        auto *profileBody = new QVBoxLayout(profileCard);
        profileBody->setContentsMargins(18, 17, 18, 17);
        profileBody->setSpacing(8);
        auto *profileTitle = new QLabel("Imported VPN Profiles");
        profileTitle->setObjectName("sectionTitle");
        profileBody->addWidget(profileTitle);
        auto *profileDesc = new QLabel("Import .ovpn and WireGuard .conf files from your local machine.");
        profileDesc->setObjectName("mutedText");
        profileBody->addWidget(profileDesc);
        m_profiles = new QTableWidget(0, 5);
        m_profiles->setHorizontalHeaderLabels({"Use", "Profile", "Protocol", "Country", "Config file"});
        m_profiles->setObjectName("dataTable");
        m_profiles->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_profiles->setSelectionMode(QAbstractItemView::SingleSelection);
        m_profiles->setShowGrid(false);
        m_profiles->verticalHeader()->setVisible(false);
        m_profiles->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_profiles->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        profileBody->addWidget(m_profiles, 1);
        auto *importRow = new QHBoxLayout;
        m_importProtocol = new QComboBox;
        addMode(m_importProtocol, "OpenVPN", "open_vpn");
        addMode(m_importProtocol, "WireGuard", "wire_guard");
        addMode(m_importProtocol, "Stealth (external only)", "stealth");
        m_importCountry = new QLineEdit;
        m_importCountry->setPlaceholderText("Country code");
        auto *importButton = new QPushButton("＋ Import profile");
        importButton->setObjectName("primaryButton");
        importRow->addWidget(m_importProtocol, 1);
        importRow->addWidget(m_importCountry);
        importRow->addWidget(importButton);
        profileBody->addLayout(importRow);
        connect(importButton, &QPushButton::clicked, this, [this] { importProfile(); });
        auto *profileActions = new QHBoxLayout;
        auto *saveProfiles = new QPushButton("Save profile selection");
        auto *removeProfile = new QPushButton("Remove selected");
        removeProfile->setObjectName("dangerButton");
        profileActions->addWidget(saveProfiles);
        profileActions->addWidget(removeProfile);
        profileBody->addLayout(profileActions);
        connect(saveProfiles, &QPushButton::clicked, this, [this] { saveConfig(); });
        connect(removeProfile, &QPushButton::clicked, this, [this] {
            const int row = m_profiles->currentRow();
            if (row < 0 || !m_profiles->item(row, 0)) return;
            const QString id = m_profiles->item(row, 0)->data(Qt::UserRole).toString();
            if (id.isEmpty()) return;
            request("POST", "/api/profiles/remove", QJsonObject{{"profile_id", id}});
            loadConfig();
        });
        profilesOuter->addWidget(profileCard, 3);

        auto *serversCard = makeCard("contentCard");
        auto *serversBody = new QVBoxLayout(serversCard);
        serversBody->setContentsMargins(18, 17, 18, 17);
        serversBody->setSpacing(8);
        auto *serversTitle = new QLabel("Proton Logical Servers");
        serversTitle->setObjectName("sectionTitle");
        serversBody->addWidget(serversTitle);
        auto *serversDesc = new QLabel("Retrieved from Proton after API authentication.");
        serversDesc->setObjectName("mutedText");
        serversBody->addWidget(serversDesc);
        m_servers = new QTableWidget(0, 7);
        m_servers->setObjectName("dataTable");
        m_servers->setHorizontalHeaderLabels({"Server", "Country", "City", "Load", "Tier", "Protocol", "Entry IP"});
        m_servers->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_servers->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_servers->setShowGrid(false);
        m_servers->verticalHeader()->setVisible(false);
        m_servers->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_servers->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
        serversBody->addWidget(m_servers, 1);
        auto *refreshServers = new QPushButton("Refresh Proton server list");
        connect(refreshServers, &QPushButton::clicked, this, [this] {
            auto doc = request("POST", "/api/servers/refresh");
            if (!doc.isNull()) populateServers(doc.array());
        });
        serversBody->addWidget(refreshServers);
        profilesOuter->addWidget(serversCard, 4);
        m_pages->addWidget(profilesPage);

        // Configuration page
        auto *configPage = new QWidget;
        auto *configOuter = new QVBoxLayout(configPage);
        configOuter->setContentsMargins(0, 0, 0, 0);
        auto *configScroll = new QScrollArea;
        configScroll->setWidgetResizable(true);
        configScroll->setFrameShape(QFrame::NoFrame);
        configScroll->setObjectName("contentScroll");
        auto *configContent = new QWidget;
        auto *configColumns = new QHBoxLayout(configContent);
        configColumns->setContentsMargins(0, 0, 4, 0);
        configColumns->setSpacing(16);

        auto *generalCard = makeCard("contentCard");
        auto *generalForm = new QFormLayout(generalCard);
        generalForm->setContentsMargins(20, 18, 20, 18);
        generalForm->setSpacing(9);
        auto *generalTitle = new QLabel("Client Configuration");
        generalTitle->setObjectName("sectionTitle");
        generalForm->addRow(generalTitle);
        m_apiBaseEdit = new QLineEdit;
        m_proxyBind = new QLineEdit;
        m_torExe = new QLineEdit;
        m_openVpnExe = new QLineEdit;
        m_wireguardGo = new QLineEdit;
        m_wgExe = new QLineEdit;
        m_stealthExe = new QLineEdit;
        m_countries = new QLineEdit;
        m_fallbackAttempts = new QSpinBox; m_fallbackAttempts->setRange(1, 20);
        m_finalFallback = new QComboBox; m_finalFallback->addItem("Tor", "tor"); m_finalFallback->addItem("LAN / direct", "lan");
        m_pingInterval = new QSpinBox; m_pingInterval->setRange(1, 3600);
        m_externalCommands = new QCheckBox("Allow external tunnel executables (drivers/elevation may be required)");
        generalForm->addRow("Proton API base URL", m_apiBaseEdit);
        generalForm->addRow("Proxy bind (loopback only)", m_proxyBind);
        generalForm->addRow("Tor executable", m_torExe);
        generalForm->addRow("OpenVPN executable", m_openVpnExe);
        generalForm->addRow("wireguard-go executable", m_wireguardGo);
        generalForm->addRow("wg.exe executable", m_wgExe);
        generalForm->addRow("Optional Stealth executable", m_stealthExe);
        generalForm->addRow("Allowed country codes", m_countries);
        generalForm->addRow("Fallback profile attempts", m_fallbackAttempts);
        generalForm->addRow("Final fallback", m_finalFallback);
        generalForm->addRow("Ping interval (seconds)", m_pingInterval);
        generalForm->addRow(m_externalCommands);
        auto *saveSettings = new QPushButton("Save all settings");
        saveSettings->setObjectName("primaryButton");
        generalForm->addRow(saveSettings);
        connect(saveSettings, &QPushButton::clicked, this, [this] { saveConfig(); });
        configColumns->addWidget(generalCard, 3);

        auto *modeSettingsCard = makeCard("contentCard");
        auto *modeForm = new QFormLayout(modeSettingsCard);
        modeForm->setContentsMargins(20, 18, 20, 18);
        modeForm->setSpacing(8);
        auto *normalTitle = new QLabel("Normal Mode Priority");
        normalTitle->setObjectName("sectionTitle");
        modeForm->addRow(normalTitle);
        modeForm->addRow("Latency weight", m_normalWeights.latency = makeWeight());
        modeForm->addRow("Successful connections", m_normalWeights.success = makeWeight());
        modeForm->addRow("Failed connections", m_normalWeights.failure = makeWeight());
        modeForm->addRow("Connection count", m_normalWeights.connections = makeWeight());
        modeForm->addRow("Success/failure ratio", m_normalWeights.ratio = makeWeight());
        auto *benchmarkTitle = new QLabel("Benchmark Mode");
        benchmarkTitle->setObjectName("sectionTitle");
        modeForm->addRow(benchmarkTitle);
        m_benchmarkEnabled = new QCheckBox("Enable benchmark mode");
        m_benchmarkPing = new QSpinBox; m_benchmarkPing->setRange(1, 3600);
        m_benchmarkRotate = new QSpinBox; m_benchmarkRotate->setRange(1, 3600);
        modeForm->addRow(m_benchmarkEnabled);
        modeForm->addRow("Measurement interval (seconds)", m_benchmarkPing);
        modeForm->addRow("Profile rotation interval (seconds)", m_benchmarkRotate);
        modeForm->addRow("Latency weight", m_benchmarkWeights.latency = makeWeight());
        modeForm->addRow("Successful connections", m_benchmarkWeights.success = makeWeight());
        modeForm->addRow("Failed connections", m_benchmarkWeights.failure = makeWeight());
        modeForm->addRow("Connection count", m_benchmarkWeights.connections = makeWeight());
        modeForm->addRow("Success/failure ratio", m_benchmarkWeights.ratio = makeWeight());
        auto *saveBenchmark = new QPushButton("Save benchmark settings");
        modeForm->addRow(saveBenchmark);
        connect(saveBenchmark, &QPushButton::clicked, this, [this] { saveConfig(); });
        configColumns->addWidget(modeSettingsCard, 3);

        configScroll->setWidget(configContent);
        configOuter->addWidget(configScroll, 1);

        auto *authCard = makeCard("contentCard");
        auto *authForm = new QFormLayout(authCard);
        authForm->setContentsMargins(20, 17, 20, 17);
        authForm->setSpacing(8);
        auto *authTitle = new QLabel("Proton API Session");
        authTitle->setObjectName("sectionTitle");
        m_authLabel = new QLabel("No API session loaded");
        m_authLabel->setObjectName("mutedText");
        m_accessToken = new QLineEdit;
        m_accessToken->setEchoMode(QLineEdit::Password);
        m_uid = new QLineEdit;
        auto *authActions = new QHBoxLayout;
        auto *saveSession = new QPushButton("Use API access token + UID");
        saveSession->setObjectName("primaryButton");
        auto *openBrowser = new QPushButton("Open Proton sign-in through Tor");
        authActions->addWidget(saveSession);
        authActions->addWidget(openBrowser);
        authForm->addRow(authTitle);
        authForm->addRow(m_authLabel);
        authForm->addRow("API access token", m_accessToken);
        authForm->addRow("API session UID", m_uid);
        authForm->addRow(authActions);
        auto *authNote = new QLabel("The Tor browser stores website cookies separately. Signing in there does not automatically populate the API access token and UID required for the server list.");
        authNote->setObjectName("mutedText");
        authNote->setWordWrap(true);
        authForm->addRow(authNote);
        configOuter->addWidget(authCard);
        connect(saveSession, &QPushButton::clicked, this, [this] {
            QJsonObject body{{"access_token", m_accessToken->text()}, {"uid", m_uid->text()}};
            auto doc = request("POST", "/api/auth/session", body);
            if (!doc.isNull()) {
                m_authLabel->setText("API session loaded (memory only)");
                m_accessToken->clear();
                refreshStatus();
            }
        });
        connect(openBrowser, &QPushButton::clicked, this, [this] { openTorBrowser(); });
        m_pages->addWidget(configPage);

        setCentralWidget(central);
        setStyleSheet(R"QSS(
            QWidget { color:#f1f5f9; font-family:"Segoe UI",Arial; font-size:9pt; }
            QWidget#appBackground, QMainWindow { background:#020617; }
            QLabel { background:transparent; }
            QFrame#headerCard, QFrame#summaryCard, QFrame#contentCard {
                background:#0f172a; border:1px solid #1e293b; border-radius:15px;
            }
            QFrame#navFrame { background:#020617; border:1px solid #1e293b; border-radius:11px; }
            QLabel#statusDot { color:#10b981; font-size:18pt; }
            QLabel#pageTitle { color:#f8fafc; font-size:17pt; font-weight:700; }
            QLabel#mutedText { color:#94a3b8; font-size:8.5pt; }
            QLabel#eyebrow { color:#94a3b8; font-size:8pt; font-weight:700; }
            QLabel#summaryValue { color:#f8fafc; font-size:12pt; font-weight:700; }
            QLabel#summaryAccentValue { color:#818cf8; font-size:12pt; font-weight:700; }
            QLabel#sectionTitle { color:#f8fafc; font-size:14pt; font-weight:700; }
            QLabel#connectionStatus { color:#e2e8f0; font-size:11pt; font-weight:600; }
            QPushButton { color:#e2e8f0; background:#1e293b; border:1px solid #334155; border-radius:9px; padding:7px 11px; min-height:20px; font-size:8.5pt; font-weight:600; }
            QPushButton:hover { background:#334155; }
            QPushButton:pressed { background:#475569; }
            QPushButton:focus { border:1px solid #818cf8; }
            QPushButton#navButton { color:#94a3b8; background:transparent; border:0; padding:8px 13px; }
            QPushButton#navButton:hover { color:#f8fafc; background:#172033; }
            QPushButton#navButton:checked { color:#fff; background:#4f46e5; }
            QPushButton#modeButton, QPushButton#fallbackButton { color:#e2e8f0; background:#1e293b; border:1px solid #334155; border-radius:9px; padding:6px 12px; }
            QPushButton#modeButton:checked { color:#fff; background:#059669; border-color:#059669; }
            QPushButton#modeButton[mode="tor"]:checked { background:#9333ea; border-color:#9333ea; }
            QPushButton#modeButton[mode="lan"]:checked { background:#d97706; border-color:#d97706; }
            QPushButton#fallbackButton:checked { color:#fff; background:#4f46e5; border-color:#4f46e5; }
            QPushButton#primaryButton { color:#fff; background:#4f46e5; border-color:#4f46e5; }
            QPushButton#primaryButton:hover { background:#6366f1; }
            QPushButton#dangerButton { color:#fda4af; background:#3f1020; border-color:#881337; }
            QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox { color:#e2e8f0; background:#020617; selection-background-color:#4338ca; border:1px solid #334155; border-radius:9px; padding:7px 9px; min-height:20px; }
            QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus { border:1px solid #818cf8; }
            QComboBox QAbstractItemView { color:#e2e8f0; background:#0f172a; border:1px solid #475569; selection-background-color:#4338ca; }
            QCheckBox { spacing:8px; }
            QTableWidget { color:#e2e8f0; background:#0f172a; alternate-background-color:#111c31; border:0; gridline-color:#1e293b; selection-background-color:#312e81; }
            QTableWidget::item { padding:8px; border-bottom:1px solid #1e293b; }
            QTableWidget#analyticsTable::item { padding:10px 8px; }
            QHeaderView { background:#0f172a; }
            QHeaderView::section { color:#94a3b8; background:#0f172a; border:0; border-bottom:1px solid #1e293b; padding:10px 8px; font-size:8pt; font-weight:600; }
            QScrollArea { background:transparent; border:0; }
            QScrollBar:vertical { background:#0b1220; width:10px; margin:2px; border:0; }
            QScrollBar::handle:vertical { background:#334155; min-height:24px; border-radius:5px; }
            QScrollBar::handle:vertical:hover { background:#475569; }
            QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
            QMenu { color:#e2e8f0; background:#0f172a; border:1px solid #334155; padding:5px; }
            QMenu::item { padding:7px 24px 7px 12px; border-radius:5px; }
            QMenu::item:selected { color:#fff; background:#3730a3; }
            QToolTip { color:#f8fafc; background:#0f172a; border:1px solid #475569; padding:5px; }
        )QSS");
    }

    void buildTray() {
        QMenu *menu = new QMenu(this);
        QAction *open = menu->addAction("Open");
        QAction *connectAction = menu->addAction("Connect");
        QAction *disconnect = menu->addAction("Disconnect");
        menu->addSeparator();
        QAction *exit = menu->addAction("Exit");
        connect(open, &QAction::triggered, this, [this] { showFromTray(); });
        connect(connectAction, &QAction::triggered, this, [this] { connectProfile(); });
        connect(disconnect, &QAction::triggered, this, [this] { request("POST", "/api/disconnect"); refreshStatus(); });
        connect(exit, &QAction::triggered, this, [this] { request("POST", "/api/quit", {}, true); qApp->quit(); });
        m_tray = new QSystemTrayIcon(this);
        m_tray->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
        m_tray->setToolTip("Proton VPN Client");
        m_tray->setContextMenu(menu);
        connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) showFromTray();
        });
        m_tray->show();
    }

    void showFromTray() {
        showNormal();
        raise();
        activateWindow();
    }

    void openTorBrowser() {
        if (m_loginBrowser) {
            m_loginBrowser->show();
            m_loginBrowser->raise();
            m_loginBrowser->activateWindow();
            return;
        }
        auto *dialog = new TorLoginDialog(nullptr);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowFlag(Qt::Window, true);
        m_loginBrowser = dialog;
        connect(dialog, &QObject::destroyed, this, [this] { m_loginBrowser = nullptr; });
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    }

    static void addMode(QComboBox *combo, const QString &label, const QString &value) {
        combo->addItem(label, value);
    }
    static QDoubleSpinBox *makeWeight() {
        auto *spin = new QDoubleSpinBox;
        spin->setRange(-1000.0, 1000.0);
        spin->setDecimals(3);
        spin->setSingleStep(0.1);
        return spin;
    }
    static void setWeight(const QJsonObject &obj, WeightFields fields) {
        fields.latency->setValue(obj.value("latency").toDouble(1.0));
        fields.success->setValue(obj.value("successful_connections").toDouble(0.2));
        fields.failure->setValue(obj.value("failed_connections").toDouble(1.0));
        fields.connections->setValue(obj.value("connection_count").toDouble(0.1));
        fields.ratio->setValue(obj.value("success_failure_ratio").toDouble(0.5));
    }
    static QJsonObject weightJson(WeightFields fields) {
        return QJsonObject{{"latency", fields.latency->value()},
            {"successful_connections", fields.success->value()},
            {"failed_connections", fields.failure->value()},
            {"connection_count", fields.connections->value()},
            {"success_failure_ratio", fields.ratio->value()}};
    }

    void loadConfig() {
        auto doc = request("GET", "/api/config", {}, true);
        if (!doc.isObject()) return;
        auto cfg = doc.object();
        setCombo(m_mode, cfg.value("mode").toString());
        m_apiBaseEdit->setText(cfg.value("proton_api_base").toString());
        m_proxyBind->setText(cfg.value("proxy_bind").toString());
        m_torExe->setText(cfg.value("tor_executable").toString());
        m_openVpnExe->setText(cfg.value("openvpn_executable").toString());
        m_wireguardGo->setText(cfg.value("wireguard_go_executable").toString());
        m_wgExe->setText(cfg.value("wg_executable").toString());
        m_stealthExe->setText(cfg.value("stealth_executable").toString());
        m_fallbackAttempts->setValue(cfg.value("fallback_attempts").toInt(5));
        setCombo(m_finalFallback, cfg.value("final_fallback").toString());
        m_pingInterval->setValue(cfg.value("ping_interval_seconds").toInt(10));
        m_externalCommands->setChecked(cfg.value("allow_external_tunnel_commands").toBool(false));
        QStringList countryCodes;
        for (const auto &country : cfg.value("countries").toArray()) countryCodes << country.toString();
        m_countries->setText(countryCodes.join(","));
        setWeight(cfg.value("priority").toObject(), m_normalWeights);
        auto bench = cfg.value("benchmark").toObject();
        m_benchmarkEnabled->setChecked(bench.value("enabled").toBool());
        m_benchmarkPing->setValue(bench.value("ping_interval_seconds").toInt(1));
        m_benchmarkRotate->setValue(bench.value("rotate_after_seconds").toInt(30));
        setWeight(bench.value("priority").toObject(), m_benchmarkWeights);
        populateProfiles(cfg.value("profiles").toArray());
        auto auth = request("GET", "/api/auth/status", {}, true).object();
        m_authLabel->setText(auth.value("present").toBool() ? "API session loaded" : "No API session loaded");
        populateServers(request("GET", "/api/servers", {}, true).array());
    }

    static void setCombo(QComboBox *combo, const QString &value) {
        const int index = combo->findData(value);
        if (index >= 0) combo->setCurrentIndex(index);
    }

    QJsonObject collectConfig() {
        auto existing = request("GET", "/api/config", {}, true).object();
        existing["mode"] = m_mode->currentData().toString();
        existing["proton_api_base"] = m_apiBaseEdit->text().trimmed();
        existing["proxy_bind"] = m_proxyBind->text().trimmed();
        existing["tor_executable"] = m_torExe->text().trimmed();
        existing["openvpn_executable"] = m_openVpnExe->text().trimmed();
        existing["wireguard_go_executable"] = m_wireguardGo->text().trimmed();
        existing["wg_executable"] = m_wgExe->text().trimmed();
        existing["stealth_executable"] = m_stealthExe->text().trimmed().isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m_stealthExe->text().trimmed());
        existing["fallback_attempts"] = m_fallbackAttempts->value();
        existing["final_fallback"] = m_finalFallback->currentData().toString();
        existing["ping_interval_seconds"] = m_pingInterval->value();
        existing["allow_external_tunnel_commands"] = m_externalCommands->isChecked();
        existing["priority"] = weightJson(m_normalWeights);
        QJsonArray countries;
        for (const QString &country : m_countries->text().split(',', Qt::SkipEmptyParts)) {
            auto code = country.trimmed().toUpper();
            if (!code.isEmpty()) countries.append(code);
        }
        existing["countries"] = countries;

        QJsonArray profiles = existing.value("profiles").toArray();
        for (int row = 0; row < m_profiles->rowCount(); ++row) {
            auto id = m_profiles->item(row, 0)->data(Qt::UserRole).toString();
            for (int i = 0; i < profiles.size(); ++i) {
                auto profile = profiles[i].toObject();
                if (profile.value("id").toString() == id) {
                    profile["enabled"] = m_profiles->item(row, 0)->checkState() == Qt::Checked;
                    profiles[i] = profile;
                    break;
                }
            }
        }
        existing["profiles"] = profiles;
        QJsonObject benchmark = existing.value("benchmark").toObject();
        benchmark["enabled"] = m_benchmarkEnabled->isChecked();
        benchmark["ping_interval_seconds"] = m_benchmarkPing->value();
        benchmark["rotate_after_seconds"] = m_benchmarkRotate->value();
        benchmark["priority"] = weightJson(m_benchmarkWeights);
        existing["benchmark"] = benchmark;
        return existing;
    }

    void saveConfig() {
        auto doc = request("PUT", "/api/config", collectConfig());
        if (!doc.isNull()) {
            m_status->setText("Settings saved. Active connections were not changed.");
            refreshStatus();
            loadProfilesAgain();
        }
    }

    void populateProfiles(const QJsonArray &profiles) {
        m_profiles->setRowCount(profiles.size());
        m_profileChoice->clear();
        for (int row = 0; row < profiles.size(); ++row) {
            auto p = profiles[row].toObject();
            auto *use = new QTableWidgetItem;
            use->setFlags(use->flags() | Qt::ItemIsUserCheckable);
            use->setCheckState(p.value("enabled").toBool(true) ? Qt::Checked : Qt::Unchecked);
            use->setData(Qt::UserRole, p.value("id").toString());
            m_profiles->setItem(row, 0, use);
            m_profiles->setItem(row, 1, new QTableWidgetItem(p.value("name").toString()));
            m_profiles->setItem(row, 2, new QTableWidgetItem(p.value("protocol").toString()));
            m_profiles->setItem(row, 3, new QTableWidgetItem(p.value("country_code").toString()));
            m_profiles->setItem(row, 4, new QTableWidgetItem(p.value("config_file").toString()));
            m_profileChoice->addItem(p.value("name").toString(), p.value("id").toString());
        }
    }

    void populateServers(const QJsonArray &servers) {
        m_servers->setRowCount(servers.size());
        for (int row = 0; row < servers.size(); ++row) {
            auto server = servers[row].toObject();
            QString protocols;
            QStringList ips;
            for (const auto &value : server.value("endpoints").toArray()) {
                auto endpoint = value.toObject();
                protocols += endpoint.value("protocol").toString() + " ";
                QString pair = endpoint.value("ip").toString();
                if (!endpoint.value("port").isNull() && endpoint.value("port").isDouble())
                    pair += ":" + QString::number(endpoint.value("port").toInt());
                ips << pair;
            }
            const QStringList cells{server.value("name").toString(), server.value("country").toString(),
                server.value("city").toString(), QString::number(server.value("load").toInt()),
                QString::number(server.value("tier").toInt()), protocols.trimmed(), ips.join(", ")};
            for (int col = 0; col < cells.size(); ++col) m_servers->setItem(row, col, new QTableWidgetItem(cells[col]));
        }
    }

    void loadProfilesAgain() {
        populateProfiles(request("GET", "/api/config", {}, true).object().value("profiles").toArray());
    }

    void importProfile() {
        const QString path = QFileDialog::getOpenFileName(this, "Import VPN configuration", QString(),
            "VPN profiles (*.ovpn *.conf);;All files (*.*)");
        if (path.isEmpty()) return;
        QJsonObject body{{"config_file", path}, {"protocol", m_importProtocol->currentData().toString()},
            {"country_code", m_importCountry->text().trimmed().toUpper()}};
        auto doc = request("POST", "/api/profiles/import", body);
        if (!doc.isNull()) loadConfig();
    }

    void connectProfile() {
        QJsonObject body;
        if (m_profileChoice->currentIndex() >= 0) body["profile_id"] = m_profileChoice->currentData().toString();
        auto doc = request("POST", "/api/connect", body);
        if (!doc.isNull()) refreshStatus();
    }

    void refreshStatus() {
        auto doc = request("GET", "/api/status", {}, true);
        if (!doc.isObject()) return;
        const auto status = doc.object();
        const auto active = status.value("active").toObject();
        const QString state = status.value("status").toString("unknown");
        const QString message = status.value("message").toString();
        m_status->setText(QString("%1  ·  %2").arg(state.toUpper(), message));
        const bool torRunning = status.value("tor_running").toBool();
        m_torStatus->setText(torRunning ? "Tor process running · SOCKS5 127.0.0.1:9050" : "Tor process is not running");
        m_authLabel->setText(status.value("api_session_present").toBool() ? "API session loaded (memory only)" : "No API session loaded");
        const QString activeName = active.value("profile_name").toString();
        m_activeConnection->setText(activeName.isEmpty() ? "—" : activeName);
        const QString mode = m_mode->currentData().toString();
        QString modeLabel = "Smart VPN";
        if (mode == "tor") modeLabel = "Tor Network";
        else if (mode == "lan") modeLabel = "Direct LAN";
        else if (mode == "open_vpn") modeLabel = "OpenVPN";
        else if (mode == "wire_guard") modeLabel = "WireGuard";
        else if (mode == "stealth") modeLabel = "Stealth";
        m_proxyModeValue->setText(modeLabel);
        const QString fallback = m_finalFallback->currentData().toString();
        m_fallbackValue->setText(fallback == "lan" ? "LAN Fallback" : "Tor Fallback");
        for (int i = 0; i < m_modeButtons.size(); ++i) {
            const QString selected = m_modeButtons[i]->property("mode").toString();
            m_modeButtons[i]->setChecked(selected == mode);
        }
        for (int i = 0; i < m_fallbackButtons.size(); ++i)
            m_fallbackButtons[i]->setChecked(m_fallbackButtons[i]->text().toLower() == fallback);
        m_headerDot->setStyleSheet(state == "connected" ? "color:#10b981; font-size:18pt;" :
            (state == "connecting" ? "color:#f59e0b; font-size:18pt;" : "color:#64748b; font-size:18pt;"));
        m_subtitle->setText(QString("Local proxy: %1  |  %2").arg(status.value("proxy_bind").toString(m_proxyAddress),
            torRunning ? "Tor running" : "Tor inactive"));
        if (!activeName.isEmpty()) m_status->setToolTip("Active route: " + activeName);
    }

    void refreshMetrics() {
        if (!m_metrics) return;
        const auto metrics = request("GET", "/api/metrics", {}, true).object();
        const auto profiles = request("GET", "/api/config", {}, true).object().value("profiles").toArray();
        QHash<QString, QString> names;
        for (const auto &item : profiles) {
            const auto profile = item.toObject();
            names.insert(profile.value("id").toString(), profile.value("name").toString());
        }
        m_metrics->setRowCount(metrics.size());
        int row = 0;
        for (auto it = metrics.begin(); it != metrics.end(); ++it, ++row) {
            const auto value = it.value().toObject();
            const QString name = names.value(it.key(), it.key());
            const auto average = value.value("average_ping_ms");
            const QString ping = average.isDouble() ? QString("%1 ms").arg(average.toDouble(), 0, 'f', 1) : "—";
            const QStringList cells{
                name,
                QString::number(value.value("connection_count").toVariant().toLongLong()),
                QString::number(value.value("successful_connections").toVariant().toLongLong()),
                QString::number(value.value("failed_connections").toVariant().toLongLong()),
                ping,
                QString::number(value.value("ping_count").toInt())
            };
            for (int column = 0; column < cells.size(); ++column) {
                auto *item = new QTableWidgetItem(cells[column]);
                if (column > 0) item->setTextAlignment(Qt::AlignCenter);
                m_metrics->setItem(row, column, item);
            }
            m_metrics->setRowHeight(row, 42);
        }
    }
};

int main(int argc, char **argv) {
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
        "--proxy-server=socks5://127.0.0.1:9050 --host-resolver-rules=MAP * ~NOTFOUND,EXCLUDE 127.0.0.1");
    QCoreApplication::setOrganizationName("Community");
    QCoreApplication::setApplicationName("ProtonVpnClient");
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    app.setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#020617"));
    palette.setColor(QPalette::WindowText, QColor("#f1f5f9"));
    palette.setColor(QPalette::Base, QColor("#0b1220"));
    palette.setColor(QPalette::AlternateBase, QColor("#111c31"));
    palette.setColor(QPalette::ToolTipBase, QColor("#0f172a"));
    palette.setColor(QPalette::ToolTipText, QColor("#f8fafc"));
    palette.setColor(QPalette::Text, QColor("#e2e8f0"));
    palette.setColor(QPalette::Button, QColor("#1e293b"));
    palette.setColor(QPalette::ButtonText, QColor("#e2e8f0"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Highlight, QColor("#4338ca"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    app.setPalette(palette);
    app.setStyleSheet(R"QSS(
        QWidget { color:#e2e8f0; background:#020617; font-family:"Segoe UI",Arial; font-size:9pt; }
        QMainWindow, QDialog { background:#020617; }
        QLabel { background:transparent; }
        QTabWidget::pane { background:#0f172a; border:1px solid #1e293b; border-radius:12px; top:-1px; }
        QTabBar::tab { color:#94a3b8; background:#0b1220; border:1px solid #1e293b; padding:11px 18px; margin-right:4px; border-top-left-radius:8px; border-top-right-radius:8px; }
        QTabBar::tab:selected { color:#fff; background:#312e81; border-color:#4338ca; }
        QTabBar::tab:hover:!selected { color:#f8fafc; background:#172033; }
        QPushButton { color:#e2e8f0; background:#1e293b; border:1px solid #334155; border-radius:8px; padding:8px 13px; min-height:18px; font-weight:600; }
        QPushButton:hover { background:#334155; border-color:#475569; }
        QPushButton:pressed { background:#4338ca; }
        QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox,QPlainTextEdit { color:#e2e8f0; background:#020617; selection-background-color:#4338ca; border:1px solid #334155; border-radius:8px; padding:8px 10px; min-height:18px; }
        QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus,QPlainTextEdit:focus { border:1px solid #818cf8; }
        QComboBox QAbstractItemView { color:#e2e8f0; background:#0f172a; border:1px solid #475569; selection-background-color:#4338ca; }
        QTableWidget { color:#e2e8f0; background:#0f172a; alternate-background-color:#111c31; border:1px solid #1e293b; gridline-color:#1e293b; selection-background-color:#312e81; }
        QTableWidget::item { padding:8px; border-bottom:1px solid #1e293b; }
        QHeaderView::section { color:#94a3b8; background:#0f172a; border:0; border-bottom:1px solid #334155; padding:10px; font-weight:600; }
        QScrollBar:vertical { background:#0b1220; width:10px; margin:2px; }
        QScrollBar::handle:vertical { background:#334155; min-height:24px; border-radius:5px; }
        QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical { height:0; }
        QCheckBox { spacing:8px; }
        QCheckBox::indicator { width:16px; height:16px; }
        QMenu { color:#e2e8f0; background:#0f172a; border:1px solid #334155; padding:5px; }
        QMenu::item { padding:8px 24px 8px 12px; border-radius:5px; }
        QMenu::item:selected { color:#fff; background:#3730a3; }
        QToolTip { color:#f8fafc; background:#0f172a; border:1px solid #475569; padding:5px; }
    )QSS");


    const QString api = qEnvironmentVariable("PROTON_CLIENT_API");
    const QString token = qEnvironmentVariable("PROTON_CLIENT_TOKEN");
    const QString proxy = qEnvironmentVariable("PROTON_CLIENT_PROXY", "127.0.0.1:8080");
    if (api.isEmpty() || token.isEmpty()) {
        QMessageBox::critical(nullptr, "Startup error", "The Rust supervisor did not provide the local API endpoint and access token.");
        return 2;
    }
    MainWindow window(api, token, proxy);
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, "System tray unavailable", "A system tray is required. The client will exit rather than opening a normal window.");
        return 3;
    }
    window.show();
    return app.exec();
}

#include "main.moc"
