#ifndef __INC_QT_UTILS_H
#define __INC_QT_UTILS_H

#include <QToolBar>
#include <QValidator>

#include "common/format_cv.h"
#include "common/format_qt.h"
#include "onnxruntime_cxx_api.h"


extern void toFile(const std::string &name, const Ort::Value &tensor);
extern void fromFile(const std::string &path, const cv::Mat &blob);
extern void fromFile(const std::string &path, std::vector<float> &blob);

inline constexpr int32_t None = std::numeric_limits<int32_t>::min();

template <typename T>
int32_t mult_size(const std::vector<T> &v) {
    return std::accumulate(v.begin(), v.end(), T(1), std::multiplies<T>());
}

template <typename T>
std::ostream &operator<<(std::ostream &os, const std::vector<T> &v) {
    os << "[";
    for (int i = 0; i < v.size(); ++i) {
        os << v[i];
        if (i != v.size() - 1) { os << ", "; }
    }
    os << "]";
    return os;
}

inline bool operator<(const QPointF &a, const QPointF &b) {
    if (a.x() < b.x()) {
        return true;
    }
    return a.y() < b.y();
}

std::vector<QColor> label_colormap();

using QKey = std::set<QString>;

class utils {
  public:
    static QIcon newIcon(const QString &icon);

    static QValidator *labelValidator();

    static QString fmtShortcut(const QList<QString> &text);
    static QString format_shortcut(const QString &text);

    static double  direction_angle(const QPointF &start, const QPointF &end);
    static QPointF project_point_on_line(QPointF point, QPointF line_start, QPointF line_end);
    static QPointF project_point_on_perpendicular_line(QPointF point, QPointF line_start, QPointF line_end);

    static QAction *newAction(const QString &text, const QList<QString> &shortcut={}, const QString &file="", const QString &tip="", bool enabled=true, bool checkable=false, bool checked=false);

    static void add_actions(QMenu *menu, const std::list<QObject *> &actions);
    static void addActions(QToolBar *tool, const std::list<QAction *> &actions);

    static qreal distance(const QPointF &p);
    static qreal distance(const QPointF &p1, const QPointF &p2);
    static qreal distanceToLine(const QPointF &point, const QLineF &line);
    static QList<qreal> distance(const QList<QPointF> &points);

    static QString HashPixmap(const QPixmap &pixmap);

    static cv::Mat ImageToMat(const QImage &image);
    static QImage MatToImage(const cv::Mat &mat);

    static cv::Mat PixmapToMat(const QPixmap &pixmap);
    static QPixmap MatToPixmap(const cv::Mat &mat);

    static cv::Rect masks_to_bboxes(const cv::Mat &mask);
    static std::vector<cv::Rect> masks_to_bboxes1(const std::vector<cv::Mat> &masks);

    static cv::Mat img_data_to_arr(const QByteArray &img_data);
    static QByteArray img_arr_to_data(const cv::Mat &img_data);

    static cv::Mat img_b64_to_arr(const std::string &b64_string);
};
#endif //__INC_QT_UTILS_H