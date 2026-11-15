#pragma once

#include <QChar>
#include <QtGlobal>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

// Small shared helpers: mode table and JSON path utilities.
namespace Common {

struct ModeInfo {
    QString id;
    QString label;
};

inline const QList<ModeInfo>& modes()
{
    static const QList<ModeInfo> list = {
        {QStringLiteral("smart"), QStringLiteral("Smart")},
        {QStringLiteral("ovpn"), QStringLiteral("OVPN")},
        {QStringLiteral("wireguard"), QStringLiteral("WireGuard")},
        {QStringLiteral("stealth"), QStringLiteral("Stealth")},
        {QStringLiteral("tor"), QStringLiteral("Tor")},
        {QStringLiteral("lan"), QStringLiteral("LAN")},
    };
    return list;
}

// SOCKS port of the Tor instance, from PVC_TOR_SOCKS_PORT (default 9050).
inline int torSocksPort()
{
    bool ok = false;
    const int p = qEnvironmentVariableIntValue("PVC_TOR_SOCKS_PORT", &ok);
    return (ok && p > 0 && p <= 65535) ? p : 9050;
}

// Read a value by dotted path ("normal.profiles.include"). Returns Undefined if missing.
inline QJsonValue getPath(const QJsonObject& root, const QString& dotted)
{
    const QStringList parts = dotted.split(QChar('.'), Qt::SkipEmptyParts);
    QJsonValue cur(root);
    for (const QString& p : parts) {
        if (!cur.isObject()) {
            return QJsonValue(QJsonValue::Undefined);
        }
        cur = cur.toObject().value(p);
    }
    return cur;
}

inline void setPathRec(QJsonObject& obj, const QStringList& parts, int idx, const QJsonValue& v)
{
    const QString& key = parts.at(idx);
    if (idx == parts.size() - 1) {
        obj.insert(key, v);
        return;
    }
    QJsonObject child = obj.value(key).toObject();
    setPathRec(child, parts, idx + 1, v);
    obj.insert(key, child);
}

// Write a value by dotted path, creating intermediate objects as needed.
inline void setPath(QJsonObject& root, const QString& dotted, const QJsonValue& v)
{
    const QStringList parts = dotted.split(QChar('.'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return;
    }
    setPathRec(root, parts, 0, v);
}

inline QStringList toStringList(const QJsonValue& v)
{
    QStringList out;
    const QJsonArray arr = v.toArray();
    for (const QJsonValue& e : arr) {
        if (e.isString()) {
            out << e.toString();
        }
    }
    return out;
}

inline QJsonArray fromStringList(const QStringList& list)
{
    QJsonArray arr;
    for (const QString& s : list) {
        arr.append(s);
    }
    return arr;
}

// Ensure `id` is (present=true) or is not (present=false) in the string array at `dotted`.
inline void setIdInList(QJsonObject& root, const QString& dotted, const QString& id, bool present)
{
    const QJsonArray arr = getPath(root, dotted).toArray();
    QJsonArray out;
    for (const QJsonValue& v : arr) {
        if (v.isString() && v.toString() == id) {
            continue;
        }
        out.append(v);
    }
    if (present) {
        out.append(id);
    }
    setPath(root, dotted, out);
}

// Human readable rendering of a scalar (or compact JSON for containers).
inline QString display(const QJsonValue& v)
{
    switch (v.type()) {
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        return QString();
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("yes") : QStringLiteral("no");
    case QJsonValue::Double:
        return QString::number(v.toDouble(), 'g', 12);
    case QJsonValue::String:
        return v.toString();
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    }
    return QString();
}

} // namespace Common
