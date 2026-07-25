#include "shape_clipboard.h"


ShapeClipboard::ShapeClipboard(QObject *parent) {
//    super().__init__(parent)
//    self._buffer: tuple[Shape, ...] = ()
}

void ShapeClipboard::store(QList<TlShape> &shapes) {
//    snapshot = tuple(shape.copy() for shape in shapes)
//    had_content = bool(self._buffer)
//    self._buffer = snapshot
//    if had_content != bool(snapshot):
//        self.availability_changed.emit(bool(snapshot))
}

QList<TlShape> ShapeClipboard::paste() {
//    return [shape.copy() for shape in self._buffer]
    return {};
}