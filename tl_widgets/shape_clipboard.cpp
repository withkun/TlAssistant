#include "shape_clipboard.h"


ShapeClipboard::ShapeClipboard(QObject *parent) : QObject(parent) {
}

void ShapeClipboard::store(const QList<TlShape> &shapes) {
    const QList<TlShape> snapshot = shapes | std::views::transform([](auto &s) { return s.copy(); }) | std::ranges::to<QList<TlShape>>();
    const auto had_content = !this->buffer_.empty();
    this->buffer_ = snapshot;
    if (had_content != !snapshot.empty())
        emit this->availability_changed(!snapshot.empty());
}

QList<TlShape> ShapeClipboard::paste() const {
    return this->buffer_ | std::views::transform([](auto &s) { return s.clone(); }) | std::ranges::to<QList<TlShape>>();
}