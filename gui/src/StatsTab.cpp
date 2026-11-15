#include "StatsTab.h"

#include "ApiClient.h"
#include "Common.h"

#include <QAbstractItemView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QSplitter>
#include <QStringList>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace {

QString headerFor(const QString& key)
{
    if (key == QLatin1String("avg_ping_ms")) {
        return QObject::tr("Ping avg (ms)");
    }
    if (key == QLatin1String("ping_count")) {
        return QObject::tr("Ping samples");
    }
    if (key == QLatin1String("successes")) {
        return QObject::tr("Successes");
    }
    if (key == QLatin1String("failures")) {
        return QObject::tr("Failures");
    }
    if (key == QLatin1String("connections")) {
        return QObject::tr("Connections");
    }
    return key;
}

QVariant numberVariant(double d, bool oneDecimal)
{
    if (oneDecimal) {
        return QVariant(std::round(d * 10.0) / 10.0);
    }
    if (std::floor(d) == d && std::fabs(d) < 9e15) {
        return QVariant(static_cast<qlonglong>(d));
    }
    return QVariant(std::round(d * 1000.0) / 1000.0);
}

} // namespace

StatsTab::StatsTab(ApiClient* api, QWidget* parent)
    : QWidget(parent)
    , m_api(api)
{
    auto* root = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    top->addStretch(1);
    auto* refreshBtn = new QPushButton(tr("Refresh"), this);
    top->addWidget(refreshBtn);
    root->addLayout(top);

    auto* split = new QSplitter(Qt::Vertical, this);

    auto* normalBox = new QGroupBox(tr("Normal"), split);
    auto* normalLay = new QVBoxLayout(normalBox);
    m_normal = makeTable(normalBox);
    normalLay->addWidget(m_normal);

    auto* benchBox = new QGroupBox(tr("Benchmark"), split);
    auto* benchLay = new QVBoxLayout(benchBox);
    m_bench = makeTable(benchBox);
    benchLay->addWidget(m_bench);

    split->addWidget(normalBox);
    split->addWidget(benchBox);
    root->addWidget(split, 1);

    connect(refreshBtn, &QPushButton::clicked, this, [this]() { refresh(); });

    m_timer.setInterval(5000);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            refresh();
        }
    });
    m_timer.start();
}

QTableWidget* StatsTab::makeTable(QWidget* parent)
{
    auto* t = new QTableWidget(0, 0, parent);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setAlternatingRowColors(true);
    return t;
}

void StatsTab::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refresh();
}

void StatsTab::refresh()
{
    if (m_refreshing) {
        return;
    }
    m_refreshing = true;
    m_api->get(QStringLiteral("/stats"), this, [this](const ApiClient::Response& r) {
        m_refreshing = false;
        if (!r.ok || !r.doc.isObject()) {
            if (!r.networkError) {
                emit message(tr("Could not load stats: %1").arg(r.error));
            }
            return;
        }
        const QJsonObject o = r.doc.object();
        fillTable(m_normal, o.value(QStringLiteral("normal")).toObject(), m_sigNormal);
        fillTable(m_bench, o.value(QStringLiteral("benchmark")).toObject(), m_sigBench);
    });
}

void StatsTab::fillTable(QTableWidget* table, const QJsonObject& stats, QByteArray& lastSignature)
{
    const QByteArray sig = QJsonDocument(stats).toJson(QJsonDocument::Compact);
    if (sig == lastSignature) {
        return;
    }
    lastSignature = sig;

    // Column set: known keys first (in a fixed order), then any other key, then the computed ratio.
    const QStringList preferred = {QStringLiteral("avg_ping_ms"), QStringLiteral("ping_count"),
                                   QStringLiteral("successes"), QStringLiteral("failures"),
                                   QStringLiteral("connections")};
    QStringList extra;
    bool haveSucc = false;
    bool haveFail = false;
    for (auto it = stats.constBegin(); it != stats.constEnd(); ++it) {
        const QJsonObject s = it.value().toObject();
        for (auto k = s.constBegin(); k != s.constEnd(); ++k) {
            if (!preferred.contains(k.key()) && !extra.contains(k.key())) {
                extra << k.key();
            }
        }
        haveSucc = haveSucc || s.contains(QStringLiteral("successes"));
        haveFail = haveFail || s.contains(QStringLiteral("failures"));
    }
    std::sort(extra.begin(), extra.end());

    QStringList keys;
    for (const QString& k : preferred) {
        keys << k;
    }
    keys << extra;
    const bool withRatio = haveSucc && haveFail;

    const int cols = 1 + static_cast<int>(keys.size()) + (withRatio ? 1 : 0);

    QHeaderView* hh = table->horizontalHeader();
    const int sortCol = hh->sortIndicatorSection();
    const Qt::SortOrder sortOrder = hh->sortIndicatorOrder();
    const int vscroll = table->verticalScrollBar()->value();

    table->setUpdatesEnabled(false);
    table->setSortingEnabled(false);
    table->clear();
    table->setColumnCount(cols);
    table->setRowCount(static_cast<int>(stats.size()));

    QStringList headers;
    headers << QObject::tr("Profile id");
    for (const QString& k : keys) {
        headers << headerFor(k);
    }
    if (withRatio) {
        headers << QObject::tr("Success:failure");
    }
    table->setHorizontalHeaderLabels(headers);

    int row = 0;
    for (auto it = stats.constBegin(); it != stats.constEnd(); ++it, ++row) {
        const QJsonObject s = it.value().toObject();

        auto* idItem = new QTableWidgetItem(it.key());
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, idItem);

        int col = 1;
        for (const QString& k : keys) {
            auto* item = new QTableWidgetItem;
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            const QJsonValue v = s.value(k);
            if (v.isDouble()) {
                item->setData(Qt::DisplayRole, numberVariant(v.toDouble(), k == QLatin1String("avg_ping_ms")));
            } else if (!v.isUndefined()) {
                item->setText(Common::display(v));
            }
            table->setItem(row, col++, item);
        }
        if (withRatio) {
            auto* item = new QTableWidgetItem;
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            const double succ = s.value(QStringLiteral("successes")).toDouble();
            const double fail = s.value(QStringLiteral("failures")).toDouble();
            item->setData(Qt::DisplayRole, numberVariant(succ / std::max(fail, 1.0), false));
            table->setItem(row, col++, item);
        }
    }

    table->setSortingEnabled(true);
    if (sortCol >= 0 && sortCol < cols) {
        table->sortByColumn(sortCol, sortOrder);
    } else {
        table->sortByColumn(0, Qt::AscendingOrder);
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setUpdatesEnabled(true);
    table->verticalScrollBar()->setValue(vscroll);
}
