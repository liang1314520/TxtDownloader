#pragma once

#include "mumudiscovery.h"

#include <QDialog>

class MuMuCapture;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

class MuMuCaptureDialog : public QDialog {
    Q_OBJECT

public:
    explicit MuMuCaptureDialog(QWidget *parent = nullptr, bool categoryOnly = false);
    ~MuMuCaptureDialog() override;
    void setImportResult(const QString &error, const QString &sessionStatus);
    void setResultMessage(const QString &message);

signals:
    void requestReady(const QByteArray &request);
    void busyChanged(bool busy);

protected:
    void reject() override;
    void closeEvent(QCloseEvent *event) override;

private:
    void updateActions();
    void startCapture();
    void detectInstances();
    void selectInstance(int index);

    MuMuCapture *capture;
    QWidget *options;
    QLineEdit *toolPath;
    QLineEdit *configPath;
    QComboBox *instances;
    QVector<MuMuInstance> detectedInstances;
    QSpinBox *adapter;
    QCheckBox *consent;
    QPushButton *startButton;
    QPushButton *stopButton;
    QPushButton *closeButton;
    QLabel *status;
    QString importMessage;
    bool closePending = false;
    bool categoryOnly = false;
};
