#include "ui/editor/EditorTabBar.h"
#include "Logger.hpp"
#include "ui/editor/MyTextEdit.h"
#include "ui/markdown/MarkdownMode.h"
#include "ui/markdown/HtmlPreviewMode.h"
#include "ui/tools/ImagePreviewer.h"
#include "ui/tools/SqliteBrowser.h"
#include "core/config/ConfigManager.h"
#include "core/editor/CodeSyntaxHighlighter.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QDebug>
#include "ui/dialog/ModernDialog.h"
#include <QPushButton>
#include <QAbstractButton>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QMainWindow>
#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QMouseEvent>

EditorTabBar::EditorTabBar(QWidget* parent)
    : QWidget(parent)
{
    // VSCode风格：标签栏 + 编辑器堆栈
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);

    // === 标签页栏（VSCode风格）===
    m_tabBar = new QTabBar(this);
    m_tabBar->setObjectName(QStringLiteral("editorTabBar"));
    m_tabBar->setTabsClosable(true);          // 显示关闭按钮
    m_tabBar->setMovable(true);               // 标签可拖拽排序
    m_tabBar->setExpanding(false);            // 不自动扩展
    m_tabBar->setDocumentMode(true);           // 文档模式（视觉风格）
    m_tabBar->setShape(QTabBar::RoundedNorth);
    m_tabBar->setElideMode(Qt::ElideRight);   // 文件名过长时省略
    m_tabBar->setMinimumHeight(36);
    m_tabBar->setMaximumHeight(36);

    // 隐藏滚动按钮（VSCode风格）— 由全局QSS管理
    // 不设置内联样式，避免与全局QSS冲突

    layout->addWidget(m_tabBar);

    // === 编辑器堆栈（多编辑器切换）===
    m_editorStack = new QStackedWidget(this);
    m_editorStack->setObjectName(QStringLiteral("editorStack"));
    layout->addWidget(m_editorStack);

    // 信号连接
    connect(m_tabBar, &QTabBar::currentChanged, this, &EditorTabBar::onTabChanged);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &EditorTabBar::onTabCloseRequested);

    // 安装事件过滤器以检测标签拖出独立窗口（V1.9）
    m_tabBar->installEventFilter(this);

    // R4: 闲置检测已提取到 IdleTabTracker，EditorTabBar 不再负责 LSP 文档生命周期
}

// ========== 标签页管理 ==========

void EditorTabBar::addNewTab()
{
    m_untitledCounter++;
    QString displayName = tr("未命名 %1").arg(m_untitledCounter);

    // 创建新编辑器
    MyTextEdit* editor = createEditor();

    int index = m_tabBar->addTab(displayName);
    m_editorStack->addWidget(editor);

    TabData data;
    data.filePath.clear();
    data.displayName = displayName;
    data.isModified = false;
    data.editor = editor;
    m_tabDataMap[index] = data;

    m_tabBar->setCurrentIndex(index);
    LOG_DEBUG("[EditorTabBar] 新建标签页:" << displayName << "索引:" << index);

    emit tabCountChanged(m_tabBar->count());
}

void EditorTabBar::openFileTab(const QString& filePath, const QString& content)
{
    if (filePath.isEmpty()) {
        addNewTab();
        // J1: 使用静默设置文本，抑制 textChanged 引发的补全弹窗
        if (auto* ed = qobject_cast<MyTextEdit*>(currentEditor()->asWidget()))
            ed->setPlainTextSilently(content);
        return;
    }

    // 检查是否已有该文件的标签页
    for (auto it = m_tabDataMap.begin(); it != m_tabDataMap.end(); ++it) {
        if (it.value().filePath == filePath) {
            switchToTab(it.key());
            LOG_DEBUG("[EditorTabBar] 文件已打开，切换到现有标签:" << filePath);
            return;
        }
    }

    // 检查是否为 .md 文件 → 使用 Markdown 模式
    if (filePath.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) {
        openMarkdownTab(filePath, content);
        return;
    }

    // 检查是否为 .html/.htm 文件 → 使用 HTML 预览模式
    if (filePath.endsWith(QStringLiteral(".html"), Qt::CaseInsensitive) ||
        filePath.endsWith(QStringLiteral(".htm"), Qt::CaseInsensitive)) {
        openHtmlPreviewTab(filePath, content);
        return;
    }

    // === M11: 图片文件检测 → 使用 ImagePreviewer 替代文本编辑器 ===
    static const QStringList imageExtensions = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp"),
        QStringLiteral("ico"), QStringLiteral("svg")
    };
    QFileInfo imgFi(filePath);
    QString imgSuffix = imgFi.suffix().toLower();
    if (imageExtensions.contains(imgSuffix)) {
        auto* previewer = new ImagePreviewer(m_editorStack);
        if (previewer->loadImage(filePath)) {
            // 获取图片尺寸信息用于标签标题
            QPixmap pix(filePath);
            QString sizeInfo = QStringLiteral("%1x%2").arg(pix.width()).arg(pix.height());
            QString displayName = QStringLiteral("%1 (%2)").arg(imgFi.fileName()).arg(sizeInfo);

            int index = m_tabBar->addTab(displayName);
            m_editorStack->addWidget(previewer);

            TabData data;
            data.filePath = filePath;
            data.displayName = displayName;
            data.isModified = false;
            data.isSpecial = true;           // 特殊标签（不可编辑）
            data.customWidget = previewer;   // ImagePreviewer 作为自定义Widget
            data.editor = nullptr;            // 无编辑器
            m_tabDataMap[index] = data;

            m_tabBar->setCurrentIndex(index);
            LOG_DEBUG("[EditorTabBar] 打开图片预览:" << displayName);

            emit tabCountChanged(m_tabBar->count());
            return;
        } else {
            // 加载失败，回退到普通文本模式显示
            delete previewer;
        }
    }

    // === M13: SQLite 数据库文件检测 → 使用 SqliteBrowser ===
    static const QStringList dbExtensions = {
        QStringLiteral("db"), QStringLiteral("sqlite"),
        QStringLiteral("sqlite3"), QStringLiteral("db3")
    };
    if (dbExtensions.contains(imgSuffix)) {
        auto* dbViewer = new SqliteBrowser(m_editorStack);
        if (dbViewer->openDatabase(filePath)) {
            QString displayName = QStringLiteral("%1 [SQLite]").arg(imgFi.fileName());

            int index = m_tabBar->addTab(displayName);
            m_editorStack->addWidget(dbViewer);

            TabData data;
            data.filePath = filePath;
            data.displayName = displayName;
            data.isModified = false;
            data.isSpecial = true;           // 特殊标签
            data.customWidget = dbViewer;     // SqliteBrowser 作为自定义Widget
            data.editor = nullptr;            // 无编辑器
            m_tabDataMap[index] = data;

            m_tabBar->setCurrentIndex(index);
            LOG_DEBUG("[EditorTabBar] 打开数据库浏览器:" << displayName);

            emit tabCountChanged(m_tabBar->count());
            return;
        } else {
            delete dbViewer;
        }
    }

    // 普通文本文件
    QFileInfo fi(filePath);
    QString displayName = fi.fileName();

    MyTextEdit* editor = createEditor();
    // J1: 使用静默设置文本，抑制 textChanged 引发的补全弹窗，避免打开文件时编辑器卡顿
    editor->setPlainTextSilently(content);

    // 根据文件后缀启用语法高亮（使用高亮器支持的语言列表）
    QString suffix = fi.suffix().toLower();
    if (CodeSyntaxHighlighter::getSupportedLanguages().contains(suffix)) {
        editor->enableSyntaxHighlighting(suffix);
    }

    int index = m_tabBar->addTab(displayName);
    m_editorStack->addWidget(editor);

    TabData data;
    data.filePath = filePath;
    data.displayName = displayName;
    data.isModified = false;
    data.isSpecial = false;
    data.editor = editor;
    m_tabDataMap[index] = data;

    m_tabBar->setCurrentIndex(index);
    LOG_DEBUG("[EditorTabBar] 打开文件标签:" << displayName << "路径:" << filePath);

    emit tabCountChanged(m_tabBar->count());
}

int EditorTabBar::addCustomTab(QWidget* widget, const QString& title, bool closable)
{
    if (!widget) return -1;

    // 检查是否已有同名标签页
    for (auto it = m_tabDataMap.begin(); it != m_tabDataMap.end(); ++it) {
        if (it.value().displayName == title && it.value().isSpecial) {
            switchToTab(it.key());
            return it.key();
        }
    }

    int index = m_tabBar->addTab(title);
    m_editorStack->addWidget(widget);

    TabData data;
    data.displayName = title;
    data.isModified = false;
    data.isSpecial = true;
    data.customWidget = widget;
    m_tabDataMap[index] = data;

    if (!closable) {
        // 不可关闭的标签页（隐藏关闭按钮）
        m_tabBar->setTabButton(index, QTabBar::RightSide, nullptr);
    }

    m_tabBar->setCurrentIndex(index);
    emit tabCountChanged(m_tabBar->count());

    LOG_DEBUG("[EditorTabBar] 添加自定义标签:" << title);
    return index;
}

int EditorTabBar::findCustomTabIndex(const QString& title) const
{
    for (auto it = m_tabDataMap.begin(); it != m_tabDataMap.end(); ++it) {
        if (it.value().displayName == title && it.value().isSpecial) {
            return it.key();
        }
    }
    return -1;
}

QList<MyTextEdit*> EditorTabBar::allMyTextEditors() const
{
    QList<MyTextEdit*> editors;
    for (auto it = m_tabDataMap.begin(); it != m_tabDataMap.end(); ++it) {
        if (it.value().editor) {
            editors.append(it.value().editor);
        }
    }
    return editors;
}

void EditorTabBar::openMarkdownTab(const QString& filePath, const QString& content)
{
    QFileInfo fi(filePath);
    QString displayName = fi.fileName();

    // 检查是否已打开
    for (auto it = m_tabDataMap.begin(); it != m_tabDataMap.end(); ++it) {
        if (it.value().filePath == filePath) {
            switchToTab(it.key());
            return;
        }
    }

    // 创建 MarkdownMode 分屏组件（左侧编辑 + 右侧预览）
    auto* mdMode = new MarkdownMode(m_editorStack);

    // P3-M02 子项1: 传递文件路径给 MarkdownMode（用于 TOC 折叠状态按文件记忆）
    mdMode->setFilePath(filePath);

    // 获取内部编辑器用于 TabData 关联
    MyTextEdit* editor = mdMode->editor();

    int index = m_tabBar->addTab(displayName);
    m_editorStack->addWidget(mdMode);

    TabData data;
    data.filePath = filePath;
    data.displayName = displayName;
    data.isModified = false;
    data.isSpecial = true;          // 标记为特殊标签，使用 customWidget 管理
    data.customWidget = mdMode;     // MarkdownMode 作为自定义Widget
    data.editor = editor;           // 保留编辑器引用，用于内容读写
    m_tabDataMap[index] = data;

    // 连接编辑器修改状态信号
    if (editor) {
        connect(editor->document(), &QTextDocument::modificationChanged,
                this, [this](bool modified) { onEditorModified(modified); });
    }

    m_tabBar->setCurrentIndex(index);
    // 先触发 currentEditorChanged → bindCurrentEditor → setCompleter
    // 再设置内容，避免 textChanged 时补全器未初始化
    mdMode->setContent(content);
    LOG_DEBUG("[EditorTabBar] 打开MD分屏标签:" << displayName);

    emit tabCountChanged(m_tabBar->count());
}

void EditorTabBar::openHtmlPreviewTab(const QString& filePath, const QString& content)
{
    QFileInfo fi(filePath);
    QString displayName = fi.fileName();

    // 检查是否已打开
    for (auto it = m_tabDataMap.begin(); it != m_tabDataMap.end(); ++it) {
        if (it.value().filePath == filePath) {
            switchToTab(it.key());
            return;
        }
    }

    // 创建 HtmlPreviewMode 分屏组件（左侧编辑 + 右侧预览）
    auto* htmlMode = new HtmlPreviewMode(m_editorStack);
    htmlMode->setFilePath(filePath);

    // 设置工作目录（用于递归查找 CSS 文件）
    QString workDir = fi.absolutePath();
    htmlMode->setWorkDirectory(workDir);

    MyTextEdit* editor = htmlMode->editor();

    int index = m_tabBar->addTab(displayName);
    m_editorStack->addWidget(htmlMode);

    TabData data;
    data.filePath = filePath;
    data.displayName = displayName;
    data.isModified = false;
    data.isSpecial = true;          // 特殊标签
    data.customWidget = htmlMode;   // HtmlPreviewMode 作为自定义 Widget
    data.editor = editor;           // 保留编辑器引用
    m_tabDataMap[index] = data;

    // 连接编辑器修改状态信号
    if (editor) {
        connect(editor->document(), &QTextDocument::modificationChanged,
                this, [this](bool modified) { onEditorModified(modified); });
    }

    m_tabBar->setCurrentIndex(index);
    htmlMode->setContent(content);
    LOG_DEBUG("[EditorTabBar] 打开HTML预览标签:" << displayName);

    emit tabCountChanged(m_tabBar->count());
}

bool EditorTabBar::closeCurrentTab()
{
    return closeTab(m_tabBar->currentIndex());
}

// ========== 标签页拖出独立窗口（V1.9）==========

bool EditorTabBar::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_tabBar) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragStartPos = me->pos();
                m_dragTabIndex = m_tabBar->tabAt(me->pos());
                m_dragging = false;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (!(me->buttons() & Qt::LeftButton)) return false;
            if (m_dragTabIndex < 0) return false;

            int distance = (me->pos() - m_dragStartPos).manhattanLength();
            if (distance < QApplication::startDragDistance() && !m_dragging) {
                return false;  // 未达到拖拽阈值
            }

            m_dragging = true;

            // 检查是否拖出了标签栏区域
            QRect tabBarRect = m_tabBar->rect();
            if (!tabBarRect.contains(me->pos())) {
                // 拖出标签栏 → 分离为独立窗口
                detachTabToWindow(m_dragTabIndex);
                m_dragging = false;
                m_dragTabIndex = -1;
                return true;  // 事件已处理
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_dragging = false;
            m_dragTabIndex = -1;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void EditorTabBar::detachTabToWindow(int index)
{
    if (index < 0 || index >= m_tabBar->count()) return;

    auto it = m_tabDataMap.find(index);
    if (it == m_tabDataMap.end()) return;

    TabData data = it.value();

    // 从当前标签栏移除（但不删除 widget）
    QWidget* widgetToDetach = data.customWidget ? data.customWidget : data.editor;
    if (!widgetToDetach) return;

    // 从堆栈中移除 widget（不删除）
    m_editorStack->removeWidget(widgetToDetach);
    widgetToDetach->setParent(nullptr);

    // 从标签栏移除
    m_tabBar->removeTab(index);
    m_tabDataMap.remove(index);
    remapTabData();

    // 创建独立窗口
    QMainWindow* detachedWindow = new QMainWindow(nullptr);
    detachedWindow->setWindowTitle(data.displayName);
    detachedWindow->resize(800, 600);

    // 设置窗口图标
    detachedWindow->setWindowIcon(windowIcon());

    // 将 widget 放入独立窗口
    detachedWindow->setCentralWidget(widgetToDetach);
    widgetToDetach->show();

    // 窗口关闭时自动清理
    detachedWindow->setAttribute(Qt::WA_DeleteOnClose);

    // 移动到鼠标当前位置附近
    QPoint globalPos = QCursor::pos();
    detachedWindow->move(globalPos - QPoint(400, 300));

    detachedWindow->show();
    detachedWindow->activateWindow();
    detachedWindow->raise();

    LOG_DEBUG("[EditorTabBar] 标签拖出独立窗口:" << data.displayName.toStdString());

    emit tabCountChanged(m_tabBar->count());

    // 如果没有标签了，发射 allTabsClosed
    if (m_tabBar->count() == 0) {
        emit allTabsClosed();
    }
}

bool EditorTabBar::closeTab(int index)
{
    if (index < 0 || index >= m_tabBar->count()) return true;

    auto it = m_tabDataMap.find(index);
    if (it == m_tabDataMap.end()) return true;

    TabData& data = it.value();

    // 特殊标签页（设置等，无编辑器）直接关闭，不提示保存
    if (data.isSpecial && !data.editor) {
        QString title = data.displayName;
        if (data.customWidget) {
            m_editorStack->removeWidget(data.customWidget);
            data.customWidget->deleteLater();
        }
        m_tabBar->removeTab(index);
        m_tabDataMap.remove(index);
        remapTabData();
        emit customTabDestroyed(title);
        emit tabCountChanged(m_tabBar->count());
        if (m_tabBar->count() == 0) emit allTabsClosed();
        return true;
    }

    // MarkdownMode 等特殊标签但有编辑器的，也需要保存检查
    // 有未保存修改 → 弹出保存提示（合并 MarkdownMode 和普通标签的检查）
    if (data.isModified && data.editor) {
        QString fileName = data.filePath.isEmpty() ? data.displayName : data.filePath;
        int result = ModernDialog::confirm(this, QCoreApplication::applicationName(),
            tr("你想将更改保存到 \"%1\" 吗？").arg(fileName));

        if (result == ModernDialog::ROLE_REJECT) {
            return false;
        }
        if (result == ModernDialog::ROLE_ACCEPT) {
            emit saveRequested(data.editor);
            // 等待外部保存完成后再继续关闭
            // 保存后清除修改标记
            data.isModified = false;
            if (data.editor->document())
                data.editor->document()->setModified(false);
        }
    }

    // 移除组件（从堆栈中删除但不立即销毁，避免崩溃）
    // MarkdownMode 等特殊标签：移除 customWidget（它包含 editor）
    // 普通编辑器标签：直接移除 editor
    QWidget* removeWidget = data.customWidget ? data.customWidget
                                              : static_cast<QWidget*>(data.editor);
    if (removeWidget) {
        m_editorStack->removeWidget(removeWidget);
        removeWidget->deleteLater();
    }

    // P0-4: 通知 LSP 发送 didClose 释放文档（在移除标签数据前，filePath 仍可用）
    if (!data.filePath.isEmpty()) {
        emit fileClosed(data.filePath);
    }

    // 移除标签和数据
    m_tabBar->removeTab(index);
    m_tabDataMap.remove(index);

    // 重新索引
    remapTabData();

    LOG_DEBUG("[EditorTabBar] 关闭标签页，剩余:" << m_tabBar->count());

    emit tabCountChanged(m_tabBar->count());

    if (m_tabBar->count() == 0) {
        emit allTabsClosed();
    }

    return true;
}

void EditorTabBar::switchToTab(int index)
{
    if (index >= 0 && index < m_tabBar->count()) {
        m_tabBar->setCurrentIndex(index);
    }
}

IEditorEdit* EditorTabBar::currentEditor() const
{
    int idx = m_tabBar->currentIndex();
    if (idx < 0) return nullptr;

    auto it = m_tabDataMap.find(idx);
    if (it != m_tabDataMap.end()) {
        return it.value().editor;
    }
    return nullptr;  // fallback: 尝试从stackedWidget获取
}

bool EditorTabBar::isCurrentModified() const
{
    int idx = m_tabBar->currentIndex();
    auto it = m_tabDataMap.find(idx);
    if (it != m_tabDataMap.end()) {
        return it.value().isModified;
    }
    return false;
}

void EditorTabBar::setCurrentModified(bool modified)
{
    int idx = m_tabBar->currentIndex();
    auto it = m_tabDataMap.find(idx);
    if (it != m_tabDataMap.end()) {
        it.value().isModified = modified;
        updateTabText(idx, it.value());
    }
}

QString EditorTabBar::currentFilePath() const
{
    int idx = m_tabBar->currentIndex();
    auto it = m_tabDataMap.find(idx);
    if (it != m_tabDataMap.end()) {
        return it.value().filePath;
    }
    return QString();
}

void EditorTabBar::setCurrentFilePath(const QString& path)
{
    int idx = m_tabBar->currentIndex();
    auto it = m_tabDataMap.find(idx);
    if (it != m_tabDataMap.end()) {
        it.value().filePath = path;
        QFileInfo fi(path);
        it.value().displayName = fi.fileName();
        m_tabBar->setTabText(idx, it.value().displayName);
    }
}

// R3: 实现 ITabWidget::allEditors() — 供 LspCoordinator 遍历编辑器进行状态路由
QList<QPair<QString, IEditorEdit*>> EditorTabBar::allEditors() const
{
    QList<QPair<QString, IEditorEdit*>> result;
    for (auto it = m_tabDataMap.constBegin(); it != m_tabDataMap.constEnd(); ++it) {
        const TabData& data = it.value();
        if (data.editor) {
            result.append({data.filePath, data.editor});
        }
    }
    return result;
}

const TabData* EditorTabBar::currentTabData() const
{
    int idx = m_tabBar->currentIndex();
    auto it = m_tabDataMap.find(idx);
    if (it != m_tabDataMap.end()) {
        return &it.value();
    }
    return nullptr;
}

const TabData* EditorTabBar::tabDataAt(int index) const
{
    auto it = m_tabDataMap.find(index);
    if (it != m_tabDataMap.end()) {
        return &it.value();
    }
    return nullptr;
}

int EditorTabBar::findTabByFilePath(const QString& filePath) const
{
    // V1.9: 遍历所有标签页，按文件路径匹配
    QString absPath = QFileInfo(filePath).absoluteFilePath();
    for (auto it = m_tabDataMap.begin(); it != m_tabDataMap.end(); ++it) {
        if (!it.value().filePath.isEmpty() &&
            QFileInfo(it.value().filePath).absoluteFilePath() == absPath) {
            return it.key();
        }
    }
    return -1;
}

void EditorTabBar::updateTabFilePath(int index, const QString& newPath)
{
    // V1.9: 文件移动/重命名后更新标签页关联路径
    auto it = m_tabDataMap.find(index);
    if (it == m_tabDataMap.end()) return;

    it.value().filePath = newPath;
    QFileInfo fi(newPath);
    it.value().displayName = fi.fileName();
    m_tabBar->setTabText(index, it.value().displayName + (it.value().isModified ? QStringLiteral("*") : QString()));
    m_tabBar->setTabToolTip(index, newPath);
}

// ========== 内部方法 ==========

void EditorTabBar::remapTabData()
{
    // 关闭标签后 QTabBar 会自动重新索引，但 m_tabDataMap 的键仍是旧索引
    // 正确做法：通过 widget 指针匹配来重建映射
    QMap<int, TabData> newDataMap;
    for (int i = 0; i < m_tabBar->count(); ++i) {
        // 遍历旧 map 找到对应的 TabData
        for (auto it = m_tabDataMap.begin(); it != m_tabDataMap.end(); ++it) {
            const TabData& data = it.value();
            QWidget* widget = data.customWidget ? data.customWidget
                                                : static_cast<QWidget*>(data.editor);
            if (widget && m_editorStack->indexOf(widget) == i) {
                newDataMap[i] = data;
                break;
            }
        }
    }
    m_tabDataMap = newDataMap;
}

MyTextEdit* EditorTabBar::createEditor()
{
    auto* editor = new MyTextEdit(m_editorStack);
    auto& config = ConfigManager::instance();

    // 应用配置
    if (config.showLineNumbers())
        editor->setLineNumberVisible(true);
    if (config.fontSize() > 0)
        editor->setFontSize(config.fontSize());

    // 连接修改状态信号
    connect(editor->document(), &QTextDocument::modificationChanged,
            this, [this](bool modified) { onEditorModified(modified); });

    return editor;
}

void EditorTabBar::updateTabText(int index, const TabData& data)
{
    QString text = data.displayName;
    if (data.isModified) {
        text = QStringLiteral("● ") + text;  // VSCode风格白圆点脏标记
    }
    m_tabBar->setTabText(index, text);
}

// ========== 槽函数 ==========

void EditorTabBar::onTabChanged(int index)
{
    auto it = m_tabDataMap.find(index);
    if (it != m_tabDataMap.end()) {
        TabData& data = it.value();

        // R4: 闲置检测已提取到 IdleTabTracker，onTabChanged 只负责 UI 切换

        QWidget* targetWidget = nullptr;

        if (data.isSpecial && data.customWidget) {
            targetWidget = data.customWidget;
            m_editorStack->setCurrentWidget(targetWidget);
            if (data.editor) {
                data.editor->setFocus();
                emit currentEditorChanged(data.editor);
            } else {
                emit currentEditorChanged(nullptr);
            }
        } else if (data.editor) {
            targetWidget = data.editor;
            m_editorStack->setCurrentWidget(targetWidget);
            data.editor->setFocus();
            emit currentEditorChanged(data.editor);
        }

        // Tab切换淡入动画
        if (targetWidget) {
            auto* effect = qobject_cast<QGraphicsOpacityEffect*>(targetWidget->graphicsEffect());
            if (!effect) {
                effect = new QGraphicsOpacityEffect(targetWidget);
                targetWidget->setGraphicsEffect(effect);
            }
            effect->setOpacity(0.0);
            auto* anim = new QPropertyAnimation(effect, "opacity");
            anim->setDuration(150);
            anim->setStartValue(0.0);
            anim->setEndValue(1.0);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            connect(anim, &QPropertyAnimation::finished, anim, &QPropertyAnimation::deleteLater);
            anim->start();
        }
    } else if (m_tabBar->count() == 0) {
        emit currentEditorChanged(nullptr);
    }
}

void EditorTabBar::onTabCloseRequested(int index)
{
    closeTab(index);
}

void EditorTabBar::onEditorModified(bool modified)
{
    // 找到发出信号的编辑器对应的标签页
    auto* senderDoc = qobject_cast<QTextDocument*>(sender());
    for (auto it = m_tabDataMap.begin(); it != m_tabDataMap.end(); ++it) {
        if (it.value().editor && it.value().editor->document() == senderDoc) {
            it.value().isModified = modified;
            updateTabText(it.key(), it.value());
            break;
        }
    }
}

void EditorTabBar::refreshAllEditors()
{
    for (auto it = m_tabDataMap.begin(); it != m_tabDataMap.end(); ++it) {
        if (it.value().editor) {
            it.value().editor->updateLineNumberArea();
        }
    }
}

// R4: onIdleCheck 已提取到 IdleTabTracker（单一职责原则）
