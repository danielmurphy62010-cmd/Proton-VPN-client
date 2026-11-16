#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QEventLoop>
#include <QIcon>
#include <QStyle>
#include <QStringList>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
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
        auto *appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
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
        resize(1080, 760);
        buildUi();
        buildTray();
        loadConfig();
        refreshStatus();
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this] { refreshStatus(); });
        m_timer->start(2500);
        hide();
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
    bool m_firstTrayOpen{true};

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
        auto *tabs = new QTabWidget(this);
        setCentralWidget(tabs);

        auto *dashboard = new QWidget;
        auto *d = new QVBoxLayout(dashboard);
        m_status = new QLabel("Starting...");
        m_status->setStyleSheet("font-size: 20px; font-weight: 600;");
        m_torStatus = new QLabel("Tor: starting");
        d->addWidget(m_status);
        d->addWidget(m_torStatus);
        auto *modeRow = new QHBoxLayout;
        modeRow->addWidget(new QLabel("Mode"));
        m_mode = new QComboBox;
        addMode(m_mode, "Smart", "smart");
        addMode(m_mode, "OpenVPN", "open_vpn");
        addMode(m_mode, "WireGuard", "wire_guard");
        addMode(m_mode, "Stealth", "stealth");
        addMode(m_mode, "Tor", "tor");
        addMode(m_mode, "LAN (no VPN)", "lan");
        modeRow->addWidget(m_mode, 1);
        modeRow->addWidget(new QLabel("Profile"));
        m_profileChoice = new QComboBox;
        modeRow->addWidget(m_profileChoice, 2);
        d->addLayout(modeRow);
        auto *buttons = new QHBoxLayout;
        auto *connectButton = new QPushButton("Connect");
        auto *disconnectButton = new QPushButton("Disconnect");
        auto *saveButton = new QPushButton("Save settings");
        buttons->addWidget(connectButton);
        buttons->addWidget(disconnectButton);
        buttons->addWidget(saveButton);
        d->addLayout(buttons);
        d->addWidget(new QLabel(QString("Local HTTP proxy: ") + m_proxyAddress));
        d->addWidget(new QLabel("The proxy is explicit and loopback-only. It does not provide transparent system-wide VPN routing."));
        buttons->addStretch();
        connect(connectButton, &QPushButton::clicked, this, [this] { connectProfile(); });
        connect(disconnectButton, &QPushButton::clicked, this, [this] {
            request("POST", "/api/disconnect");
            refreshStatus();
        });
        connect(saveButton, &QPushButton::clicked, this, [this] { saveConfig(); });
        tabs->addTab(dashboard, "Overview");

        auto *profilePage = new QWidget;
        auto *p = new QVBoxLayout(profilePage);
        m_profiles = new QTableWidget(0, 5);
        m_profiles->setHorizontalHeaderLabels({"Use", "Profile", "Protocol", "Country", "Config file"});
        m_profiles->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        m_profiles->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        p->addWidget(new QLabel("Imported tunnel profiles"));
        p->addWidget(m_profiles, 1);
        auto *importRow = new QHBoxLayout;
        m_importProtocol = new QComboBox;
        addMode(m_importProtocol, "OpenVPN", "open_vpn");
        addMode(m_importProtocol, "WireGuard", "wire_guard");
        addMode(m_importProtocol, "Stealth (external backend only)", "stealth");
        m_importCountry = new QLineEdit;
        m_importCountry->setPlaceholderText("Country code (e.g. US)");
        auto *importButton = new QPushButton("Import config file...");
        importRow->addWidget(m_importProtocol);
        importRow->addWidget(m_importCountry);
        importRow->addWidget(importButton);
        p->addLayout(importRow);
        connect(importButton, &QPushButton::clicked, this, [this] { importProfile(); });
        auto *saveProfiles = new QPushButton("Save profile selection");
        auto *removeProfile = new QPushButton("Remove selected profile");
        p->addWidget(saveProfiles);
        p->addWidget(removeProfile);
        connect(saveProfiles, &QPushButton::clicked, this, [this] { saveConfig(); });
        connect(removeProfile, &QPushButton::clicked, this, [this] {
            const int row = m_profiles->currentRow();
            if (row < 0 || !m_profiles->item(row, 0)) return;
            const QString id = m_profiles->item(row, 0)->data(Qt::UserRole).toString();
            if (id.isEmpty()) return;
            request("POST", "/api/profiles/remove", QJsonObject{{"profile_id", id}});
            loadConfig();
        });
        tabs->addTab(profilePage, "Profiles");

        auto *serversPage = new QWidget;
        auto *s = new QVBoxLayout(serversPage);
        m_servers = new QTableWidget(0, 7);
        m_servers->setHorizontalHeaderLabels({"Server", "Country", "City", "Load", "Tier", "Protocol", "Entry IP"});
        m_servers->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_servers->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
        s->addWidget(m_servers, 1);
        auto *serverButtons = new QHBoxLayout;
        auto *refreshButton = new QPushButton("Refresh Proton server list");
        serverButtons->addWidget(refreshButton);
        serverButtons->addStretch();
        s->addLayout(serverButtons);
        connect(refreshButton, &QPushButton::clicked, this, [this] {
            auto doc = request("POST", "/api/servers/refresh");
            if (!doc.isNull()) populateServers(doc.array());
        });
        tabs->addTab(serversPage, "Proton servers");

        auto *settingsPage = new QWidget;
        auto *form = new QFormLayout(settingsPage);
        m_apiBaseEdit = new QLineEdit;
        m_proxyBind = new QLineEdit;
        m_torExe = new QLineEdit;
        m_openVpnExe = new QLineEdit;
        m_wireguardGo = new QLineEdit;
        m_wgExe = new QLineEdit;
        m_stealthExe = new QLineEdit;
        m_countries = new QLineEdit;
        m_fallbackAttempts = new QSpinBox; m_fallbackAttempts->setRange(1,20);
        m_finalFallback = new QComboBox; m_finalFallback->addItem("Tor","tor"); m_finalFallback->addItem("LAN / direct","lan");
        m_pingInterval = new QSpinBox; m_pingInterval->setRange(1,3600);
        m_externalCommands = new QCheckBox("Allow external tunnel executables (may require preinstalled drivers / elevation)");
        form->addRow("Proton API base URL", m_apiBaseEdit);
        form->addRow("Proxy bind (loopback only)", m_proxyBind);
        form->addRow("Tor executable", m_torExe);
        form->addRow("OpenVPN executable", m_openVpnExe);
        form->addRow("wireguard-go executable", m_wireguardGo);
        form->addRow("wg.exe executable", m_wgExe);
        form->addRow("Optional Stealth executable", m_stealthExe);
        form->addRow("Allowed country codes (comma-separated; empty = all)", m_countries);
        form->addRow("Fallback profile attempts (1–20)", m_fallbackAttempts);
        form->addRow("Final fallback", m_finalFallback);
        form->addRow("Normal measurement interval (seconds)", m_pingInterval);
        form->addRow(m_externalCommands);
        form->addRow(new QLabel("Priority weights"));
        form->addRow("Latency weight", m_normalWeights.latency = makeWeight());
        form->addRow("Successful connections weight", m_normalWeights.success = makeWeight());
        form->addRow("Failed connections weight", m_normalWeights.failure = makeWeight());
        form->addRow("Connection count weight", m_normalWeights.connections = makeWeight());
        form->addRow("Success/failure ratio weight", m_normalWeights.ratio = makeWeight());
        auto *saveSettings = new QPushButton("Save all settings");
        form->addRow(saveSettings);
        connect(saveSettings, &QPushButton::clicked, this, [this] { saveConfig(); });
        tabs->addTab(settingsPage, "Settings");

        auto *benchPage = new QWidget;
        auto *bench = new QFormLayout(benchPage);
        m_benchmarkEnabled = new QCheckBox("Enable benchmark mode");
        m_benchmarkPing = new QSpinBox; m_benchmarkPing->setRange(1,3600);
        m_benchmarkRotate = new QSpinBox; m_benchmarkRotate->setRange(1,3600);
        bench->addRow(m_benchmarkEnabled);
        bench->addRow("Measurement interval (seconds)", m_benchmarkPing);
        bench->addRow("Random profile rotation interval (seconds)", m_benchmarkRotate);
        bench->addRow(new QLabel("Benchmark priority weights"));
        bench->addRow("Latency weight", m_benchmarkWeights.latency = makeWeight());
        bench->addRow("Successful connections weight", m_benchmarkWeights.success = makeWeight());
        bench->addRow("Failed connections weight", m_benchmarkWeights.failure = makeWeight());
        bench->addRow("Connection count weight", m_benchmarkWeights.connections = makeWeight());
        bench->addRow("Success/failure ratio weight", m_benchmarkWeights.ratio = makeWeight());
        auto *saveBench = new QPushButton("Save benchmark settings");
        bench->addRow(saveBench);
        connect(saveBench, &QPushButton::clicked, this, [this] { saveConfig(); });
        tabs->addTab(benchPage, "Benchmark");

        auto *loginPage = new QWidget;
        auto *login = new QFormLayout(loginPage);
        m_authLabel = new QLabel("No API session loaded");
        m_accessToken = new QLineEdit; m_accessToken->setEchoMode(QLineEdit::Password);
        m_uid = new QLineEdit;
        auto *saveSession = new QPushButton("Use API access token + UID (memory only)");
        auto *openBrowser = new QPushButton("Open Proton login through Tor");
        login->addRow(m_authLabel);
        login->addRow("API access token", m_accessToken);
        login->addRow("API session UID", m_uid);
        login->addRow(saveSession);
        login->addRow(openBrowser);
        login->addRow(new QLabel("The mini-browser retains website cookies in its Qt WebEngine profile. Proton's API server-list request uses a separate Bearer access token and x-pm-uid header; cookies alone are not treated as API credentials."));
        connect(saveSession, &QPushButton::clicked, this, [this] {
            QJsonObject body{{"access_token", m_accessToken->text()}, {"uid", m_uid->text()}};
            auto doc = request("POST", "/api/auth/session", body);
            if (!doc.isNull()) { m_authLabel->setText("API session loaded (memory only)"); m_accessToken->clear(); }
        });
        connect(openBrowser, &QPushButton::clicked, this, [this] { openTorBrowser(); });
        tabs->addTab(loginPage, "Login / API");

        auto *countriesPage = new QWidget;
        auto *c = new QVBoxLayout(countriesPage);
        c->addWidget(new QLabel("Allowed countries are configured in Settings. Current server data is retrieved from Proton's authenticated logical-server API."));
        tabs->addTab(countriesPage, "Help");
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
        show();
        raise();
        activateWindow();
        if (m_firstTrayOpen) {
            m_firstTrayOpen = false;
            auto status = request("GET", "/api/auth/status", {}, true).object();
            if (!status.value("present").toBool()) QTimer::singleShot(250, this, [this] { openTorBrowser(); });
        }
    }

    void openTorBrowser() {
        auto *dialog = new TorLoginDialog(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->exec();
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
        auto status = doc.object();
        const auto active = status.value("active").toObject();
        m_status->setText(QString("%1 — %2").arg(status.value("status").toString(), status.value("message").toString()));
        m_torStatus->setText(status.value("tor_running").toBool() ? "Tor: running (127.0.0.1:9050)" : "Tor: not running");
        m_authLabel->setText(status.value("api_session_present").toBool() ? "API session loaded" : "No API session loaded");
        if (!active.isEmpty() && !active.value("profile_name").toString().isEmpty())
            m_status->setToolTip("Active route: " + active.value("profile_name").toString());
    }
};

int main(int argc, char **argv) {
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS",
        "--proxy-server=socks5://127.0.0.1:9050 --host-resolver-rules=MAP * ~NOTFOUND,EXCLUDE 127.0.0.1");
    QCoreApplication::setOrganizationName("Community");
    QCoreApplication::setApplicationName("ProtonVpnClient");
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

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
    return app.exec();
}

#include "main.moc"
