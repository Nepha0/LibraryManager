#include "windowwidgets.h"

#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace WindowWidgets {

QLabel *makeLabel(const QString &text, const QString &objectName)
{
    auto *label = new QLabel(text);
    if (!objectName.isEmpty()) {
        label->setObjectName(objectName);
    }
    return label;
}

QFrame *makePanel(const QString &objectName)
{
    auto *panel = new QFrame;
    panel->setObjectName(objectName);
    return panel;
}

QTableWidget *makeTable(const QStringList &headers)
{
    auto *table = new QTableWidget;
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(
        headers.size() - 1, QHeaderView::Stretch);
    return table;
}

QPushButton *makePrimaryButton(const QString &text)
{
    auto *button = new QPushButton(text);
    button->setProperty("role", QStringLiteral("primary"));
    return button;
}

QWidget *makeMetric(const QString &caption, QLabel **valueLabel,
                    const QString &accentName)
{
    auto *panel = makePanel(QStringLiteral("metric"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 16, 18, 16);
    auto *captionLabel = makeLabel(caption, QStringLiteral("metricCaption"));
    auto *value = makeLabel(QStringLiteral("0"), QStringLiteral("metricValue"));
    auto *rule = new QFrame;
    rule->setObjectName(QStringLiteral("metricRule"));
    rule->setProperty("accent", accentName);
    rule->setFixedHeight(3);
    layout->addWidget(captionLabel);
    layout->addWidget(value);
    layout->addWidget(rule);
    *valueLabel = value;
    return panel;
}

QString shortDate(const QDateTime &dateTime)
{
    return dateTime.isValid()
               ? dateTime.toString(QStringLiteral("yyyy-MM-dd"))
               : QStringLiteral("-");
}

qint64 selectedId(const QTableWidget *table)
{
    const auto *item = table->item(table->currentRow(), 0);
    return item ? item->data(Qt::UserRole).toLongLong() : 0;
}

} // namespace WindowWidgets
