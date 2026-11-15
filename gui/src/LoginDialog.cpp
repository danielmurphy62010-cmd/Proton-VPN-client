#include "LoginDialog.h"

#include "ApiClient.h"
#include "Common.h"

#include <QDir>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineCookieStore>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace {
const char* const kDefaultLoginUrl = "https://account.protonvpn.com/login";
}

LoginDialog::LoginDialog(ApiClient* api, QWidget* parent)
    : QDialog(parent)
    , m_api(api)
{
    setWindowTitle(tr("Log in to Proton"));
    resize(1000, 780);

    // Persistent profile with its own storage, separate from anything else on the machine.
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/.pvc-gui");
    }
    base += QStringLiteral("/webprofile");
    QDir().mkpath(base);
    QDir().mkpath(base + QStringLiteral("/cache"));

    // No parent on purpose: the profile has to outlive the page and is deleted by hand in
    // the destructor, after the view (which owns the page).
    m_profile = new QWebEngineProfile(QStringLiteral("pvc-login"), nullptr);
    m_profile->setPersistentStoragePath(base);
    m_profile->setCachePath(base + QStringLiteral("/cache"));
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    // Do not expose local interface addresses through WebRTC.
    m_profile->settings()->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);

    auto* lay = new QVBoxLayout(this);

    m_torLabel = new QLabel(
        tr("All traffic of this window is routed through Tor (SOCKS5 127.0.0.1:%1). "
           "If Tor is not running or not bootstrapped yet, pages will fail to load.")
            .arg(Common::torSocksPort()),
        this);
    m_torLabel->setWordWrap(true);
    lay->addWidget(m_torLabel);

    m_view = new QWebEngineView(this);
    m_page = new QWebEnginePage(m_profile, m_view);
    m_view->setPage(m_page);
    lay->addWidget(m_view, 1);

    auto* bottom = new QHBoxLayout;
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    bottom->addWidget(m_status, 1);

    m_retryBtn = new QPushButton(tr("Retry sending session"), this);
    m_retryBtn->setVisible(false);
    bottom->addWidget(m_retryBtn);

    auto* clearBtn = new QPushButton(tr("Clear browser cookies"), this);
    bottom->addWidget(clearBtn);

    auto* closeBtn = new QPushButton(tr("Close"), this);
    bottom->addWidget(closeBtn);
    lay->addLayout(bottom);

    setStatus(tr("Waiting for login. The session is sent to the core automatically once the "
                 "Proton cookies appear."),
              false);

    connect(m_profile->cookieStore(), &QWebEngineCookieStore::cookieAdded, this, &LoginDialog::onCookieAdded);
    connect(m_view, &QWebEngineView::loadFinished, this, [this](bool ok) {
        if (!ok && m_view->url() != QUrl(QStringLiteral("about:blank"))) {
            setStatus(tr("The page failed to load. Check that Tor is running and bootstrapped."), true);
        }
    });
    connect(m_retryBtn, &QPushButton::clicked, this, [this]() {
        m_lastSubmittedKey.clear();
        trySubmit();
    });
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        clearBrowserData();
        setStatus(tr("Browser cookies cleared."), false);
        loadUrl(QString::fromLatin1(kDefaultLoginUrl));
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
}

LoginDialog::~LoginDialog()
{
    // Order matters: view (and its page) first, then the profile.
    delete m_view;
    m_view = nullptr;
    m_page = nullptr;
    delete m_profile;
    m_profile = nullptr;
}

void LoginDialog::setStatus(const QString& text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? QStringLiteral("color: #cf222e;") : QString());
}

void LoginDialog::startLogin()
{
    m_retryBtn->setVisible(false);
    setStatus(tr("Waiting for login. The session is sent to the core automatically once the "
                 "Proton cookies appear."),
              false);

    show();
    raise();
    activateWindow();

    // Cookies that are already stored in the persistent profile are reported through cookieAdded.
    m_profile->cookieStore()->loadAllCookies();

    m_api->get(QStringLiteral("/config"), this, [this](const ApiClient::Response& r) {
        QString url = QString::fromLatin1(kDefaultLoginUrl);
        if (r.ok && r.doc.isObject()) {
            const QString configured =
                Common::getPath(r.doc.object(), QStringLiteral("proton.login_url")).toString().trimmed();
            if (!configured.isEmpty()) {
                url = configured;
            }
        }
        loadUrl(url);
    });
}

void LoginDialog::loadUrl(const QString& url)
{
    QUrl u = QUrl::fromUserInput(url);
    const QString scheme = u.scheme().toLower();
    if (!u.isValid() || (scheme != QLatin1String("https") && scheme != QLatin1String("http"))) {
        u = QUrl(QString::fromLatin1(kDefaultLoginUrl));
    }
    m_view->load(u);
}

void LoginDialog::clearBrowserData()
{
    m_profile->cookieStore()->deleteAllCookies();
    m_profile->clearHttpCache();
    m_uid.clear();
    m_auth.clear();
    m_sessionId.clear();
    m_lastSubmittedKey.clear();
}

void LoginDialog::done(int result)
{
    // Stop whatever the page is doing while the dialog is closed.
    if (m_view) {
        m_view->load(QUrl(QStringLiteral("about:blank")));
    }
    QDialog::done(result);
}

void LoginDialog::onCookieAdded(const QNetworkCookie& cookie)
{
    const QString name = QString::fromUtf8(cookie.name());
    const QString value = QString::fromUtf8(cookie.value());

    if (name.startsWith(QLatin1String("AUTH-")) && name.size() > 5) {
        m_uid = name.mid(5);
        m_auth = value;
    } else if (name == QLatin1String("Session-Id")) {
        m_sessionId = value;
    } else {
        return;
    }
    trySubmit();
}

void LoginDialog::trySubmit()
{
    if (m_submitting || m_uid.isEmpty() || m_auth.isEmpty() || m_sessionId.isEmpty()) {
        return;
    }
    const QString key = m_uid + QLatin1Char('|') + m_auth + QLatin1Char('|') + m_sessionId;
    if (key == m_lastSubmittedKey) {
        return; // this exact session was already sent; do not loop on stale cookies
    }
    m_lastSubmittedKey = key;
    m_submitting = true;
    m_retryBtn->setVisible(false);
    setStatus(tr("Login detected, sending the session to the core..."), false);

    QJsonObject body;
    body.insert(QStringLiteral("uid"), m_uid);
    body.insert(QStringLiteral("auth"), m_auth);
    body.insert(QStringLiteral("session_id"), m_sessionId);

    m_api->post(QStringLiteral("/auth/cookies"), body, this, [this](const ApiClient::Response& r) {
        m_submitting = false;
        if (r.ok) {
            setStatus(tr("Session accepted."), false);
            emit loggedIn();
            accept();
        } else {
            m_lastSubmittedKey.clear();
            m_retryBtn->setVisible(true);
            setStatus(tr("The core rejected the session: %1").arg(r.error), true);
        }
    });
}
