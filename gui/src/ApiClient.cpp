#include "ApiClient.h"

#include <QJsonParseError>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <utility>

ApiClient::ApiClient(int port, const QString& token, QObject* parent)
    : QObject(parent)
    , m_port(port)
    , m_token(token.toUtf8())
{
    m_nam = new QNetworkAccessManager(this);
    // The GUI process routes Chromium through Tor, but the control API on
    // loopback must never be proxied.
    m_nam->setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
}

void ApiClient::get(const QString& path, QObject* ctx, Callback cb, int timeoutMs)
{
    send(Method::Get, path, QByteArray(), ctx, std::move(cb), timeoutMs);
}

void ApiClient::post(const QString& path, const QJsonObject& body, QObject* ctx, Callback cb, int timeoutMs)
{
    send(Method::Post, path, QJsonDocument(body).toJson(QJsonDocument::Compact), ctx, std::move(cb), timeoutMs);
}

void ApiClient::put(const QString& path, const QJsonObject& body, QObject* ctx, Callback cb, int timeoutMs)
{
    const bool isConfig = (path == QLatin1String("/config"));
    send(Method::Put, path, QJsonDocument(body).toJson(QJsonDocument::Compact), ctx,
         [this, isConfig, cb](const Response& r) {
             if (isConfig && r.ok) {
                 emit configChanged();
             }
             if (cb) {
                 cb(r);
             }
         },
         timeoutMs);
}

void ApiClient::send(Method method, const QString& path, const QByteArray& body, QObject* ctx, Callback cb, int timeoutMs)
{
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_port).arg(path)));
    req.setRawHeader("X-Token", m_token);
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(timeoutMs);

    QNetworkReply* reply = nullptr;
    switch (method) {
    case Method::Get:
        reply = m_nam->get(req);
        break;
    case Method::Post:
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = m_nam->post(req, body);
        break;
    case Method::Put:
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = m_nam->put(req, body);
        break;
    }
    if (!reply) {
        return;
    }

    // Always free the reply, even if the receiver object is gone.
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);

    QObject* receiver = ctx ? ctx : this;
    connect(reply, &QNetworkReply::finished, receiver, [reply, cb]() {
        Response r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        if (!data.isEmpty()) {
            r.doc = QJsonDocument::fromJson(data);
        }
        if (r.status == 0) {
            r.networkError = true;
            r.error = reply->errorString();
        } else if (r.status >= 400) {
            if (r.doc.isObject()) {
                r.error = r.doc.object().value(QStringLiteral("error")).toString();
            }
            if (r.error.isEmpty()) {
                r.error = QStringLiteral("HTTP %1").arg(r.status);
            }
        } else {
            r.ok = true;
        }
        if (cb) {
            cb(r);
        }
    });
}

void ApiClient::modifyConfig(QObject* ctx, ConfigModifier modifier, ConfigDone done)
{
    ConfigJob job;
    job.ctx = ctx;
    job.hadCtx = (ctx != nullptr);
    job.modifier = std::move(modifier);
    job.done = std::move(done);
    m_jobs.enqueue(std::move(job));
    runNextJob();
}

void ApiClient::runNextJob()
{
    if (m_jobRunning || m_jobs.isEmpty()) {
        return;
    }
    m_jobRunning = true;
    const ConfigJob job = m_jobs.dequeue();

    get(QStringLiteral("/config"), this, [this, job](const Response& r) {
        if (!r.ok || !r.doc.isObject()) {
            finishJob(job, false,
                      r.error.isEmpty() ? QStringLiteral("could not read config") : r.error);
            return;
        }
        QJsonObject cfg = r.doc.object();
        if (!job.modifier || !job.modifier(cfg)) {
            finishJob(job, true, QString());
            return;
        }
        put(QStringLiteral("/config"), cfg, this, [this, job](const Response& pr) {
            finishJob(job, pr.ok, pr.error);
        });
    });
}

void ApiClient::finishJob(const ConfigJob& job, bool ok, const QString& error)
{
    m_jobRunning = false;
    if (job.done && (!job.hadCtx || !job.ctx.isNull())) {
        job.done(ok, error);
    }
    runNextJob();
}
