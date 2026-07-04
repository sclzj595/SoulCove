#ifndef COMMANDPALETTE_H
#define COMMANDPALETTE_H

#include <QFrame>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

struct CommandItem {
    QString id;           // 唯一ID，如 "file.open"
    QString title;        // 显示名称，如 "打开文件"
    QString shortcut;     // 快捷键显示，如 "Ctrl+O"
    QString category;     // 分类，如 "文件" / "编辑" / "视图" / "终端"
};

class CommandPalette : public QFrame
{
    Q_OBJECT
public:
    explicit CommandPalette(QWidget* parent = nullptr);

    /// 注册命令（由外部调用）
    void registerCommand(const CommandItem& cmd);

    /// 显示/隐藏面板
    void showPalette();
    void hidePalette();

    /// 应用/刷新主题样式（切换主题时自动调用）
    void applyTheme();

signals:
    void commandTriggered(const QString& commandId);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onItemActivated(QListWidgetItem* item);

private:
    QLineEdit* m_searchEdit;
    QListWidget* m_resultList;
    QList<CommandItem> m_allCommands;
};

#endif // COMMANDPALETTE_H
