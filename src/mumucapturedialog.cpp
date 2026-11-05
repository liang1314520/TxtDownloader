#include "mumucapturedialog.h"
#include "mumucapture.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

MuMuCaptureDialog::MuMuCaptureDialog(QWidget *parent, bool captureCategory)
    : QDialog(parent), capture(new MuMuCapture(this)), categoryOnly(captureCategory)
{
    setWindowTitle(categoryOnly ? QStringLiteral("MuMu 采集添加分类") : QStringLiteral("MuMu 自动采集会话"));
    resize(780, 520);
    auto *layout = new QVBoxLayout(this);
    auto *notice = new QLabel(QStringLiteral(
        "仅支持 MuMu 自带的网卡记录功能，不使用 Android 代理，不安装证书、不修改 APK。\n"
        "请先手动启动目标 MuMu 实例并打开自己的 App，关闭模拟器内其他应用。\n"
        "底层会把所选实例的整个网卡流量写入临时 PCAP（可能含其他账号或应用数据）；"
        "本工具只提取目标小说请求，60 秒或达到 32 MiB 时停止（轮询及关闭期间可能短暂超出）。\n"
        "捕获后先关闭记录并恢复原设置，再导入会话。正常结束删除临时文件；"
        "异常退出、恢复或删除失败可能留下敏感文件；临时目录中的 recovery.json 记录原设置，必须按提示人工处理。"), this);
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    if (categoryOnly) {
        auto *help = new QLabel(QStringLiteral("开始后，请在 App 中打开目标小说分类；已打开时需刷新或重新进入。只识别带分类条件的列表请求，随后由你填写名称确认添加，不会自动加载书库。"), this);
        help->setTextFormat(Qt::PlainText);
        help->setWordWrap(true);
        layout->addWidget(help);
    }
    options = new QWidget(this);
    auto *form = new QFormLayout(options);
    instances = new QComboBox(options);
    instances->setObjectName(QStringLiteral("mumuCaptureInstanceCombo"));
    auto *detect = new QPushButton(QStringLiteral("重新检测"), options);
    detect->setObjectName(QStringLiteral("detectMuMuInstancesButton"));
    auto *instanceRow = new QHBoxLayout;
    instanceRow->addWidget(instances, 1);
    instanceRow->addWidget(detect);
    form->addRow(QStringLiteral("检测到的实例"), instanceRow);
    toolPath = new QLineEdit(options);
    toolPath->setObjectName(QStringLiteral("mumuCaptureToolEdit"));
    toolPath->setPlaceholderText(QStringLiteral("MuMu 安装目录/nx_device/15.0/hypervisor/VBoxManage.exe"));
    configPath = new QLineEdit(options);
    configPath->setObjectName(QStringLiteral("mumuCaptureConfigEdit"));
    configPath->setPlaceholderText(QStringLiteral("MuMu 安装目录/vms/实例名/实例名.nemu"));
    auto *chooseTool = new QPushButton(QStringLiteral("选择工具…"), options);
    auto *chooseConfig = new QPushButton(QStringLiteral("选择实例…"), options);
    auto *toolRow = new QHBoxLayout;
    toolRow->addWidget(toolPath);
    toolRow->addWidget(chooseTool);
    auto *configRow = new QHBoxLayout;
    configRow->addWidget(configPath);
    configRow->addWidget(chooseConfig);
    form->addRow(QStringLiteral("MuMu 管理工具"), toolRow);
    form->addRow(QStringLiteral("实例配置（只读）"), configRow);
    adapter = new QSpinBox(options);
    adapter->setObjectName(QStringLiteral("mumuCaptureAdapterSpin"));
    adapter->setRange(1, 8);
    adapter->setValue(1);
    form->addRow(QStringLiteral("网卡编号"), adapter);
    consent = new QCheckBox(QStringLiteral("我确认这是自己的实例，并同意本次短暂记录其全网卡流量及临时文件风险"), options);
    consent->setObjectName(QStringLiteral("mumuCaptureConsentCheck"));
    form->addRow(consent);
    layout->addWidget(options);
    auto *actions = new QHBoxLayout;
    startButton = new QPushButton(QStringLiteral("开始采集"), this);
    startButton->setObjectName(QStringLiteral("startMuMuCaptureButton"));
    stopButton = new QPushButton(QStringLiteral("停止并恢复"), this);
    stopButton->setObjectName(QStringLiteral("stopMuMuCaptureButton"));
    closeButton = new QPushButton(QStringLiteral("关闭"), this);
    actions->addWidget(startButton);
    actions->addWidget(stopButton);
    actions->addStretch();
    actions->addWidget(closeButton);
    layout->addLayout(actions);
    status = new QLabel(QStringLiteral("未开始采集。选择 MuMu 工具与对应 .nemu 实例配置后，勾选授权并点击开始。"), this);
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status);
    layout->addStretch();
    connect(chooseTool, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 MuMu 自带的 VBoxManage.exe"),
            toolPath->text(), QStringLiteral("MuMu 管理工具 (VBoxManage.exe)"));
        if (!path.isEmpty()) {
            instances->setCurrentIndex(0);
            toolPath->setText(path);
        }
    });
    connect(chooseConfig, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("选择对应 MuMu 实例配置"),
            configPath->text(), QStringLiteral("MuMu 实例 (*.nemu)"));
        if (!path.isEmpty()) {
            instances->setCurrentIndex(0);
            configPath->setText(path);
        }
    });
    connect(detect, &QPushButton::clicked, this, &MuMuCaptureDialog::detectInstances);
    connect(instances, &QComboBox::currentIndexChanged, this, &MuMuCaptureDialog::selectInstance);
    connect(toolPath, &QLineEdit::textEdited, this, [this] { instances->setCurrentIndex(0); });
    connect(configPath, &QLineEdit::textEdited, this, [this] { instances->setCurrentIndex(0); });
    connect(toolPath, &QLineEdit::textChanged, this, [this] {
        consent->setChecked(false);
        updateActions();
    });
    connect(configPath, &QLineEdit::textChanged, this, [this] {
        consent->setChecked(false);
        updateActions();
    });
    connect(adapter, &QSpinBox::valueChanged, this, [this] { consent->setChecked(false); });
    connect(consent, &QCheckBox::toggled, this, &MuMuCaptureDialog::updateActions);
    connect(startButton, &QPushButton::clicked, this, &MuMuCaptureDialog::startCapture);
    connect(stopButton, &QPushButton::clicked, capture, &MuMuCapture::stop);
    connect(closeButton, &QPushButton::clicked, this, &MuMuCaptureDialog::reject);
    connect(capture, &MuMuCapture::statusChanged, status, &QLabel::setText);
    connect(capture, &MuMuCapture::busyChanged, this, [this](bool busy) {
        options->setEnabled(!busy);
        updateActions();
        emit busyChanged(busy);
    });
    connect(capture, &MuMuCapture::requestReady, this, &MuMuCaptureDialog::requestReady);
    connect(capture, &MuMuCapture::finished, this, [this](bool success, const QString &message) {
        status->setText(success && !importMessage.isEmpty() ? importMessage + QStringLiteral("\n") + message : message);
        if (capture->needsRecovery() || capture->hasCleanupWarning()) {
            QMessageBox::warning(this, QStringLiteral("需要人工处理采集记录"), message);
        }
        updateActions();
        if (closePending)
            QDialog::reject();
    });
    detectInstances();
    updateActions();
}

MuMuCaptureDialog::~MuMuCaptureDialog() = default;

void MuMuCaptureDialog::detectInstances()
{
    if (capture->isBusy())
        return;
    detectedInstances = MuMuDiscovery::scanInstallations(MuMuDiscovery::installationRoots());
    {
        const QSignalBlocker blocker(instances);
        instances->clear();
        instances->addItem(QStringLiteral("请选择实例，或手动选择下方路径"));
        for (const auto &instance : detectedInstances)
            instances->addItem(QStringLiteral("%1 · %2").arg(instance.name, QDir::toNativeSeparators(instance.configPath)));
        instances->setCurrentIndex(0);
    }
    if (detectedInstances.size() == 1 && toolPath->text().isEmpty() && configPath->text().isEmpty())
        instances->setCurrentIndex(1);
    status->setText(detectedInstances.isEmpty()
        ? QStringLiteral("没有检测到可配对的 MuMu 工具与实例，请手动选择；不会启动模拟器或自动开启采集。")
        : detectedInstances.size() == 1
            ? QStringLiteral("检测到 1 个 MuMu 实例。请核对下方路径，手动启动目标 App，再勾选授权并开始。")
            : QStringLiteral("检测到 %1 个 MuMu 实例，请在下拉列表选择目标；不会自动选取或启动实例。")
                  .arg(detectedInstances.size()));
    updateActions();
}

void MuMuCaptureDialog::selectInstance(int index)
{
    if (capture->isBusy() || index < 1 || index > detectedInstances.size())
        return;
    const auto &instance = detectedInstances[index - 1];
    toolPath->setText(QDir::toNativeSeparators(instance.toolPath));
    configPath->setText(QDir::toNativeSeparators(instance.configPath));
}

void MuMuCaptureDialog::setImportResult(const QString &error, const QString &sessionStatus)
{
    importMessage = error.isEmpty()
        ? QStringLiteral("目标请求会话已导入。%1").arg(sessionStatus)
        : QStringLiteral("目标请求已捕获，但会话导入失败，原会话保留：%1").arg(error);
}

void MuMuCaptureDialog::setResultMessage(const QString &message)
{
    importMessage = message;
}

void MuMuCaptureDialog::updateActions()
{
    const bool busy = capture->isBusy();
    startButton->setEnabled(!busy && !capture->needsRecovery() && consent->isChecked()
        && QFileInfo(toolPath->text().trimmed()).isFile() && QFileInfo(configPath->text().trimmed()).isFile());
    stopButton->setEnabled(busy);
    closeButton->setText(busy ? QStringLiteral("停止并关闭") : QStringLiteral("关闭"));
}

void MuMuCaptureDialog::startCapture()
{
    importMessage.clear();
    closePending = false;
    capture->start(toolPath->text().trimmed(), configPath->text().trimmed(), adapter->value(), categoryOnly);
}

void MuMuCaptureDialog::reject()
{
    if (capture->isBusy()) {
        closePending = true;
        capture->stop();
        status->setText(QStringLiteral("正在关闭采集并恢复原记录设置，请勿强行退出程序…"));
        return;
    }
    QDialog::reject();
}

void MuMuCaptureDialog::closeEvent(QCloseEvent *event)
{
    if (capture->isBusy()) {
        event->ignore();
        reject();
        return;
    }
    QDialog::closeEvent(event);
}
