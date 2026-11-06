#include "mumucapturedialog.h"
#include "mumucapture.h"
#include "workbenchtheme.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

MuMuCaptureDialog::MuMuCaptureDialog(QWidget *parent, bool captureCategory)
    : QDialog(parent), capture(new MuMuCapture(this)), categoryOnly(captureCategory)
{
    setWindowTitle(categoryOnly ? QStringLiteral("MuMu 采集添加分类") : QStringLiteral("MuMu 采集工作台"));
    setStyleSheet(WorkbenchTheme::styleSheet());
    resize(860, 820);
    setMinimumSize(680, 620);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 20);
    layout->setSpacing(14);
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("LOCAL CONNECTION / 本机连接"), "eyebrow", this));
    layout->addWidget(WorkbenchTheme::label(categoryOnly ? QStringLiteral("采集并添加小说分类") : QStringLiteral("MuMu 采集工作台"), "pageTitle", this));
    auto *steps = new QHBoxLayout;
    for (const auto &text : {QStringLiteral("01 / 选择实例"), QStringLiteral("02 / 授权采集"), QStringLiteral("03 / 恢复完成")}) {
        auto *step = WorkbenchTheme::panel(this);
        auto *stepLayout = new QVBoxLayout(step);
        stepLayout->setContentsMargins(14, 12, 14, 12);
        stepLayout->addWidget(WorkbenchTheme::label(text, "eyebrow", step));
        steps->addWidget(step);
    }
    layout->addLayout(steps);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *body = new QWidget(scroll);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(14);
    auto *notice = new QLabel(QStringLiteral(
        "仅支持 MuMu 自带的网卡记录功能，不使用 Android 代理，不安装证书、不修改 APK。\n"
        "请先手动启动目标 MuMu 实例并打开自己的 App，关闭模拟器内其他应用。\n"
        "底层会把所选实例的整个网卡流量写入临时 PCAP（可能含其他账号或应用数据）；"
        "本工具只提取目标小说请求，60 秒或达到 32 MiB 时停止（轮询及关闭期间可能短暂超出）。\n"
        "捕获后先关闭记录并恢复原设置，再导入会话。正常结束删除临时文件；"
        "异常退出、恢复或删除失败可能留下敏感文件；临时目录中的 recovery.json 记录原设置，必须按提示人工处理。"), this);
    notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true);
    notice->setProperty("role", "notice");
    bodyLayout->addWidget(notice);
    if (categoryOnly) {
        auto *help = WorkbenchTheme::label(QStringLiteral("先进入源 App 的小说页面。开始采集后，点击你要添加的分类标签；标签命中缓存时可能没有新请求。只识别带分类条件的列表请求，随后由你确认条件并填写名称；不会自动遍历分类或加载书库。"), "status", body);
        bodyLayout->addWidget(help);
    }
    options = WorkbenchTheme::panel(body);
    auto *form = new QFormLayout(options);
    form->setContentsMargins(18, 18, 18, 18);
    form->setSpacing(12);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
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
    bodyLayout->addWidget(options);
    bodyLayout->addStretch();
    scroll->setWidget(body);
    layout->addWidget(scroll, 1);
    auto *actions = new QHBoxLayout;
    startButton = new QPushButton(QStringLiteral("开始采集"), this);
    startButton->setObjectName(QStringLiteral("startMuMuCaptureButton"));
    startButton->setProperty("primary", true);
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
    status->setProperty("role", "status");
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status);
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
