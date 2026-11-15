#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

// "Status" tab: mode, benchmark toggle, connections, Tor/login status, log tail.
// Pure view: all actions are emitted as signals and handled by MainWindow.
class StatusTab : public QWidget
{
    Q_OBJECT
public:
    explicit StatusTab(QWidget* parent = nullptr);

public slots:
    void setState(const QJsonObject& state);
    void setUnreachable();

signals:
    void modeSelected(const QString& mode);
    void benchmarkToggled(bool enabled);
    void disconnectRequested(const QString& id);
    void loginRequested();
    void logoutRequested();

private:
    void rebuildConnections(const QJsonArray& conns);
    void fillConnectionRow(int row, const QJsonObject& c);

    QComboBox* m_mode = nullptr;
    QCheckBox* m_bench = nullptr;
    QLabel* m_login = nullptr;
    QLabel* m_tor = nullptr;
    QLabel* m_proxy = nullptr;
    QPushButton* m_loginBtn = nullptr;
    QPushButton* m_logoutBtn = nullptr;
    QTableWidget* m_conns = nullptr;
    QPlainTextEdit* m_log = nullptr;

    QStringList m_connIds;
    QString m_lastLog;
};
