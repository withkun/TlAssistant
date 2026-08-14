#ifndef __INC_LABEL_FILE_H
#define __INC_LABEL_FILE_H

#include <QObject>
#include "tl_shape.h"


class ShapeDict {
public:
    QString                     label_;
    QList<QPointF>              points_;
    QString                     shape_type_;
    QMap<QString, bool>         flags_;
    QString                     description_;
    int32_t                     group_id_;
    cv::Mat                     mask_;
    QMap<QString, QByteArray>   other_data_;
};

struct AnnotationEx {
    QString                     image_path_;
    QByteArray                  image_data_;
    QList<ShapeDict>            shapes_;
    QMap<QString, bool>         flags_;
    QMap<QString, QByteArray>   other_data_;

    bool isNull() const {
        return image_path_.isEmpty() && shapes_.isEmpty();
    }
};

class OSError : public std::exception {
public:
    OSError() = default;
    explicit OSError(const std::string &string) : std::exception(string.data()) {};
};

class LabelFileError : public std::exception {
public:
    // Base for read/write failures of labelme JSON annotation files.
    LabelFileError() = default;
    explicit LabelFileError(const std::string &string) : std::exception(string.data()) {};
};

class LabelFileReadError : public LabelFileError {
public:
    // Wraps an underlying parse or image-decode failure during load.
    LabelFileReadError() = default;
    explicit LabelFileReadError(const std::string &string) : LabelFileError(string) {};
};

class LabelFileWriteError : public LabelFileError {
public:
    // Wraps an underlying I/O failure during save.
    LabelFileWriteError() = default;
    explicit LabelFileWriteError(const std::string &string) : LabelFileError(string) {};
};

extern const QString LABEL_FILE_SUFFIX;

bool is_label_file_path(const QString &filename);

QByteArray read_image_file(const QString &filename);
AnnotationEx read_label_file(const QString &filename);

void write_label_file(const QString &filename, const AnnotationEx &annotation, int32_t image_height, int32_t image_width, bool save_image_data);

#endif //__INC_LABEL_FILE_H