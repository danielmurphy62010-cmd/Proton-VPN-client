#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QTimer>
#include <QWidget>

class ApiClient;
class QShowEvent;
class QTableWidget;

// "Stats" tab: read-only tables of GET /stats for the normal and the benchmark config.
class StatsTab : public QWidget
{
    Q_OBJECT
public:
    explicit StatsTab(ApiClient* api, QWidget* parent = nullptr);

public slots:
    void refresh();

signals:
    void message(const QString& text);

protected:
    void showEvent(QShowEvent* event) override;

private:
    static QTableWidget* makeTable(QWidget* parent);
    static void fillTable(QTableWidget* table, const QJsonObject& stats, QByteArray& lastSignature);

    ApiClient* m_api;
    QTableWidget* m_normal = nullptr;
    QTableWidget* m_bench = nullptr;
    QByteArray m_sigNormal;
    QByteArray m_sigBench;
    QTimer m_timer;
    bool m_refreshing = false;
};
