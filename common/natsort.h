#ifndef __INC_NATSORT_H
#define __INC_NATSORT_H

#include <vector>
#include <QStringList>


class natsort {
public:
    template <typename T>
    static std::vector<T> os_sorted(std::vector<T> &seq, bool reverse = false);
    static QList<QString> os_sorted(QStringList &images, bool reverse = false);

    static bool compareNat(const std::string &a, const std::string &b);
    static bool compareFilename(const std::string &a, const std::string &b);
    static QList<QString> natsorted(const QList<QString> &images);
    static std::vector<std::string> natsorted(const std::vector<std::string> &images);
};
#endif //__INC_NATSORT_H