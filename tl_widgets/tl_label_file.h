#ifndef __INC_LABEL_FILE_H
#define __INC_LABEL_FILE_H

#include <QObject>
#include "tl_shape.h"


class ShapeDict {
public:
    QString                 label;
    QList<QPointF>          points;
    QString                 shape_type;
    QMap<QString, bool>     flags;
    QString                 description;
    int32_t                 group_id;
    cv::Mat                 mask;
    QMap<QString, QString>  other_data;
};

struct AnnotationEx {
    QString                     image_path_;
    QByteArray                  image_data_;
    QList<ShapeDict>            shapes_;
    QMap<QString, bool>         flags_;
    QMap<QString, QByteArray>   other_data_;
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

class LabelFile : public QObject {
public:
    explicit LabelFile(const QString &filename = "");

    static QString suffix;

    static QByteArray load_image_file(const QString &filename);

    void load(const QString &filename);

    void save(const QString &filename,
              const QList<TlShape> &shapes,
              const QString &imagePath,
              const QByteArray &imageData,
              int32_t imageHeight,
              int32_t imageWidth,
              const QString &otherData="",
              const QMap<QString, bool> &flags={});

private:
    std::pair<int32_t, int32_t> check_image_height_and_width(const QByteArray &imageData, int32_t imageHeight, int32_t imageWidth);

public:
    QString         flags_;
    QList<TlShape>  shapes_;
    QList<ShapeDict>  shapes1_;

    QString         filename_;
    QString         imagePath_;
    QByteArray      imageData_;
    QByteArray      otherData_;
};
#endif //__INC_LABEL_FILE_H