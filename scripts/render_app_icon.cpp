// Regenerate checked-in PNG/ICO assets from resources/icons/studio.svg.
// Compile this helper with Qt Gui + Svg; the application itself needs neither Svg nor this tool.
#include <QGuiApplication>
#include <QSvgRenderer>
#include <QImage>
#include <QPainter>
#include <QFile>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QVector>
int main(int argc,char** argv) {
    QGuiApplication app(argc,argv);
    if(argc!=2) return 1;
    QDir folder(QString::fromLocal8Bit(argv[1]));
    QSvgRenderer renderer(folder.filePath("studio.svg")); if(!renderer.isValid()) return 2;
    const QVector<int> sizes{16,20,24,32,40,48,64,128,256};
    QVector<QByteArray> frames;
    for(int size:sizes) {
        QImage large(size*4,size*4,QImage::Format_ARGB32_Premultiplied); large.fill(Qt::transparent);
        {QPainter painter(&large); renderer.render(&painter);}
        QImage frame=large.scaled(size,size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        if(!frame.save(folder.filePath(QString("studio-%1.png").arg(size)))) return 3;
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); if(!frame.save(&buffer,"PNG")) return 4; frames.append(png);
    }
    QImage preview(512,512,QImage::Format_ARGB32_Premultiplied); preview.fill(Qt::transparent);
    {QPainter painter(&preview); renderer.render(&painter);}
    if(!preview.save(folder.filePath("studio.png"))) return 5;
    QFile ico(folder.filePath("studio.ico")); if(!ico.open(QIODevice::WriteOnly)) return 6;
    QDataStream stream(&ico); stream.setByteOrder(QDataStream::LittleEndian);
    stream<<quint16(0)<<quint16(1)<<quint16(sizes.size());
    quint32 offset=6+16*sizes.size();
    for(int i=0;i<sizes.size();++i) {
        const quint8 dimension=sizes[i]==256?0:sizes[i];
        stream<<dimension<<dimension<<quint8(0)<<quint8(0)<<quint16(1)<<quint16(32)<<quint32(frames[i].size())<<offset;
        offset+=frames[i].size();
    }
    for(const auto& png:frames) stream.writeRawData(png.constData(),png.size());
    return stream.status()==QDataStream::Ok?0:7;
}
