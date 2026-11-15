#include "PriorityTab.h"

#include "ApiClient.h"
#include "Common.h"
#include "Widgets.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Col { ColMetric = 0, ColPrefer, ColWeight, ColEnabled, ColCount };

const QStringList& metricNames()
{
    static const QStringList v = {QStringLiteral("latency"), QStringLiteral("successes"),
                                  QStringLiteral("failures"), QStringLiteral("connections"),
                                  QStringLiteral("ratio")};
    return v;
}

const QStringList& preferNames()
{
    static const QStringList v = {QStringLiteral("lower"), QStringLiteral("higher")};
    return v;
}

const QStringList& strategyNames()
{
    static const QStringList v = {QStringLiteral("weighted"), QStringLiteral("lexicographic")};
    return v;
}

// Combo box for a fixed choice list that still keeps an unknown current value.
QComboBox* makeCombo(const QStringList& choices, const QString& current, QWidget* parent)
{
    auto* cb = new WheelGuardComboBox(parent);
    cb->addItems(choices);
    int idx = cb->findText(current);
    if (idx < 0 && !current.isEmpty()) {
        cb->addItem(current);
        idx = cb->findText(current);
    }
    if (idx >= 0) {
        cb->setCurrentIndex(idx);
    }
    return cb;
}
} // namespace

// ===========================================================================
// PriorityEditor
// ===========================================================================

PriorityEditor::PriorityEditor(ApiClient* api, const QString& basePath, const QString& title, QWidget* parent)
    : QGroupBox(title, parent)
    , m_api(api)
    , m_base(basePath)
    , m_title(title)
{
    auto* lay = new QVBoxLayout(this);

    auto* row1 = new QHBoxLayout;
    row1->addWidget(new QLabel(tr("Strategy:"), this));
    m_strategy = makeCombo(strategyNames(), QString(), this);
    m_strategy->setToolTip(
        tr("weighted: normalised weighted average of all enabled criteria\n"
           "lexicographic: criteria compared in list order, later ones only break ties"));
    row1->addWidget(m_strategy, 1);
    lay->addLayout(row1);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({tr("Metric"), tr("Prefer"), tr("Weight"), tr("Enabled")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColEnabled, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->setToolTip(tr("Click a row number on the left to select a criterion. "
                           "The order matters for the lexicographic strategy."));
    lay->addWidget(m_table, 1);

    auto* row2 = new QHBoxLayout;
    auto* addBtn = new QPushButton(tr("Add"), this);
    auto* removeBtn = new QPushButton(tr("Remove"), this);
    auto* upBtn = new QPushButton(tr("Move up"), this);
    auto* downBtn = new QPushButton(tr("Move down"), this);
    row2->addWidget(addBtn);
    row2->addWidget(removeBtn);
    row2->addWidget(upBtn);
    row2->addWidget(downBtn);
    row2->addStretch(1);
    lay->addLayout(row2);

    auto* row3 = new QHBoxLayout;
    auto* reloadBtn = new QPushButton(tr("Revert"), this);
    m_applyBtn = new QPushButton(tr("Apply"), this);
    row3->addStretch(1);
    row3->addWidget(reloadBtn);
    row3->addWidget(m_applyBtn);
    lay->addLayout(row3);

    connect(m_strategy, &QComboBox::currentIndexChanged, this, [this](int) { markDirty(); });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) { markDirty(); });
    connect(addBtn, &QPushButton::clicked, this, [this]() { addRow(); });
    connect(removeBtn, &QPushButton::clicked, this, [this]() { removeRow(); });
    connect(upBtn, &QPushButton::clicked, this, [this]() { moveRow(-1); });
    connect(downBtn, &QPushButton::clicked, this, [this]() { moveRow(1); });
    connect(reloadBtn, &QPushButton::clicked, this, [this]() { reload(); });
    connect(m_applyBtn, &QPushButton::clicked, this, [this]() { apply(); });

    // Pick up changes made elsewhere, but never overwrite unsaved edits.
    connect(m_api, &ApiClient::configChanged, this, [this]() {
        if (!m_dirty && !m_applying && isVisible()) {
            reload();
        }
    });
}

void PriorityEditor::showEvent(QShowEvent* event)
{
    QGroupBox::showEvent(event);
    if (!m_dirty) {
        reload();
    }
}

void PriorityEditor::setDirty(bool dirty)
{
    m_dirty = dirty;
    setTitle(dirty ? m_title + tr(" (unsaved changes)") : m_title);
}

void PriorityEditor::markDirty()
{
    if (!m_loading) {
        setDirty(true);
    }
}

void PriorityEditor::reload()
{
    m_api->get(QStringLiteral("/config"), this, [this](const ApiClient::Response& r) {
        if (!r.ok || !r.doc.isObject()) {
            if (!r.networkError) {
                emit message(tr("Could not load config: %1").arg(r.error));
            }
            return;
        }
        const QJsonObject prio = Common::getPath(r.doc.object(), m_base).toObject();

        m_loading = true;
        const QString strategy = prio.value(QStringLiteral("strategy")).toString(QStringLiteral("weighted"));
        int idx = m_strategy->findText(strategy);
        if (idx < 0) {
            m_strategy->addItem(strategy);
            idx = m_strategy->findText(strategy);
        }
        m_strategy->setCurrentIndex(idx);
        fillRows(prio.value(QStringLiteral("criteria")).toArray());
        m_loading = false;
        setDirty(false);
    });
}

void PriorityEditor::fillRows(const QJsonArray& criteria)
{
    const bool wasLoading = m_loading;
    m_loading = true;

    m_table->setRowCount(0); // also deletes the old cell widgets
    m_table->setRowCount(criteria.size());
    for (int r = 0; r < criteria.size(); ++r) {
        const QJsonObject c = criteria.at(r).toObject();

        // Plain items under the widgets so the row can be selected and the table is not "empty".
        for (int col = ColMetric; col <= ColWeight; ++col) {
            auto* it = new QTableWidgetItem;
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_table->setItem(r, col, it);
        }

        QComboBox* metric = makeCombo(metricNames(), c.value(QStringLiteral("metric")).toString(), m_table);
        QComboBox* prefer = makeCombo(preferNames(), c.value(QStringLiteral("prefer")).toString(), m_table);
        auto* weight = new NumberSpinBox(m_table);
        weight->setDecimals(4);
        weight->setRange(0.0, 1000000.0);
        weight->setSingleStep(0.5);
        weight->setValue(c.value(QStringLiteral("weight")).toDouble(1.0));
        weight->setKeyboardTracking(false);

        connect(metric, &QComboBox::currentIndexChanged, this, [this](int) { markDirty(); });
        connect(prefer, &QComboBox::currentIndexChanged, this, [this](int) { markDirty(); });
        connect(weight, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this](double) { markDirty(); });

        m_table->setCellWidget(r, ColMetric, metric);
        m_table->setCellWidget(r, ColPrefer, prefer);
        m_table->setCellWidget(r, ColWeight, weight);

        auto* en = new QTableWidgetItem;
        en->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        en->setCheckState(c.value(QStringLiteral("enabled")).toBool(true) ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(r, ColEnabled, en);
    }

    m_loading = wasLoading;
}

QJsonArray PriorityEditor::collectRows() const
{
    QJsonArray out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const auto* metric = qobject_cast<QComboBox*>(m_table->cellWidget(r, ColMetric));
        const auto* prefer = qobject_cast<QComboBox*>(m_table->cellWidget(r, ColPrefer));
        const auto* weight = qobject_cast<QDoubleSpinBox*>(m_table->cellWidget(r, ColWeight));
        const QTableWidgetItem* en = m_table->item(r, ColEnabled);
        if (!metric || !prefer || !weight || !en) {
            continue;
        }
        QJsonObject o;
        o.insert(QStringLiteral("metric"), metric->currentText());
        o.insert(QStringLiteral("prefer"), prefer->currentText());
        o.insert(QStringLiteral("weight"), weight->value());
        o.insert(QStringLiteral("enabled"), en->checkState() == Qt::Checked);
        out.append(o);
    }
    return out;
}

void PriorityEditor::addRow()
{
    QJsonArray rows = collectRows();
    QJsonObject o;
    o.insert(QStringLiteral("metric"), QStringLiteral("latency"));
    o.insert(QStringLiteral("prefer"), QStringLiteral("lower"));
    o.insert(QStringLiteral("weight"), 1.0);
    o.insert(QStringLiteral("enabled"), true);
    rows.append(o);
    fillRows(rows);
    m_table->selectRow(static_cast<int>(rows.size()) - 1);
    setDirty(true);
}

void PriorityEditor::removeRow()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_table->rowCount()) {
        emit message(tr("Select a criterion first (click its row number)."));
        return;
    }
    QJsonArray rows = collectRows();
    if (row >= rows.size()) {
        return;
    }
    rows.removeAt(row);
    fillRows(rows);
    if (!rows.isEmpty()) {
        m_table->selectRow(qMin(row, static_cast<int>(rows.size()) - 1));
    }
    setDirty(true);
}

void PriorityEditor::moveRow(int delta)
{
    const int row = m_table->currentRow();
    const int target = row + delta;
    QJsonArray rows = collectRows();
    if (row < 0 || row >= rows.size() || target < 0 || target >= rows.size()) {
        return;
    }
    const QJsonValue a = rows.at(row);
    const QJsonValue b = rows.at(target);
    rows.replace(row, b);
    rows.replace(target, a);
    fillRows(rows);
    m_table->selectRow(target);
    setDirty(true);
}

void PriorityEditor::apply()
{
    const QString strategy = m_strategy->currentText();
    const QJsonArray criteria = collectRows();
    const QString base = m_base;

    m_applying = true;
    m_applyBtn->setEnabled(false);
    m_api->modifyConfig(
        this,
        [base, strategy, criteria](QJsonObject& cfg) {
            QJsonObject prio = Common::getPath(cfg, base).toObject(); // keeps unknown keys
            prio.insert(QStringLiteral("strategy"), strategy);
            prio.insert(QStringLiteral("criteria"), criteria);
            Common::setPath(cfg, base, prio);
            return true;
        },
        [this](bool ok, const QString& err) {
            m_applying = false;
            m_applyBtn->setEnabled(true);
            if (ok) {
                emit message(tr("Priority saved (%1).").arg(m_base));
                setDirty(false);
                reload();
            } else {
                emit message(tr("Saving priority (%1) failed: %2").arg(m_base, err));
            }
        });
}

// ===========================================================================
// PriorityTab
// ===========================================================================

PriorityTab::PriorityTab(ApiClient* api, QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    auto* normal = new PriorityEditor(api, QStringLiteral("normal.priority"),
                                      tr("Normal config (normal.priority)"), this);
    auto* bench = new PriorityEditor(api, QStringLiteral("benchmark.base.priority"),
                                     tr("Benchmark config (benchmark.base.priority)"), this);
    lay->addWidget(normal, 1);
    lay->addWidget(bench, 1);

    connect(normal, &PriorityEditor::message, this, &PriorityTab::message);
    connect(bench, &PriorityEditor::message, this, &PriorityTab::message);
}
