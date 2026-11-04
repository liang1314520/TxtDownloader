#pragma once

#include "novelapi.h"

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;

class BookDetailDialog : public QDialog {
    Q_OBJECT

public:
    explicit BookDetailDialog(const NovelSearchBook &book, QWidget *parent = nullptr);
    void setCatalog(const NovelBook &book);
    void setChapter(int index, const QString &text, const QString &status);
    void setMessage(const QString &message);
    void finishPreview(const QString &message);
    void setPreviewAvailable(bool available);

signals:
    void previewRequested();
    void cancelRequested();

private:
    void showChapter();
    QPlainTextEdit *descriptionView;
    QPlainTextEdit *previewView;
    QComboBox *chapterChoice;
    QLabel *messageLabel;
    QPushButton *loadButton;
    QPushButton *cancelButton;
    QStringList texts;
    QStringList statuses;
};
