#include "tl_tool_bar.h"

#include "utilities.h"

#include <QLayout>
#include <QToolButton>
#include <QWidgetAction>


TlToolBar::TlToolBar(const QString &title, const std::list<QAction *> &actions, const Qt::Orientation orientation,
                     const Qt::ToolButtonStyle button_style, const QFont &font_base, QWidget *parent) : QToolBar(title, parent) {
    if (font_base != QFont()) {
        QFont font = font_base;
        font.setPointSizeF(font_base.pointSizeF() * 0.875);
        setFont(font);
    }

    const auto layout = this->layout();
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    this->setContentsMargins(0, 0, 0, 0);
    this->setWindowFlags(this->windowFlags() | Qt::FramelessWindowHint);
    this->setMovable(false);
    this->setFloatable(false);

    this->setObjectName(title + "ToolBar");
    this->setOrientation(orientation);
    this->setToolButtonStyle(button_style);
    utils::addActions(this, actions);
}

void TlToolBar::addAction(QAction *action) {
    if (qobject_cast<QWidgetAction *>(action) != nullptr) {
        QToolBar::addAction(action);
        return;
    }
    auto *btn = new QToolButton();
    btn->setDefaultAction(action);
    btn->setToolButtonStyle(this->toolButtonStyle());
    this->addWidget(btn);
}