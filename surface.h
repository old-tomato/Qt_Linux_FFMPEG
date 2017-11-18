#ifndef SURFACE_H
#define SURFACE_H

#include <QWidget>
#include <QMouseEvent>
#include <QFileDialog>
#include <QPaintEvent>
#include <QImage>
#include <QPainter>

#include "reader.h"

class Surface : public QWidget
{
    Q_OBJECT
public:
    explicit Surface(QWidget *parent = nullptr);
    void mousePressEvent(QMouseEvent * event);
    void paintEvent(QPaintEvent *event);
signals:

public slots:
    void getNewImage(QImage image);
private:
    QImage image;
    Reader * reader;
};

#endif // SURFACE_H
