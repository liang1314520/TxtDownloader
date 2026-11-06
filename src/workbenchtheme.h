#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QFrame;
class QLabel;
class QWidget;

namespace WorkbenchTheme {
enum class Symbol { Library, Download, Convert, Connection, Book, Favorite };

QString styleSheet();
QLabel *label(const QString &text, const char *role, QWidget *parent);
QFrame *panel(QWidget *parent);
QIcon icon(Symbol symbol, const QColor &color = QColor(QStringLiteral("#386fc3")));
}
