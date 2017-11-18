#include "surface.h"
#include <QApplication>

Surface::Surface(QWidget *parent) : QWidget(parent)
{

}

/**
 * 一个鼠标的点击事件监听
 * @brief Surface::mousePressEvent
 * @param event
 */
void Surface::mousePressEvent(QMouseEvent * event)
{
    // 监听的内容是鼠标右键点击的事件
    if(event->button() == Qt::RightButton)
    {
        qDebug() << "!!!!";
        QString filename = QFileDialog::getOpenFileName();
        if(!filename.isEmpty())
        {
            qDebug() << "surface filename : " << filename;
            // 创建一个Reader，用于处理数据
            reader = new Reader(filename) ;
            // 链接一个信号，用于定时更新界面
            connect(reader , SIGNAL(onUpdateImage(QImage)) , this , SLOT(getNewImage(QImage)));
        }
    }
}

void Surface::getNewImage(QImage image)
{
    this->image = image;
    update();
}

void Surface::paintEvent(QPaintEvent *event)
{

    if(image.isNull())
    {
        return;
    }
    QPainter painter(this);
    painter.drawImage(0 , 0 , image.scaled(size() ,
                                           Qt::IgnoreAspectRatio , Qt::SmoothTransformation));
}

int main(int argc , char * argv[])
{
    QApplication app(argc , argv);

    av_register_all();
    avcodec_register_all();
    avformat_network_init();

    Surface surface;
    surface.show();

    return app.exec();
}
