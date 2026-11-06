#include "workbenchtheme.h"

#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

QString WorkbenchTheme::styleSheet()
{
    return QStringLiteral(R"(
        QWidget {
            color: #233348;
            font-family: "Microsoft YaHei UI", "Segoe UI";
            font-size: 10pt;
        }
        QWidget#workbenchWindow, QWidget[pageSurface="true"], QDialog { background: #f3f6fa; }
        QWidget#workbenchSidebar {
            background: #fcfdff;
            border-right: 1px solid #e3e9f1;
        }
        QFrame[panel="true"] {
            background: #ffffff;
            border: 1px solid #e3e9f1;
            border-radius: 10px;
        }
        QFrame#workbenchToolbar {
            background: #fcfdff;
            border-bottom: 1px solid #e3e9f1;
        }
        QFrame#workbenchTaskBar {
            background: #fcfdff;
            border-top: 1px solid #e3e9f1;
        }
        QLabel { background: transparent; border: none; }
        QLabel[role="brand"] { font-size: 17px; font-weight: 700; }
        QLabel[role="pageTitle"] { font-size: 25px; font-weight: 700; }
        QLabel[role="sectionTitle"] { font-size: 15px; font-weight: 600; }
        QLabel[role="eyebrow"] { color: #386fc3; font-size: 10px; font-weight: 600; }
        QLabel[role="muted"] { color: #67788c; font-size: 12px; }
        QLabel[role="metric"] { color: #386fc3; font-size: 22px; font-weight: 600; }
        QLabel[role="notice"] {
            color: #795921;
            background: #fcf7ed;
            border: 1px solid #eee2ca;
            border-radius: 7px;
            padding: 12px;
        }
        QLabel[role="status"] { color: #52677f; font-size: 12px; }
        QLabel[role="bookArt"] {
            background: #edf3f8;
            border-radius: 8px;
            color: #386fc3;
            padding: 18px;
        }
        QPushButton {
            min-height: 18px;
            padding: 7px 12px;
            background: #ffffff;
            border: 1px solid #dce4ee;
            border-radius: 6px;
            color: #52677f;
        }
        QPushButton:hover { background: #edf3fc; border-color: #a7bedf; color: #285ba4; }
        QPushButton:pressed { background: #dce8f8; }
        QPushButton:focus { border: 2px solid #386fc3; padding: 6px 11px; }
        QPushButton[primary="true"] { background: #386fc3; border-color: #386fc3; color: white; }
        QPushButton[primary="true"]:hover { background: #285ba4; border-color: #285ba4; }
        QPushButton[danger="true"] { color: #a44343; }
        QPushButton:disabled, QPushButton[primary="true"]:disabled {
            background: #edf1f6; border-color: #e3e9f1; color: #98a5b5;
        }
        QPushButton[nav="true"] {
            text-align: left;
            padding: 12px 14px;
            border: 1px solid transparent;
            background: transparent;
            color: #62758c;
        }
        QPushButton[nav="true"]:checked {
            color: #285ba4; background: #edf3fc; border-color: #dbe7f7; font-weight: 600;
        }
        QPushButton[nav="true"]:hover { background: #f0f4fa; }
        QPushButton[nav="true"]:focus { border-color: #386fc3; }
        QPushButton[nav="true"]:disabled { color: #a5afbc; background: transparent; }
        QLineEdit, QComboBox, QSpinBox {
            background: #f8fafc;
            border: 1px solid #dce4ee;
            border-radius: 6px;
            min-height: 20px;
            padding: 6px 9px;
            selection-background-color: #386fc3;
            selection-color: white;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QPlainTextEdit:focus { border-color: #386fc3; }
        QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled { color: #98a5b5; background: #edf1f6; }
        QComboBox { padding-right: 24px; }
        QComboBox::drop-down { border: none; width: 24px; }
        QComboBox QAbstractItemView {
            background: #ffffff; color: #233348; selection-background-color: #edf3fc;
            selection-color: #285ba4; border: 1px solid #dce4ee;
        }
        QPlainTextEdit {
            background: #f8fafc; border: 1px solid #e3e9f1;
            border-radius: 7px; selection-background-color: #386fc3; selection-color: white;
        }
        QPlainTextEdit#bookSelectedDescriptionView { background: #ffffff; border: none; }
        QPlainTextEdit#chapterPreviewView { background: #fffefa; font-size: 12pt; }
        QTableWidget {
            background: #ffffff;
            alternate-background-color: #fafcfe;
            border: none;
            gridline-color: #edf1f6;
            selection-background-color: #edf3fc;
            selection-color: #285ba4;
        }
        QTableWidget::item { padding: 8px; border-bottom: 1px solid #edf1f6; }
        QTableWidget::item:selected { background: #edf3fc; color: #285ba4; }
        QTableWidget:focus { border: 1px solid #a7bedf; }
        QHeaderView::section {
            background: #f8fafc; color: #67788c; border: none;
            border-bottom: 1px solid #e3e9f1; padding: 10px 8px; font-size: 12px;
        }
        QProgressBar {
            background: #e8eef6; border: none; border-radius: 4px;
            min-height: 16px; text-align: center; color: #233348; font-size: 11px;
        }
        QProgressBar::chunk { background: #9ebce8; border-radius: 4px; }
        QCheckBox { spacing: 8px; }
        QCheckBox:disabled { color: #98a5b5; }
        QScrollArea { background: transparent; border: none; }
        QScrollBar:vertical { background: #f3f6fa; width: 10px; margin: 0; }
        QScrollBar::handle:vertical { background: #c8d3e1; min-height: 30px; border-radius: 5px; }
        QScrollBar:horizontal { background: #f3f6fa; height: 10px; margin: 0; }
        QScrollBar::handle:horizontal { background: #c8d3e1; min-width: 30px; border-radius: 5px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QSplitter::handle { background: transparent; }
        QSplitter::handle:hover { background: #dce8f8; }
        QToolTip { color: #233348; background: #ffffff; border: 1px solid #dce4ee; padding: 6px; }
    )");
}

QLabel *WorkbenchTheme::label(const QString &text, const char *role, QWidget *parent)
{
    auto *result = new QLabel(text, parent);
    result->setProperty("role", role);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    return result;
}

QFrame *WorkbenchTheme::panel(QWidget *parent)
{
    auto *result = new QFrame(parent);
    result->setProperty("panel", true);
    return result;
}

QIcon WorkbenchTheme::icon(Symbol symbol, const QColor &color)
{
    QPixmap image(48, 48);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(2, 2);
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath path;
    switch (symbol) {
    case Symbol::Library:
        painter.drawRoundedRect(QRectF(3, 5, 4, 15), 0.6, 0.6);
        painter.drawRoundedRect(QRectF(10, 3, 4, 17), 0.6, 0.6);
        path.moveTo(17, 6); path.lineTo(20, 5); path.lineTo(23, 19); path.lineTo(19, 20); path.closeSubpath();
        break;
    case Symbol::Download:
        path.moveTo(12, 3); path.lineTo(12, 15);
        path.moveTo(8, 11); path.lineTo(12, 15); path.lineTo(16, 11);
        path.moveTo(4, 16); path.lineTo(4, 20); path.lineTo(20, 20); path.lineTo(20, 16);
        break;
    case Symbol::Convert:
        path.moveTo(4, 8); path.lineTo(20, 8); path.lineTo(16, 4);
        path.moveTo(20, 8); path.lineTo(16, 12);
        path.moveTo(20, 16); path.lineTo(4, 16); path.lineTo(8, 12);
        path.moveTo(4, 16); path.lineTo(8, 20);
        break;
    case Symbol::Connection:
        path.moveTo(12, 3); path.lineTo(20, 6); path.lineTo(20, 12);
        path.cubicTo(20, 17, 12, 21, 12, 21); path.cubicTo(12, 21, 4, 17, 4, 12);
        path.lineTo(4, 6); path.closeSubpath();
        path.moveTo(8, 12); path.lineTo(11, 15); path.lineTo(16, 9);
        break;
    case Symbol::Favorite:
        path.moveTo(6, 3); path.lineTo(18, 3); path.lineTo(18, 21);
        path.lineTo(12, 17); path.lineTo(6, 21); path.closeSubpath();
        break;
    case Symbol::Book:
        path.moveTo(12, 5); path.cubicTo(9, 3, 6, 3, 3, 4); path.lineTo(3, 19);
        path.cubicTo(6, 18, 9, 18, 12, 20); path.cubicTo(15, 18, 18, 18, 21, 19);
        path.lineTo(21, 4); path.cubicTo(18, 3, 15, 3, 12, 5); path.lineTo(12, 20);
        break;
    }
    painter.drawPath(path);
    painter.end();
    QIcon result;
    result.addPixmap(image);
    return result;
}
