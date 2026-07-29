#ifndef __INC_FILE_DIALOG_H
#define __INC_FILE_DIALOG_H

#include <QFileDialog>
#include <QScrollArea>


class ScrollAreaPreview : public QScrollArea {
};

class FileDialogPreview : public QFileDialog {
public:
    FileDialogPreview(QWidget *parent);

};
#endif //__INC_FILE_DIALOG_H