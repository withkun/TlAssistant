#ifndef __INC_STATUS_STATS_H
#define __INC_STATUS_STATS_H

#include <QLabel>


class StatusStats : public QLabel {
    Q_OBJECT
public:
    explicit StatusStats(QWidget *parent = nullptr);
};
#endif //__INC_STATUS_STATS_H