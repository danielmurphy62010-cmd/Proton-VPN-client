#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QString>

#include <functional>

// Thin async client for the Rust core's control API (see docs/control-api.md).
// All requests go to http://127.0.0.1:<port> with header X-Token, and never use a proxy.
class ApiClient : public QObject
{
    Q_OBJECT
public:
    struct Response {
        bool ok = false;           // HTTP 2xx
        bool networkError = false; // no HTTP response at all (refused, timeout, ...)
        int status = 0;            // HTTP status, 0 when networkError
        QJsonDocument doc;         // parsed body (may be null)
        QString error;             // error text for !ok
    };

    using Callback = std::function<void(const Response&)>;
    // Return false from a modifier to skip the PUT (nothing to change).
    using ConfigModifier = std::function<bool(QJsonObject&)>;
    using ConfigDone = std::function<void(bool ok, const QString& error)>;

    ApiClient(int port, const QString& token, QObject* parent = nullptr);

    // `ctx` is the receiver object: the callback is dropped if it is destroyed
    // before the reply arrives. Pass a non-null widget/object that owns the request.
    void get(const QString& path, QObject* ctx, Callback cb, int timeoutMs = 5000);
    void post(const QString& path, const QJsonObject& body, QObject* ctx, Callback cb, int timeoutMs = 8000);
    void put(const QString& path, const QJsonObject& body, QObject* ctx, Callback cb, int timeoutMs = 15000);

    // Serialized read-modify-write of the whole config (GET /config, modifier, PUT /config).
    // Jobs run strictly one after another so rapid edits cannot overwrite each other.
    void modifyConfig(QObject* ctx, ConfigModifier modifier, ConfigDone done);

signals:
    // Emitted after a successful PUT /config.
    void configChanged();

private:
    enum class Method { Get, Post, Put };

    struct ConfigJob {
        QPointer<QObject> ctx;
        bool hadCtx = false;
        ConfigModifier modifier;
        ConfigDone done;
    };

    void send(Method method, const QString& path, const QByteArray& body, QObject* ctx, Callback cb, int timeoutMs);
    void runNextJob();
    void finishJob(const ConfigJob& job, bool ok, const QString& error);

    QNetworkAccessManager* m_nam = nullptr;
    int m_port;
    QByteArray m_token;
    QQueue<ConfigJob> m_jobs;
    bool m_jobRunning = false;
};
