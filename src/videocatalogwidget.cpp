#include "videocatalogwidget.h"
#include "videocatalogcontroller.h"
#include "videocover.h"
#include "videolistmodel.h"
#include "workbenchtheme.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QPushButton *button(const QString &text, const char *name, QWidget *parent)
{
    auto *result = new QPushButton(text, parent);
    result->setObjectName(QString::fromLatin1(name));
    return result;
}
QString snapshotText(const VideoSnapshot &snapshot)
{
    if (!snapshot.page)
        return QStringLiteral("本机暂无结果；请主动加载。封面网络访问尚未启用。");
    const auto identity = snapshot.query.kind == VideoApi::QueryKind::Search
        ? QStringLiteral("关键词「%1」 · 分区 %2").arg(snapshot.query.keyword).arg(static_cast<int>(snapshot.query.part))
        : snapshot.query.parentId.isEmpty() ? QStringLiteral("全部分类")
            : QStringLiteral("分类 ID：%1 / %2").arg(snapshot.query.parentId, snapshot.query.childId.isEmpty() ? QStringLiteral("全部子项") : snapshot.query.childId);
    return QStringLiteral("%1 · 第 %2 页 · %3 条 · %4\n缓存时间：%5")
        .arg(identity).arg(snapshot.page).arg(snapshot.items.size())
        .arg(snapshot.complete ? QStringLiteral("已确认到末页") : QStringLiteral("未确认完整"))
        .arg(snapshot.updatedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
}
}
VideoCatalogWidget::VideoCatalogWidget(QWidget *parent) : VideoCatalogWidget(VideoStoragePaths::defaults(), parent) {}
VideoCatalogWidget::VideoCatalogWidget(const VideoStoragePaths &paths, QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("videoCatalogWidget"));
    setProperty("pageSurface", true);
    controller = new VideoCatalogController(paths, this);
    covers = new VideoCoverCache(paths, this);
    categoryModel = new VideoListModel(covers, this);
    searchModel = new VideoListModel(covers, this);
    favoritesModel = new VideoListModel(covers, this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("VIDEO CATALOG / 本机收藏与按需浏览"), "eyebrow", this));
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    pages = new QStackedWidget(splitter);
    pages->setObjectName(QStringLiteral("videoWorkspaceStack"));
    auto *catalog = new QWidget(pages);
    auto *catalogLayout = new QVBoxLayout(catalog);
    catalogLayout->setContentsMargins(0, 0, 0, 0);
    catalogLayout->addWidget(WorkbenchTheme::label(QStringLiteral("视频分类与搜索"), "pageTitle", catalog));
    auto *categories = new QHBoxLayout;
    parents = new QComboBox(catalog);
    parents->setObjectName(QStringLiteral("videoParentCategoryCombo"));
    children = new QComboBox(catalog);
    children->setObjectName(QStringLiteral("videoChildCategoryCombo"));
    categories->addWidget(parents, 1);
    categories->addWidget(children, 1);
    catalogLayout->addLayout(categories);
    auto *categoryActions = new QHBoxLayout;
    refreshCategoriesButton = button(QStringLiteral("刷新原分类"), "videoRefreshCategoriesButton", catalog);
    loadCategoryButton = button(QStringLiteral("加载所选分类"), "videoLoadCategoryButton", catalog);
    orderButton = button(QStringLiteral("本机排序…"), "videoCategoryOrderButton", catalog);
    categoryActions->addWidget(refreshCategoriesButton);
    categoryActions->addWidget(loadCategoryButton);
    categoryActions->addWidget(orderButton);
    categoryActions->addStretch();
    catalogLayout->addLayout(categoryActions);
    auto *search = new QHBoxLayout;
    searchEdit = new QLineEdit(catalog);
    searchEdit->setObjectName(QStringLiteral("videoSearchEdit"));
    searchEdit->setMaxLength(256);
    searchEdit->setPlaceholderText(QStringLiteral("输入关键词；仅搜索所选分区"));
    searchEdit->setClearButtonEnabled(true);
    parts = new QComboBox(catalog);
    parts->setObjectName(QStringLiteral("videoSearchPartCombo"));
    parts->addItem(QStringLiteral("视频区"), 2);
    parts->addItem(QStringLiteral("AV区"), 1);
    parts->addItem(QStringLiteral("动漫区"), 3);
    parts->addItem(QStringLiteral("禁区"), 4);
    searchButton = button(QStringLiteral("搜索"), "videoSearchButton", catalog);
    search->addWidget(searchEdit, 1);
    search->addWidget(parts);
    search->addWidget(searchButton);
    catalogLayout->addLayout(search);
    results = new QTabWidget(catalog);
    results->setObjectName(QStringLiteral("videoResultsTabs"));
    const auto makeResults = [this](QWidget *parentPage, VideoListModel *model, const char *name, QLabel *&summary) {
        auto *box = new QVBoxLayout(parentPage);
        box->setContentsMargins(0, 8, 0, 0);
        summary = WorkbenchTheme::label({}, "muted", parentPage);
        box->addWidget(summary);
        auto *view = new QListView(parentPage);
        view->setObjectName(QString::fromLatin1(name));
        view->setModel(model);
        view->setUniformItemSizes(true);
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *delegate = new VideoCardDelegate(view);
        view->setItemDelegate(delegate);
        box->addWidget(view, 1);
        connect(delegate, &VideoCardDelegate::favoriteRequested, this, [this, model](const QModelIndex &index) {
            toggleFavorite(model->item(index));
        });
        connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &) { showCurrentDetails(); });
        return view;
    };
    auto *categoryResults = new QWidget(results);
    categoryView = makeResults(categoryResults, categoryModel, "videoResultsView", categoryStatus);
    results->addTab(categoryResults, QStringLiteral("分类列表"));
    auto *searchResults = new QWidget(results);
    searchView = makeResults(searchResults, searchModel, "videoSearchResultsView", searchStatus);
    results->addTab(searchResults, QStringLiteral("搜索结果"));
    catalogLayout->addWidget(results, 1);
    loadMoreButton = button(QStringLiteral("加载更多"), "videoLoadMoreButton", catalog);
    catalogLayout->addWidget(loadMoreButton);
    pages->addWidget(catalog);
    auto *favoritePage = new QWidget(pages);
    auto *favoriteLayout = new QVBoxLayout(favoritePage);
    favoriteLayout->setContentsMargins(0, 0, 0, 0);
    favoriteLayout->addWidget(WorkbenchTheme::label(QStringLiteral("视频本机收藏"), "pageTitle", favoritePage));
    favoriteLayout->addWidget(WorkbenchTheme::label(QStringLiteral("仅保存展示资料。进入收藏不联网，不自动更新详情或封面。"), "muted", favoritePage));
    favoritesFilter = new QLineEdit(favoritePage);
    favoritesFilter->setObjectName(QStringLiteral("videoFavoritesFilterEdit"));
    favoritesFilter->setPlaceholderText(QStringLiteral("筛选本机收藏标题或分类"));
    favoritesFilter->setClearButtonEnabled(true);
    favoriteLayout->addWidget(favoritesFilter);
    auto *favoriteResults = new QWidget(favoritePage);
    QLabel *favoriteSummary = nullptr;
    favoritesView = makeResults(favoriteResults, favoritesModel, "videoFavoritesView", favoriteSummary);
    favoriteSummary->setText(QStringLiteral("没有本机缩略图时显示占位；点击“已收藏”可移除。"));
    favoriteLayout->addWidget(favoriteResults, 1);
    pages->addWidget(favoritePage);
    auto *connectionPage = new QWidget(pages);
    auto *connectionLayout = new QVBoxLayout(connectionPage);
    connectionLayout->setContentsMargins(0, 0, 0, 0);
    connectionLayout->addWidget(WorkbenchTheme::label(QStringLiteral("视频会话与连接"), "pageTitle", connectionPage));
    connectionStatus = WorkbenchTheme::label({}, "status", connectionPage);
    connectionStatus->setObjectName(QStringLiteral("videoConnectionStatusLabel"));
    connectionLayout->addWidget(connectionStatus);
    connectionLayout->addWidget(WorkbenchTheme::label(QStringLiteral("适配 App 1.0.7。仅使用两个已核实的精确 HTTPS 服务来源，不自动发现新域或切换来源。\n首次联网会登记一个固定随机游客身份；不采集硬件标识、不登录账号，不提供播放或视频下载。\n封面 CDN 尚未核实，网络封面暂未启用。"), "notice", connectionPage));
    retrySessionButton = button(QStringLiteral("初始化／重试保存游客会话"), "videoRetrySessionButton", connectionPage);
    clearSessionButton = button(QStringLiteral("清除游客会话…"), "videoClearSessionButton", connectionPage);
    connectionLayout->addWidget(retrySessionButton);
    connectionLayout->addWidget(clearSessionButton);
    connectionLayout->addStretch();
    pages->addWidget(connectionPage);
    auto *detailPanel = WorkbenchTheme::panel(splitter);
    detailPanel->setMinimumWidth(230);
    auto *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->addWidget(WorkbenchTheme::label(QStringLiteral("已选视频资料"), "sectionTitle", detailPanel));
    detailsView = new QPlainTextEdit(detailPanel);
    detailsView->setObjectName(QStringLiteral("videoDetailsView"));
    detailsView->setReadOnly(true);
    detailsView->setPlaceholderText(QStringLiteral("选择条目查看本机资料；不会自动请求详情。"));
    detailLayout->addWidget(detailsView, 1);
    refreshDetailsButton = button(QStringLiteral("获取最新详情"), "videoRefreshDetailsButton", detailPanel);
    detailLayout->addWidget(refreshDetailsButton);
    detailLayout->addWidget(WorkbenchTheme::label(QStringLiteral("仅展示元数据，不包含播放链接、播放器或下载功能。"), "muted", detailPanel));
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    auto *bottom = new QHBoxLayout;
    status = WorkbenchTheme::label(QStringLiteral("离线就绪。仅点击加载、搜索或详情按钮后联网。"), "status", this);
    status->setObjectName(QStringLiteral("videoStatusLabel"));
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bottom->addWidget(status, 1);
    cancelButton = button(QStringLiteral("取消视频请求"), "videoCancelButton", this);
    bottom->addWidget(cancelButton);
    layout->addLayout(bottom);

    connect(controller, &VideoCatalogController::categoriesChanged, this, &VideoCatalogWidget::updateCategories);
    connect(controller, &VideoCatalogController::categoryResultsChanged, this, [this] { updateResults(false); });
    connect(controller, &VideoCatalogController::searchResultsChanged, this, [this] { updateResults(true); });
    connect(controller, &VideoCatalogController::favoritesChanged, this, &VideoCatalogWidget::updateFavorites);
    connect(controller, &VideoCatalogController::detailsChanged, this, &VideoCatalogWidget::updateDetails);
    connect(controller, &VideoCatalogController::connectionChanged, this, &VideoCatalogWidget::updateConnection);
    connect(controller, &VideoCatalogController::errorOccurred, this, &VideoCatalogWidget::showError);
    connect(controller, &VideoCatalogController::busyChanged, this, [this](bool busy) {
        updateActions();
        if (busy)
            status->setText(QStringLiteral("正在请求视频元数据，可取消；切换模块不会中断请求。"));
        else if (status->text().startsWith(QStringLiteral("正在请求")))
            status->setText(QStringLiteral("视频请求已结束。封面网络访问尚未启用。"));
        emit busyChanged(busy);
    });
    connect(controller, &VideoCatalogController::consentRequired, this, [this] {
        QMessageBox prompt(QMessageBox::Question, QStringLiteral("允许视频游客访问"),
            QStringLiteral("继续后将向已核实的视频 HTTPS 服务发送一个固定随机 UUID，以创建或复用游客会话，并请求本次选择的分类、列表、搜索或元数据详情。\n\n不上传小说文件，不采集硬件标识，不请求媒体内容。游客凭证使用当前 Windows 用户的 DPAPI 保存。\n\n是否允许本次启动期间按需访问？"),
            QMessageBox::Yes | QMessageBox::No, this);
        prompt.setTextFormat(Qt::PlainText);
        prompt.setDefaultButton(QMessageBox::No);
        controller->grantConsent(prompt.exec() == QMessageBox::Yes);
    });
    connect(parents, &QComboBox::currentIndexChanged, this, [this] {
        updateChildren();
        selectCachedCategory();
    });
    connect(children, &QComboBox::currentIndexChanged, this, &VideoCatalogWidget::selectCachedCategory);
    connect(refreshCategoriesButton, &QPushButton::clicked, controller, &VideoCatalogController::refreshCategories);
    connect(loadCategoryButton, &QPushButton::clicked, this, [this] {
        results->setCurrentIndex(0);
        controller->loadCategory(parents->currentData().toString(), children->currentData().toString());
    });
    const auto startSearch = [this] {
        if (controller->isBusy())
            return;
        results->setCurrentIndex(1);
        controller->search(searchEdit->text(), static_cast<VideoApi::SearchPart>(parts->currentData().toInt()));
    };
    connect(searchButton, &QPushButton::clicked, this, startSearch);
    connect(searchEdit, &QLineEdit::returnPressed, this, startSearch);
    connect(searchEdit, &QLineEdit::textChanged, this, &VideoCatalogWidget::updateActions);
    connect(results, &QTabWidget::currentChanged, this, [this] {
        showCurrentDetails();
        updateActions();
    });
    connect(loadMoreButton, &QPushButton::clicked, this, [this] {
        controller->loadMore(results->currentIndex() == 0 ? VideoApi::QueryKind::Category : VideoApi::QueryKind::Search);
    });
    connect(refreshDetailsButton, &QPushButton::clicked, this, [this] { controller->refreshDetails(controller->details().id); });
    connect(cancelButton, &QPushButton::clicked, controller, &VideoCatalogController::cancel);
    connect(favoritesFilter, &QLineEdit::textChanged, this, &VideoCatalogWidget::updateFavorites);
    connect(orderButton, &QPushButton::clicked, this, &VideoCatalogWidget::showOrderDialog);
    connect(retrySessionButton, &QPushButton::clicked, controller, &VideoCatalogController::retrySession);
    connect(clearSessionButton, &QPushButton::clicked, this, [this] {
        QMessageBox prompt(QMessageBox::Question, QStringLiteral("清除视频游客会话"),
            QStringLiteral("仅清除本机视频游客凭证，保留固定身份、已尝试登记记录、视频收藏、分类和缓存。小说资料不受影响。\n下次登记须由你主动重试，且仍复用原身份。是否清除？"),
            QMessageBox::Yes | QMessageBox::No, this);
        prompt.setTextFormat(Qt::PlainText);
        prompt.setDefaultButton(QMessageBox::No);
        if (prompt.exec() == QMessageBox::Yes)
            showError(controller->clearSession());
    });
    updateCategories();
    updateResults(false);
    updateResults(true);
    updateFavorites();
    updateConnection();
    updateActions();
    if (!controller->startupError().isEmpty())
        showError(controller->startupError());
}
void VideoCatalogWidget::setPage(Page page)
{
    pages->setCurrentIndex(static_cast<int>(page));
    showCurrentDetails();
}
void VideoCatalogWidget::showCurrentDetails()
{
    if (pages->currentIndex() == static_cast<int>(Page::Connection))
        return;
    auto *view = pages->currentIndex() == static_cast<int>(Page::Favorites)
        ? favoritesView : results->currentIndex() == 0 ? categoryView : searchView;
    auto *model = static_cast<VideoListModel *>(view->model());
    if (view->currentIndex().isValid())
        controller->showSavedDetails(model->item(view->currentIndex()));
}
void VideoCatalogWidget::showError(const QString &message)
{
    if (!message.isEmpty())
        status->setText(message);
}
void VideoCatalogWidget::updateChildren(const QString &selected)
{
    const QSignalBlocker blocker(children);
    children->clear();
    children->addItem(QStringLiteral("全部子分类"), QString());
    const auto id = parents->currentData().toString();
    for (const auto &parent : controller->categories()) {
        if (parent.id != id)
            continue;
        for (const auto &childId : controller->categoryOrder().children.value(id)) {
            const auto found = std::find_if(parent.children.cbegin(), parent.children.cend(), [&childId](const auto &c) { return c.id == childId; });
            if (found != parent.children.cend())
                children->addItem(found->name, found->id);
        }
    }
    children->setCurrentIndex(qMax(0, children->findData(selected)));
}
void VideoCatalogWidget::updateCategories()
{
    const auto parentId = parents->currentData().toString();
    const auto childId = children->currentData().toString();
    {
        const QSignalBlocker blocker(parents);
        parents->clear();
        parents->addItem(QStringLiteral("全部分类"), QString());
        for (const auto &id : controller->categoryOrder().parents) {
            const auto &source = controller->categories();
            const auto found = std::find_if(source.cbegin(), source.cend(), [&id](const auto &p) { return p.id == id; });
            if (found != source.cend())
                parents->addItem(found->name, found->id);
        }
        parents->setCurrentIndex(qMax(0, parents->findData(parentId)));
    }
    updateChildren(childId);
    if (!controller->isBusy() && ((!parentId.isEmpty() && parents->currentData().toString() != parentId)
        || (!childId.isEmpty() && children->currentData().toString() != childId)))
        selectCachedCategory();
    updateActions();
}
void VideoCatalogWidget::selectCachedCategory()
{
    showError(controller->selectCachedCategory(parents->currentData().toString(), children->currentData().toString()));
    updateActions();
}
void VideoCatalogWidget::updateResults(bool search)
{
    auto *model = search ? searchModel : categoryModel;
    auto *view = search ? searchView : categoryView;
    const auto selected = model->item(view->currentIndex()).id;
    const auto scroll = view->verticalScrollBar()->value();
    const auto &snapshot = search ? controller->searchSnapshot() : controller->categorySnapshot();
    model->setItems(snapshot.items);
    for (int row = 0; row < model->rowCount(); ++row) {
        if (model->data(model->index(row), VideoListModel::IdRole).toString() == selected) {
            view->setCurrentIndex(model->index(row));
            break;
        }
    }
    view->verticalScrollBar()->setValue(scroll);
    (search ? searchStatus : categoryStatus)->setText(snapshotText(snapshot));
    updateActions();
}
void VideoCatalogWidget::updateFavorites()
{
    QSet<QString> ids;
    QVector<VideoApi::Summary> filtered;
    const auto keyword = favoritesFilter->text().trimmed();
    for (const auto &item : controller->favorites()) {
        ids.insert(item.id);
        if (keyword.isEmpty() || item.title.contains(keyword, Qt::CaseInsensitive)
            || item.categoryText.contains(keyword, Qt::CaseInsensitive))
            filtered.append(item);
    }
    categoryModel->setFavorites(ids);
    searchModel->setFavorites(ids);
    favoritesModel->setFavorites(ids);
    const auto selected = favoritesModel->item(favoritesView->currentIndex()).id;
    const auto scroll = favoritesView->verticalScrollBar()->value();
    favoritesModel->setItems(filtered);
    for (int row = 0; row < favoritesModel->rowCount(); ++row) {
        if (favoritesModel->data(favoritesModel->index(row), VideoListModel::IdRole).toString() == selected) {
            favoritesView->setCurrentIndex(favoritesModel->index(row));
            break;
        }
    }
    favoritesView->verticalScrollBar()->setValue(scroll);
}
void VideoCatalogWidget::toggleFavorite(const VideoApi::Summary &item)
{
    showError(controller->setFavorite(item, !controller->isFavorite(item.id)));
}
void VideoCatalogWidget::updateDetails()
{
    const auto &details = controller->details();
    const QStringList lines{details.title, QStringLiteral("ID：%1").arg(details.id),
        QStringLiteral("时间：%1").arg(details.createdAt), QStringLiteral("浏览：%1").arg(details.viewsText),
        QStringLiteral("评论：%1 · 来源收藏数：%2").arg(details.commentsText, details.favoritesText),
        QStringLiteral("标签：%1").arg(details.labels.join(QStringLiteral("、"))),
        QStringLiteral("人员：%1").arg(details.people.join(QStringLiteral("、")))};
    detailsView->setPlainText(lines.join(QLatin1Char('\n')));
    updateActions();
}
void VideoCatalogWidget::updateConnection()
{
    connectionStatus->setText(controller->hasSession()
        ? QStringLiteral("已保存当前 Windows 用户可解密的视频游客会话。尚未进行本次在线业务验收。")
        : QStringLiteral("尚无可用的视频游客凭证。启动和切页不会创建身份或联网。"));
    updateActions();
}
void VideoCatalogWidget::updateActions()
{
    const bool busy = controller->isBusy();
    parents->setEnabled(!busy);
    children->setEnabled(!busy && !parents->currentData().toString().isEmpty());
    refreshCategoriesButton->setEnabled(!busy);
    orderButton->setEnabled(!busy && !controller->categories().isEmpty());
    VideoApi::Query query;
    loadCategoryButton->setEnabled(!busy && VideoApi::categoryQuery(controller->categories(), parents->currentData().toString(),
                                                                  children->currentData().toString(), query).isEmpty());
    searchButton->setEnabled(!busy && !searchEdit->text().trimmed().isEmpty());
    const auto &snapshot = results->currentIndex() == 0 ? controller->categorySnapshot() : controller->searchSnapshot();
    loadMoreButton->setEnabled(!busy && snapshot.page > 0 && snapshot.hasMore);
    refreshDetailsButton->setEnabled(!busy && !controller->details().id.isEmpty());
    retrySessionButton->setEnabled(!busy);
    clearSessionButton->setEnabled(!busy);
    cancelButton->setEnabled(busy);
}
void VideoCatalogWidget::showOrderDialog()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("videoCategoryOrderDialog"));
    dialog.setWindowTitle(QStringLiteral("视频分类本机排序"));
    dialog.resize(480, 550);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(WorkbenchTheme::label(QStringLiteral("仅调整显示顺序；子分类只能在原父分类内移动。取消不保存。"), "muted", &dialog));
    auto *tree = new QTreeWidget(&dialog);
    tree->setObjectName(QStringLiteral("videoCategoryOrderTree"));
    tree->setHeaderHidden(true);
    tree->setDragDropMode(QAbstractItemView::NoDragDrop);
    for (const auto &id : controller->categoryOrder().parents) {
        const auto &source = controller->categories();
        const auto parent = std::find_if(source.cbegin(), source.cend(), [&id](const auto &p) { return p.id == id; });
        if (parent == source.cend())
            continue;
        auto *node = new QTreeWidgetItem(tree, {parent->name});
        node->setData(0, Qt::UserRole, id);
        for (const auto &childId : controller->categoryOrder().children.value(id)) {
            const auto child = std::find_if(parent->children.cbegin(), parent->children.cend(), [&childId](const auto &c) { return c.id == childId; });
            if (child != parent->children.cend()) {
                auto *leaf = new QTreeWidgetItem(node, {child->name});
                leaf->setData(0, Qt::UserRole, childId);
            }
        }
    }
    tree->expandAll();
    layout->addWidget(tree, 1);
    auto *moves = new QHBoxLayout;
    for (const int delta : {-1, 1}) {
        auto *move = button(delta < 0 ? QStringLiteral("上移") : QStringLiteral("下移"),
                            delta < 0 ? "videoCategoryMoveUpButton" : "videoCategoryMoveDownButton", &dialog);
        moves->addWidget(move);
        connect(move, &QPushButton::clicked, &dialog, [tree, delta] {
            auto *item = tree->currentItem();
            if (!item)
                return;
            auto *parent = item->parent();
            const int index = parent ? parent->indexOfChild(item) : tree->indexOfTopLevelItem(item);
            const int count = parent ? parent->childCount() : tree->topLevelItemCount();
            if (index + delta < 0 || index + delta >= count)
                return;
            if (parent)
                parent->insertChild(index + delta, parent->takeChild(index));
            else
                tree->insertTopLevelItem(index + delta, tree->takeTopLevelItem(index));
            tree->setCurrentItem(item);
        });
    }
    layout->addLayout(moves);
    auto *error = WorkbenchTheme::label({}, "status", &dialog);
    layout->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, tree, error, &dialog] {
        VideoCategoryOrder order;
        for (int index = 0; index < tree->topLevelItemCount(); ++index) {
            const auto *parent = tree->topLevelItem(index);
            const auto id = parent->data(0, Qt::UserRole).toString();
            order.parents.append(id);
            QStringList children;
            for (int child = 0; child < parent->childCount(); ++child)
                children.append(parent->child(child)->data(0, Qt::UserRole).toString());
            order.children.insert(id, children);
        }
        const auto message = controller->saveOrder(order);
        if (!message.isEmpty())
            error->setText(message);
        else
            dialog.accept();
    });
    dialog.exec();
}
