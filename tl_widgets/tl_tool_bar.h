#ifndef __INC_TOOL_BAR_H
#define __INC_TOOL_BAR_H

#include <QToolBar>


class TlToolBar : public QToolBar {
    Q_OBJECT
public:
    explicit TlToolBar(const QString &title, const std::list<QAction *> &actions, Qt::Orientation orientation=Qt::Horizontal, Qt::ToolButtonStyle button_style=Qt::ToolButtonTextUnderIcon, const QFont &font_base=QFont(), QWidget *parent=nullptr);

    void addAction(QAction *action);
};
#endif //__INC_TOOL_BAR_H