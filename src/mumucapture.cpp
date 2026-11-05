#include "mumucapture.h"
#include "novelapi.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <QXmlStreamReader>

namespace {
constexpr qint64 MaxTraceSize = 32 * 1024 * 1024;
constexpr int MaxCaptureTime = 60000;
constexpr qsizetype MaxCommandOutput = 128 * 1024;

bool regularLocation(const QFileInfo &file)
{
    if (!file.isFile() || file.isSymLink() || file.isJunction())
        return false;
    QDir directory(file.absolutePath());
    for (;;) {
        const QFileInfo info(directory.absolutePath());
        if (info.isSymLink() || info.isJunction())
            return false;
        if (directory.isRoot())
            return true;
        if (!directory.cdUp())
            return false;
    }
}
}

MuMuCapture::MuMuCapture(QObject *parent) : QObject(parent)
{
    commandTimer.setSingleShot(true);
    commandTimer.setInterval(10000);
    pollTimer.setInterval(200);
    connect(&commandTimer, &QTimer::timeout, this, [this] {
        commandFailed = true;
        process.kill();
    });
    connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
        const auto bytes = process.readAllStandardOutput();
        if (commandOutput.size() + bytes.size() > MaxCommandOutput) {
            commandFailed = true;
            process.kill();
        } else {
            commandOutput.append(bytes);
        }
    });
    connect(&process, &QProcess::readyReadStandardError, this, [this] {
        process.readAllStandardError();
    });
    connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && commandRunning) {
            commandRunning = false;
            commandTimer.stop();
            commandFinished(false);
        }
    });
    connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!commandRunning)
            return;
        commandRunning = false;
        commandTimer.stop();
        const auto remaining = process.readAllStandardOutput();
        if (commandOutput.size() + remaining.size() <= MaxCommandOutput)
            commandOutput.append(remaining);
        else
            commandFailed = true;
        process.readAllStandardError();
        commandFinished(code == 0 && status == QProcess::NormalExit && !commandFailed);
    });
    connect(&pollTimer, &QTimer::timeout, this, &MuMuCapture::poll);
}

MuMuCapture::~MuMuCapture()
{
    commandTimer.stop();
    pollTimer.stop();
    process.disconnect(this);
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(2000);
    }
    trace.close();
    if ((fileChanged || traceChanged) && !restoreOnShutdown() && directory)
        directory->setAutoRemove(false);
    capturedRequest.fill('\0');
}

QString MuMuCapture::parseVmState(const QByteArray &output, int adapter, MuMuVmState &state)
{
    if (output.size() > MaxCommandOutput || adapter < 1 || adapter > 8)
        return QStringLiteral("MuMu 实例信息超过限制或网卡编号无效。");
    const auto invalidState = QStringLiteral("MuMu 没有返回可确认的网卡记录状态，未修改任何记录设置。");
    const auto nicKey = QStringLiteral("NIC %1").arg(adapter);
    QMap<QString, QString> values;
    for (const auto &line : QString::fromUtf8(output).split(QLatin1Char('\n'))) {
        const auto separator = line.indexOf(QLatin1Char(':'));
        if (separator <= 0)
            continue;
        const auto key = line.first(separator).trimmed();
        if (key != QStringLiteral("Config file") && key != QStringLiteral("State") && key != nicKey)
            continue;
        if (values.contains(key))
            return invalidState;
        values.insert(key, line.sliced(separator + 1).trimmed());
    }
    static const QRegularExpression tracePattern(
        QStringLiteral("(?:^|, )Trace: (on|off) \\(file: (.*)\\), Type: [^,]+(?:, .*)?$"));
    const auto traceState = tracePattern.match(values.value(nicKey));
    const auto vmState = values.value(QStringLiteral("State"));
    if (!values.contains(QStringLiteral("Config file")) || vmState.isEmpty() || !traceState.hasMatch())
        return invalidState;
    MuMuVmState parsed;
    parsed.configPath = values.value(QStringLiteral("Config file"));
    parsed.running = vmState == QStringLiteral("running")
        || (vmState.startsWith(QStringLiteral("running (since ")) && vmState.endsWith(QLatin1Char(')')));
    parsed.traceFile = traceState.captured(2);
    if (parsed.traceFile == QStringLiteral("none"))
        parsed.traceFile.clear();
    parsed.tracing = traceState.captured(1) == QStringLiteral("on");
    for (const auto &path : {parsed.configPath, parsed.traceFile}) {
        if (path.contains(QChar::Null) || path.contains(QLatin1Char('\r'))
            || path.contains(QLatin1Char('\n')) || path.size() > 4096)
            return QStringLiteral("MuMu 实例信息格式不正确，未修改记录设置。");
    }
    if (parsed.configPath.isEmpty())
        return invalidState;
    state = parsed;
    return {};
}

void MuMuCapture::start(const QString &tool, const QString &config, int adapter, bool captureCategory)
{
    if (isBusy() || needsRecovery())
        return;
    categoryOnly = captureCategory;
    categoryError.clear();
    if (categoryOnly) {
        NovelApi api;
        const auto error = api.loadSessionKeys();
        if (!error.isEmpty()) {
            emit finished(false, error);
            return;
        }
    }
    const QFileInfo toolFile(tool);
    const QFileInfo configFile(config);
    if (!regularLocation(toolFile) || toolFile.fileName().compare(QStringLiteral("VBoxManage.exe"), Qt::CaseInsensitive) != 0
        || !regularLocation(configFile) || configFile.suffix().compare(QStringLiteral("nemu"), Qt::CaseInsensitive) != 0
        || configFile.size() > 1024 * 1024 || adapter < 1 || adapter > 8) {
        emit finished(false, QStringLiteral("请选择 MuMu 自带的 VBoxManage.exe、普通 .nemu 实例配置和 1–8 网卡编号。"));
        return;
    }
    QFile settings(configFile.absoluteFilePath());
    if (!settings.open(QIODevice::ReadOnly)) {
        emit finished(false, QStringLiteral("无法只读打开 MuMu 实例配置。"));
        return;
    }
    QXmlStreamReader xml(settings.read(1024 * 1024 + 1));
    QString id;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.tokenType() == QXmlStreamReader::DTD) {
            xml.raiseError(QStringLiteral("不接受带 DTD 的配置"));
            break;
        }
        if (xml.isStartElement() && xml.name() == QLatin1String("Machine"))
            id = xml.attributes().value(QStringLiteral("uuid")).toString();
    }
    const QUuid uuid(id);
    if (xml.hasError() || uuid.isNull()) {
        emit finished(false, QStringLiteral("MuMu 实例配置中没有有效的机器 UUID。"));
        return;
    }
    executable = toolFile.canonicalFilePath();
    configPath = configFile.canonicalFilePath();
    vmId = uuid.toString(QUuid::WithoutBraces);
    adapterNumber = adapter;
    previousTraceFile.clear();
    tracePath.clear();
    message.clear();
    stopRequested = false;
    finalScan = false;
    cleanupWarning = false;
    readOffset = 0;
    capturedRequest.clear();
    reader.reset();
    stage = Stage::Inspect;
    emit busyChanged(true);
    emit statusChanged(QStringLiteral("正在只读核对所选 MuMu 实例；不会启动、注册或重启模拟器…"));
    command(Stage::Inspect, {QStringLiteral("showvminfo"), vmId, QStringLiteral("--details")});
}

void MuMuCapture::command(Stage next, const QStringList &arguments)
{
    stage = next;
    commandOutput.clear();
    commandFailed = false;
    commandRunning = true;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.setWorkingDirectory(QFileInfo(executable).absolutePath());
    commandTimer.start();
    process.start();
}

void MuMuCapture::commandFinished(bool success)
{
    const auto nic = QString::number(adapterNumber);
    switch (stage) {
    case Stage::Inspect: {
        MuMuVmState state;
        const auto error = success ? parseVmState(commandOutput, adapterNumber, state)
                                   : QStringLiteral("无法查询 MuMu 实例。请先手动启动所选实例；不会自动注册或修改虚拟机配置。");
        if (!error.isEmpty() || QFileInfo(state.configPath).canonicalFilePath().compare(configPath, Qt::CaseInsensitive) != 0
            || !state.running || state.tracing || stopRequested) {
            message = !error.isEmpty() ? error : stopRequested ? QStringLiteral("采集已取消，未开启记录。")
                : state.tracing ? QStringLiteral("所选网卡已有记录任务，未接管、停止或覆盖它。")
                : !state.running ? QStringLiteral("所选 MuMu 实例未运行，请手动启动后重试。")
                : QStringLiteral("工具返回的实例路径与所选配置不一致，未修改任何设置。");
            finish(true);
            return;
        }
        previousTraceFile = state.traceFile;
        directory = std::make_unique<QTemporaryDir>(QDir::temp().filePath(QStringLiteral("NovDecoder-capture-XXXXXX")));
        if (!directory->isValid()) {
            message = QStringLiteral("无法创建本次采集临时目录，未开启记录。");
            finish(true);
            return;
        }
        tracePath = QDir(directory->path()).filePath(QStringLiteral("network.pcap"));
        const auto recovery = QJsonDocument(QJsonObject{
            {QStringLiteral("tool"), executable}, {QStringLiteral("config"), configPath},
            {QStringLiteral("vm_id"), vmId}, {QStringLiteral("adapter"), adapterNumber},
            {QStringLiteral("previous_trace_file"), previousTraceFile},
            {QStringLiteral("temporary_trace_file"), tracePath}}).toJson(QJsonDocument::Indented);
        QSaveFile marker(QDir(directory->path()).filePath(QStringLiteral("recovery.json")));
        marker.setDirectWriteFallback(false);
        if (!marker.open(QIODevice::WriteOnly) || marker.write(recovery) != recovery.size() || !marker.commit()) {
            message = QStringLiteral("无法写入异常退出恢复信息，未开启采集。");
            finish(true);
            return;
        }
        fileChanged = true;
        command(Stage::SetFile, {QStringLiteral("controlvm"), vmId, QStringLiteral("nictracefile") + nic, tracePath});
        return;
    }
    case Stage::SetFile:
        if (!success || stopRequested) {
            message = success ? QStringLiteral("采集已取消。") : QStringLiteral("无法设置本次临时记录位置。");
            beginCleanup();
            return;
        }
        traceChanged = true;
        command(Stage::Enable, {QStringLiteral("controlvm"), vmId, QStringLiteral("nictrace") + nic, QStringLiteral("on")});
        return;
    case Stage::Enable:
        if (!success || stopRequested) {
            message = success ? QStringLiteral("采集已取消。") : QStringLiteral("MuMu 未能开启网络记录。");
            beginCleanup();
            return;
        }
        stage = Stage::Capture;
        elapsed.start();
        pollTimer.start();
        emit statusChanged(QStringLiteral("正在采集所选实例，60 秒或达到 32 MiB 时停止（可能短暂超出）。%1\n本次临时目录：%2")
                               .arg(categoryOnly
                                        ? QStringLiteral("请在 App 中打开目标小说分类；如已打开，请重新进入或刷新，不要使用关键词搜索。")
                                        : QStringLiteral("请在 App 正常搜索或打开一本书；不要操作其他应用。"),
                                    QDir::toNativeSeparators(directory->path())));
        return;
    case Stage::Disable:
        if (!success) {
            message += QStringLiteral("\n未确认记录已关闭；请在 MuMu 中手动关闭该网卡记录，临时目录保留。");
            finish(false);
            return;
        }
        if (finalScan && !stopRequested && capturedRequest.isEmpty()) {
            stage = Stage::Drain;
            pollTimer.start();
            poll();
        } else {
            command(Stage::Restore, {QStringLiteral("controlvm"), vmId, QStringLiteral("nictracefile") + nic, previousTraceFile});
        }
        return;
    case Stage::Restore:
        if (!success) {
            message += QStringLiteral("\n未能恢复原记录路径；请手动恢复，临时目录保留。");
            finish(false);
            return;
        }
        command(Stage::Verify, {QStringLiteral("showvminfo"), vmId, QStringLiteral("--details")});
        return;
    case Stage::Verify: {
        MuMuVmState state;
        const auto error = success ? parseVmState(commandOutput, adapterNumber, state) : QStringLiteral("无法复核记录设置");
        if (!error.isEmpty() || state.tracing || state.traceFile != previousTraceFile
            || QFileInfo(state.configPath).canonicalFilePath().compare(configPath, Qt::CaseInsensitive) != 0) {
            message += QStringLiteral("\n未确认原记录设置恢复，请人工检查；临时目录保留。");
            finish(false);
        } else {
            traceChanged = false;
            fileChanged = false;
            if (!capturedRequest.isEmpty() && !stopRequested)
                message = QStringLiteral("目标请求已捕获，网络记录已关闭并恢复原设置。");
            finish(true);
        }
        return;
    }
    default:
        return;
    }
}

void MuMuCapture::poll()
{
    if (stage != Stage::Capture && stage != Stage::Drain)
        return;
    const bool draining = stage == Stage::Drain;
    const auto finishDrain = [this] {
        finalScan = false;
        pollTimer.stop();
        trace.close();
        command(Stage::Restore, {QStringLiteral("controlvm"), vmId,
            QStringLiteral("nictracefile") + QString::number(adapterNumber), previousTraceFile});
    };
    const auto finishRead = [this, draining, &finishDrain] {
        if (draining)
            finishDrain();
        else
            beginCleanup();
    };
    const QFileInfo info(tracePath);
    if (info.isSymLink() || info.isJunction() || (info.exists() && !info.isFile()) || info.size() > MaxTraceSize) {
        message = QStringLiteral("网络记录达到 32 MiB 上限或位置发生异常，已停止采集。");
        finishRead();
        return;
    }
    if (!draining && elapsed.elapsed() >= MaxCaptureTime) {
        message = QStringLiteral("采集已达到 60 秒，正在关闭记录并检查最后写入的数据…");
        finalScan = true;
        beginCleanup();
        return;
    }
    if (!info.exists()) {
        if (draining) {
            message = QStringLiteral("MuMu 没有生成网络记录文件，未导入会话。");
            finishDrain();
        }
        return;
    }
    if (!trace.isOpen()) {
        trace.setFileName(tracePath);
        if (!trace.open(QIODevice::ReadOnly) || trace.size() < readOffset || !trace.seek(readOffset)) {
            message = QStringLiteral("无法从上次位置读取 MuMu 的本次网络记录，已停止。");
            finishRead();
            return;
        }
    }
    if (trace.size() < trace.pos()) {
        message = QStringLiteral("本次网络记录被截断，已停止。");
        finishRead();
        return;
    }
    qint64 budget = 512 * 1024;
    bool buffered = true;
    int batches = 0;
    while (budget > 0 && batches < 16) {
        QByteArray bytes;
        if (!buffered) {
            if (trace.bytesAvailable() == 0)
                break;
            bytes = trace.read(qMin<qint64>(64 * 1024, budget));
            if (bytes.isEmpty())
                break;
            budget -= bytes.size();
            readOffset = trace.pos();
        }
        auto requests = reader.append(bytes);
        buffered = requests.size() == 8;
        ++batches;
        if (!reader.errorString().isEmpty()) {
            message = reader.errorString();
            finishRead();
            return;
        }
        for (auto &request : requests) {
            if (categoryOnly) {
                NovelCategory category;
                const auto error = NovelApi::categoryFromRequest(request, category);
                if (!error.isEmpty()) {
                    request.fill('\0');
                    if (error != categoryError) {
                        categoryError = error;
                        emit statusChanged(QStringLiteral("尚未捕获可添加的小说分类，继续等待：%1\n请在 App 打开目标分类或重新进入该页面。")
                                               .arg(error));
                    }
                    continue;
                }
            }
            capturedRequest = request;
            message = QStringLiteral("已捕获完整目标请求，正在关闭记录并恢复设置…");
            finishRead();
            return;
        }
    }
    if (trace.error() != QFileDevice::NoError) {
        message = QStringLiteral("读取本次网络记录失败，已停止。");
        finishRead();
    } else if (draining && !buffered && trace.bytesAvailable() == 0) {
        message = categoryOnly
            ? QStringLiteral("网络记录已关闭，未发现可添加的小说分类请求。请重新进入分类或刷新后重试。%1")
                  .arg(categoryError.isEmpty() ? QString() : QStringLiteral("\n") + categoryError)
            : QStringLiteral("网络记录已关闭，未发现完整目标请求。请在 App 正常搜索或打开书籍后重试。");
        finishDrain();
    }
}

void MuMuCapture::stop()
{
    if (!isBusy())
        return;
    stopRequested = true;
    capturedRequest.fill('\0');
    capturedRequest.clear();
    if (stage == Stage::Capture) {
        message = QStringLiteral("采集已手动停止，未导入会话。");
        beginCleanup();
    } else if (stage == Stage::Drain) {
        finalScan = false;
        pollTimer.stop();
        trace.close();
        message = QStringLiteral("采集已手动停止，未导入会话。");
        command(Stage::Restore, {QStringLiteral("controlvm"), vmId,
            QStringLiteral("nictracefile") + QString::number(adapterNumber), previousTraceFile});
    } else if (stage != Stage::Disable && stage != Stage::Restore && stage != Stage::Verify) {
        emit statusChanged(QStringLiteral("正在取消并恢复记录设置，请勿关闭程序…"));
    }
}

void MuMuCapture::beginCleanup()
{
    pollTimer.stop();
    trace.close();
    if (directory)
        emit statusChanged(message + QStringLiteral("\n本次临时目录（异常退出时需人工检查）：%1")
                                       .arg(QDir::toNativeSeparators(directory->path())));
    else
        emit statusChanged(message);
    if (traceChanged) {
        command(Stage::Disable, {QStringLiteral("controlvm"), vmId,
            QStringLiteral("nictrace") + QString::number(adapterNumber), QStringLiteral("off")});
    } else if (fileChanged) {
        command(Stage::Restore, {QStringLiteral("controlvm"), vmId,
            QStringLiteral("nictracefile") + QString::number(adapterNumber), previousTraceFile});
    } else {
        finish(true);
    }
}

void MuMuCapture::finish(bool restored)
{
    commandTimer.stop();
    pollTimer.stop();
    trace.close();
    reader.reset();
    if (directory) {
        if (!restored) {
            directory->setAutoRemove(false);
            message += QStringLiteral("\n实例 UUID：%1；网卡编号：%2；原记录路径：%3\n"
                                      "需使用 MuMu 管理工具确认 nictrace%2 为 off，并恢复 nictracefile%2 后，再删除本次临时目录：%4")
                           .arg(vmId).arg(adapterNumber)
                           .arg(previousTraceFile.isEmpty() ? QStringLiteral("（空）") : previousTraceFile,
                                QDir::toNativeSeparators(directory->path()));
        } else if (!directory->remove()) {
            cleanupWarning = true;
            message += QStringLiteral("\n临时文件删除失败，可能含敏感流量，请手动删除：%1").arg(QDir::toNativeSeparators(directory->path()));
            directory->setAutoRemove(false);
        }
        directory.reset();
    }
    stage = Stage::Idle;
    const bool success = restored && !stopRequested && !capturedRequest.isEmpty();
    if (success)
        emit requestReady(capturedRequest);
    capturedRequest.fill('\0');
    capturedRequest.clear();
    emit busyChanged(false);
    emit finished(success, message);
}

bool MuMuCapture::restoreOnShutdown()
{
    const auto nic = QString::number(adapterNumber);
    auto run = [this](const QStringList &arguments, QByteArray *output = nullptr) {
        QProcess cleanup;
        cleanup.setWorkingDirectory(QFileInfo(executable).absolutePath());
        cleanup.start(executable, arguments);
        if (!cleanup.waitForFinished(3000)) {
            cleanup.kill();
            cleanup.waitForFinished(1000);
            return false;
        }
        if (output)
            *output = cleanup.readAllStandardOutput();
        return cleanup.exitStatus() == QProcess::NormalExit && cleanup.exitCode() == 0;
    };
    if (traceChanged && !run({QStringLiteral("controlvm"), vmId, QStringLiteral("nictrace") + nic, QStringLiteral("off")}))
        return false;
    if (fileChanged && !run({QStringLiteral("controlvm"), vmId, QStringLiteral("nictracefile") + nic, previousTraceFile}))
        return false;
    QByteArray output;
    MuMuVmState state;
    if (!run({QStringLiteral("showvminfo"), vmId, QStringLiteral("--details")}, &output)
        || !parseVmState(output, adapterNumber, state).isEmpty() || state.tracing
        || state.traceFile != previousTraceFile
        || QFileInfo(state.configPath).canonicalFilePath().compare(configPath, Qt::CaseInsensitive) != 0)
        return false;
    traceChanged = false;
    fileChanged = false;
    return true;
}
