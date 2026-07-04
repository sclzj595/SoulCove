#include "ui/editor/FindReplaceBar.h"
#include "ui/editor/MyTextEdit.h"
#include "core/config/ThemeManager.h"

#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QTextDocument>
#include <QRegularExpression>
#include <QTextCursor>
#include <QShortcut>

FindReplaceBar::FindReplaceBar(QWidget* parent)
    : QWidget(parent)
{
    // 查找行
    m_findEdit = new QLineEdit(this);
    m_findEdit->setPlaceholderText(tr("查找"));
    m_findEdit->setClearButtonEnabled(true);

    m_btnFindPrev = new QPushButton(tr("↑"), this);
    m_btnFindPrev->setToolTip(tr("查找上一个 (Shift+F3)"));
    m_btnFindPrev->setFixedSize(32, 28);

    m_btnFindNext = new QPushButton(tr("↓"), this);
    m_btnFindNext->setToolTip(tr("查找下一个 (F3)"));
    m_btnFindNext->setFixedSize(32, 28);

    m_chkCaseSensitive = new QCheckBox(tr("Aa"), this);
    m_chkCaseSensitive->setToolTip(tr("区分大小写"));

    m_chkWholeWord = new QCheckBox(tr("W"), this);
    m_chkWholeWord->setToolTip(tr("全字匹配"));

    m_chkRegex = new QCheckBox(tr(".*"), this);
    m_chkRegex->setToolTip(tr("正则表达式"));

    m_matchCountLabel = new QLabel(this);
    m_matchCountLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;")
        .arg(ThemeManager::instance().currentPalette().fgSecondary.name()));

    m_btnClose = new QPushButton(tr("×"), this);
    m_btnClose->setToolTip(tr("关闭 (Esc)"));
    m_btnClose->setFixedSize(28, 28);

    QHBoxLayout* findLayout = new QHBoxLayout();
    findLayout->setContentsMargins(4, 2, 4, 0);
    findLayout->setSpacing(4);
    findLayout->addWidget(m_findEdit);
    findLayout->addWidget(m_btnFindPrev);
    findLayout->addWidget(m_btnFindNext);
    findLayout->addWidget(m_chkCaseSensitive);
    findLayout->addWidget(m_chkWholeWord);
    findLayout->addWidget(m_chkRegex);
    findLayout->addWidget(m_matchCountLabel);
    findLayout->addStretch();
    findLayout->addWidget(m_btnClose);

    // 替换行
    m_replaceEdit = new QLineEdit(this);
    m_replaceEdit->setPlaceholderText(tr("替换为"));
    m_replaceEdit->setClearButtonEnabled(true);

    m_btnReplace = new QPushButton(tr("替换"), this);
    m_btnReplace->setToolTip(tr("替换当前匹配"));
    m_btnReplaceAll = new QPushButton(tr("全部替换"), this);
    m_btnReplaceAll->setToolTip(tr("替换所有匹配"));

    QHBoxLayout* replaceLayout = new QHBoxLayout();
    replaceLayout->setContentsMargins(4, 0, 4, 2);
    replaceLayout->setSpacing(4);
    replaceLayout->addWidget(m_replaceEdit);
    replaceLayout->addWidget(m_btnReplace);
    replaceLayout->addWidget(m_btnReplaceAll);
    replaceLayout->addStretch();

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addLayout(findLayout);
    mainLayout->addLayout(replaceLayout);

    // 默认隐藏替换行
    m_replaceEdit->hide();
    m_btnReplace->hide();
    m_btnReplaceAll->hide();
    m_replaceModeVisible = false;

    // 连接信号
    connect(m_findEdit, &QLineEdit::textChanged, this, &FindReplaceBar::onFindTextChanged);
    connect(m_findEdit, &QLineEdit::returnPressed, this, &FindReplaceBar::findNext);
    connect(m_btnFindNext, &QPushButton::clicked, this, &FindReplaceBar::findNext);
    connect(m_btnFindPrev, &QPushButton::clicked, this, &FindReplaceBar::findPrev);
    connect(m_btnReplace, &QPushButton::clicked, this, &FindReplaceBar::replaceOne);
    connect(m_btnReplaceAll, &QPushButton::clicked, this, &FindReplaceBar::replaceAll);
    connect(m_btnClose, &QPushButton::clicked, this, &FindReplaceBar::hideBar);

    // 选项变更时重新查找
    connect(m_chkCaseSensitive, &QCheckBox::toggled, this, [this]() { onFindTextChanged(); });
    connect(m_chkWholeWord, &QCheckBox::toggled, this, [this]() { onFindTextChanged(); });
    connect(m_chkRegex, &QCheckBox::toggled, this, [this]() { onFindTextChanged(); });

    // F3 / Shift+F3 快捷键
    QShortcut* f3Shortcut = new QShortcut(QKeySequence(Qt::Key_F3), this);
    connect(f3Shortcut, &QShortcut::activated, this, &FindReplaceBar::findNext);
    QShortcut* shiftF3Shortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
    connect(shiftF3Shortcut, &QShortcut::activated, this, &FindReplaceBar::findPrev);

    applyThemeStyle();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        applyThemeStyle();
    });
}

void FindReplaceBar::applyThemeStyle()
{
    const auto& p = ThemeManager::instance().currentPalette();
    setStyleSheet(QStringLiteral(
        "FindReplaceBar { background: %1; border-bottom: 1px solid %2; }"
        "QLineEdit { background: %3; border: 1px solid %2; border-radius: 3px; "
        "padding: 3px 6px; color: %4; selection-background-color: %5; }"
        "QLineEdit:focus { border: 1px solid %5; }"
        "QPushButton { background: %3; border: 1px solid %2; border-radius: 3px; "
        "padding: 3px 8px; color: %4; }"
        "QPushButton:hover { background: %6; border-color: %5; }"
        "QPushButton:pressed { background: %5; color: %7; }"
        "QCheckBox { color: %4; padding: 2px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; }"
        "QLabel { color: %8; }"
    ).arg(p.bgEditor.name(), p.borderDefault.name(), p.bgInput.name(),
          p.fgPrimary.name(), p.accentPrimary.name(), p.bgHover.name(),
          p.fgOnAccent.name(), p.fgSecondary.name()));
}

void FindReplaceBar::setEditor(MyTextEdit* editor)
{
    m_editor = editor;
}

void FindReplaceBar::showFind()
{
    show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    if (!m_findEdit->text().isEmpty()) {
        onFindTextChanged();
    }
}

void FindReplaceBar::showReplace()
{
    m_replaceModeVisible = true;
    m_replaceEdit->show();
    m_btnReplace->show();
    m_btnReplaceAll->show();
    showFind();
    m_replaceEdit->setFocus();
}

void FindReplaceBar::hideBar()
{
    hide();
    // 清除高亮
    if (m_editor) {
        QList<QTextEdit::ExtraSelection> empty;
        m_editor->setExtraSelections(empty);
    }
    emit barHidden();
}

void FindReplaceBar::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // 如果编辑器有选中文本，自动填入查找框
    if (m_editor) {
        QString selected = m_editor->textCursor().selectedText();
        if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator)) {
            m_findEdit->setText(selected);
        }
    }
}

void FindReplaceBar::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hideBar();
        return;
    }
    QWidget::keyPressEvent(event);
}

QTextDocument::FindFlags FindReplaceBar::findFlags() const
{
    QTextDocument::FindFlags flags;
    if (m_chkCaseSensitive->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    if (m_chkWholeWord->isChecked())
        flags |= QTextDocument::FindWholeWords;
    return flags;
}

void FindReplaceBar::onFindTextChanged()
{
    if (m_findEdit->text().isEmpty()) {
        m_matchCountLabel->clear();
        if (m_editor) {
            m_editor->setExtraSelections({});
        }
        return;
    }
    highlightAllMatches();
    updateMatchCount();
    // 立即查找第一个匹配
    findNext();
}

void FindReplaceBar::onToggleReplaceMode(bool checked)
{
    m_replaceModeVisible = checked;
    m_replaceEdit->setVisible(checked);
    m_btnReplace->setVisible(checked);
    m_btnReplaceAll->setVisible(checked);
}

bool FindReplaceBar::doFind(bool forward)
{
    if (!m_editor || m_findEdit->text().isEmpty()) return false;

    QString searchText = m_findEdit->text();
    QTextDocument::FindFlags flags = findFlags();
    if (!forward) flags |= QTextDocument::FindBackward;

    bool found = false;
    if (m_chkRegex->isChecked()) {
        // 正则查找
        QRegularExpression regex(searchText,
            m_chkCaseSensitive->isChecked() ? QRegularExpression::NoPatternOption
                                            : QRegularExpression::CaseInsensitiveOption);
        if (regex.isValid()) {
            QTextCursor cur = m_editor->textCursor();
            QTextDocument* doc = m_editor->document();
            QTextCursor result = doc->find(regex, cur, flags);
            if (result.isNull()) {
                // 回绕查找
                cur.movePosition(forward ? QTextCursor::Start : QTextCursor::End);
                result = doc->find(regex, cur, flags);
            }
            if (!result.isNull()) {
                m_editor->setTextCursor(result);
                found = true;
            }
        }
    } else {
        found = m_editor->find(searchText, flags);
        if (!found) {
            // 回绕查找
            QTextCursor cur = m_editor->textCursor();
            cur.movePosition(forward ? QTextCursor::Start : QTextCursor::End);
            m_editor->setTextCursor(cur);
            found = m_editor->find(searchText, flags);
        }
    }
    return found;
}

void FindReplaceBar::findNext()
{
    if (!doFind(true)) {
        m_matchCountLabel->setText(tr("无匹配"));
    }
}

void FindReplaceBar::findPrev()
{
    if (!doFind(false)) {
        m_matchCountLabel->setText(tr("无匹配"));
    }
}

void FindReplaceBar::replaceOne()
{
    if (!m_editor || m_findEdit->text().isEmpty()) return;

    // 如果当前有选中文本且匹配查找内容，直接替换
    QTextCursor cur = m_editor->textCursor();
    if (cur.hasSelection()) {
        QString selected = cur.selectedText();
        bool match = false;
        if (m_chkRegex->isChecked()) {
            QRegularExpression regex(m_findEdit->text());
            match = regex.match(selected).hasMatch();
        } else {
            Qt::CaseSensitivity cs = m_chkCaseSensitive->isChecked() ?
                Qt::CaseSensitive : Qt::CaseInsensitive;
            match = selected.compare(m_findEdit->text(), cs) == 0;
        }
        if (match) {
            cur.insertText(m_replaceEdit->text());
        }
    }
    // 查找下一个
    findNext();
}

void FindReplaceBar::replaceAll()
{
    if (!m_editor || m_findEdit->text().isEmpty()) return;

    QTextCursor cursor(m_editor->document());
    cursor.beginEditBlock();

    int count = 0;
    QTextDocument::FindFlags flags = findFlags();

    if (m_chkRegex->isChecked()) {
        QRegularExpression regex(m_findEdit->text(),
            m_chkCaseSensitive->isChecked() ? QRegularExpression::NoPatternOption
                                            : QRegularExpression::CaseInsensitiveOption);
        if (!regex.isValid()) {
            cursor.endEditBlock();
            return;
        }
        QTextDocument* doc = m_editor->document();
        QTextCursor result = doc->find(regex, 0);
        while (!result.isNull()) {
            result.insertText(m_replaceEdit->text());
            count++;
            result = doc->find(regex, result);
        }
    } else {
        // 简单文本替换
        cursor.movePosition(QTextCursor::Start);
        while (m_editor->find(m_findEdit->text(), flags)) {
            QTextCursor c = m_editor->textCursor();
            c.insertText(m_replaceEdit->text());
            count++;
        }
    }

    cursor.endEditBlock();
    m_matchCountLabel->setText(tr("已替换 %1 处").arg(count));
}

void FindReplaceBar::updateMatchCount()
{
    if (!m_editor || m_findEdit->text().isEmpty()) {
        m_matchCountLabel->clear();
        return;
    }

    int count = 0;
    QTextDocument* doc = m_editor->document();

    if (m_chkRegex->isChecked()) {
        QRegularExpression regex(m_findEdit->text(),
            m_chkCaseSensitive->isChecked() ? QRegularExpression::NoPatternOption
                                            : QRegularExpression::CaseInsensitiveOption);
        if (regex.isValid()) {
            QTextCursor cur(doc);
            while (!cur.isNull()) {
                cur = doc->find(regex, cur);
                if (!cur.isNull()) count++;
            }
        }
    } else {
        QString text = m_findEdit->text();
        Qt::CaseSensitivity cs = m_chkCaseSensitive->isChecked() ?
            Qt::CaseSensitive : Qt::CaseInsensitive;
        QTextCursor cur(doc);
        cur.movePosition(QTextCursor::Start);
        while (!cur.isNull()) {
            cur = doc->find(text, cur,
                m_chkWholeWord->isChecked() ? QTextDocument::FindWholeWords : QTextDocument::FindFlags());
            if (!cur.isNull()) {
                if (m_chkCaseSensitive->isChecked()) {
                    if (cur.selectedText().compare(text, cs) == 0) count++;
                } else {
                    count++;
                }
            }
        }
    }

    m_matchCountLabel->setText(tr("%1 个匹配").arg(count));
}

void FindReplaceBar::highlightAllMatches()
{
    if (!m_editor) return;

    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection sel;
    sel.format.setBackground(ThemeManager::instance().currentPalette().selectionBg);
    sel.format.setProperty(QTextFormat::FullWidthSelection, false);

    QTextDocument* doc = m_editor->document();
    QTextCursor cur(doc);
    cur.movePosition(QTextCursor::Start);

    if (m_chkRegex->isChecked()) {
        QRegularExpression regex(m_findEdit->text(),
            m_chkCaseSensitive->isChecked() ? QRegularExpression::NoPatternOption
                                            : QRegularExpression::CaseInsensitiveOption);
        if (regex.isValid()) {
            while (!cur.isNull()) {
                cur = doc->find(regex, cur);
                if (!cur.isNull()) {
                    sel.cursor = cur;
                    selections.append(sel);
                }
            }
        }
    } else {
        QString text = m_findEdit->text();
        QTextDocument::FindFlags flags = findFlags();
        while (!cur.isNull()) {
            cur = doc->find(text, cur, flags);
            if (!cur.isNull()) {
                sel.cursor = cur;
                selections.append(sel);
            }
        }
    }

    m_editor->setExtraSelections(selections);
}
