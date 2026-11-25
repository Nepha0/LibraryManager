#pragma once

#include <QDateTime>
#include <QStringList>

class QFrame;
class QLabel;
class QPushButton;
class QTableWidget;
class QWidget;

namespace WindowWidgets {

QLabel *makeLabel(const QString &text, const QString &objectName = {});
QFrame *makePanel(const QString &objectName = QStringLiteral("panel"));
QTableWidget *makeTable(const QStringList &headers);
QPushButton *makePrimaryButton(const QString &text);
QWidget *makeMetric(const QString &caption, QLabel **valueLabel, const QString &accentName);
QString shortDate(const QDateTime &dateTime);
qint64 selectedId(const QTableWidget *table);

} // namespace WindowWidgets
