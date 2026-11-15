#pragma once

#include <QDialog>
#include <QNetworkCookie>
#include <QString>

class ApiClient;
class QLabel;
class QPushButton;
class QWebEnginePage;
class QWebEngineProfile;
class QWebEngineView;

// Embedded browser used to log in to Proton. The whole process routes Chromium traffic
// through Tor (see main.cpp). When both the AUTH-<uid> cookie and the Session-Id cookie
// have been seen, they are sent to the core with POST /auth/cookies and the dialog closes.
class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LoginDialog(ApiClient* api, QWidget* parent = nullptr);
    ~LoginDialog() override;

    // Reads proton.login_url from the config (default https://account.protonvpn.com/login),
    // loads it and shows the dialog.
    void startLogin();

    // Deletes all cookies of the persistent web profile (used after logout).
    void clearBrowserData();

signals:
    void loggedIn();

public slots:
    void done(int result) override;

private slots:
    void onCookieAdded(const QNetworkCookie& cookie);

private:
    void loadUrl(const QString& url);
    void trySubmit();
    void setStatus(const QString& text, bool error);

    ApiClient* m_api;
    QWebEngineProfile* m_profile = nullptr;
    QWebEngineView* m_view = nullptr;
    QWebEnginePage* m_page = nullptr;
    QLabel* m_torLabel = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_retryBtn = nullptr;

    QString m_uid;
    QString m_auth;
    QString m_sessionId;
    bool m_submitting = false;
    QString m_lastSubmittedKey; // uid|auth|session of the last accepted/attempted submission
};
