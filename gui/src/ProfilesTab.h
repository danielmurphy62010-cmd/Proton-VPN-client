#pragma once

#include <QByteArray>
#include <QGroupBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class ApiClient;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

// Countries sub-panel: checkable country list editing <base>.countries.list and
// <base>.countries.mode for the normal or the benchmark config.
class CountriesPanel : public QGroupBox
{
    Q_OBJECT
public:
    CountriesPanel(ApiClient* api, QWidget* parent = nullptr);

public slots:
    void reload();

signals:
    void message(const QString& text);

protected:
    void showEvent(QShowEvent* event) override;

private:
    QString basePath() const;
    void populate();
    void applyFilter();
    void apply();
    void setDirty(bool dirty);

    ApiClient* m_api;
    QComboBox* m_target = nullptr;
    QComboBox* m_mode = nullptr;
    QLineEdit* m_filter = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_applyBtn = nullptr;

    QStringList m_countries;
    QJsonObject m_config;
    bool m_dirty = false;
    bool m_filling = false;
};

// "Profiles" tab: profile table, connect/disconnect, include/exclude editing,
// plus the countries sub-panel.
class ProfilesTab : public QWidget
{
    Q_OBJECT
public:
    ProfilesTab(ApiClient* api, QWidget* parent = nullptr);

public slots:
    void refresh();

signals:
    void message(const QString& text);

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void onItemChanged(QTableWidgetItem* item);

private:
    QString listPath() const;       // e.g. "normal.profiles.include"
    QString listKind() const;       // "include" or "exclude"
    bool targetIsBenchmark() const;
    QStringList selectedIds() const;
    void rebuildTable();
    void applyFilter();
    void actOnSelected(const QString& apiPath);

    ApiClient* m_api;
    QLineEdit* m_filter = nullptr;
    QComboBox* m_cfgTarget = nullptr;
    QComboBox* m_listKind = nullptr;
    QTableWidget* m_table = nullptr;
    CountriesPanel* m_countries = nullptr;
    QTimer m_timer;

    QJsonArray m_profiles;
    QJsonObject m_config;
    QByteArray m_lastSignature;
    bool m_filling = false;
    bool m_refreshing = false;
};
