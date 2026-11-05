#pragma once

#include "pcaprequestreader.h"

#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

struct MuMuVmState {
    QString configPath;
    QString traceFile;
    bool tracing = false;
    bool running = false;
};

class MuMuCapture : public QObject {
    Q_OBJECT

public:
    explicit MuMuCapture(QObject *parent = nullptr);
    ~MuMuCapture() override;
    bool isBusy() const { return stage != Stage::Idle; }
    bool needsRecovery() const { return fileChanged || traceChanged; }
    bool hasCleanupWarning() const { return cleanupWarning; }
    void start(const QString &tool, const QString &config, int adapter, bool categoryOnly = false);
    void stop();
    static QString parseVmState(const QByteArray &output, int adapter, MuMuVmState &state);

signals:
    void busyChanged(bool busy);
    void statusChanged(const QString &message);
    void requestReady(const QByteArray &request);
    void finished(bool success, const QString &message);

private:
    enum class Stage { Idle, Inspect, SetFile, Enable, Capture, Disable, Drain, Restore, Verify };
    void command(Stage next, const QStringList &arguments);
    void commandFinished(bool success);
    void beginCleanup();
    void poll();
    void finish(bool restored);
    bool restoreOnShutdown();

    Stage stage = Stage::Idle;
    QProcess process;
    QTimer commandTimer;
    QTimer pollTimer;
    QElapsedTimer elapsed;
    QString executable;
    QString configPath;
    QString vmId;
    int adapterNumber = 1;
    QString previousTraceFile;
    QString tracePath;
    std::unique_ptr<QTemporaryDir> directory;
    QFile trace;
    PcapRequestReader reader;
    QByteArray commandOutput;
    QByteArray capturedRequest;
    QString message;
    bool categoryOnly = false;
    QString categoryError;
    bool stopRequested = false;
    bool fileChanged = false;
    bool traceChanged = false;
    bool commandRunning = false;
    bool commandFailed = false;
    bool finalScan = false;
    bool cleanupWarning = false;
    qint64 readOffset = 0;
};
