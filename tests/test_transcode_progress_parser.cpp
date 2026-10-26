#include "transcodeprogressparser.h"

#include <QStringDecoder>
#include <QtTest>

class TranscodeProgressParserTest : public QObject
{
    Q_OBJECT

private slots:
    void handlesSplitUtf8AndSplitLines();
    void ignoresUnrelatedMalformedAndRegressingLines();
    void resetsBetweenTasks();
    void keepsHumanLogAndHidesMachineEvents();
};

void TranscodeProgressParserTest::handlesSplitUtf8AndSplitLines()
{
    TranscodeProgressParser parser;
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QByteArray bytes = QStringLiteral(
        "[12:34:56.789] [INFO ] Demux 进度: 90%\n"
        "QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\",\"percent\":20}\r\n")
                                 .toUtf8();
    const int split = bytes.indexOf(QStringLiteral("进").toUtf8()) + 1;
    QVERIFY(split > 1);
    QVERIFY(parser.append(decoder(bytes.left(split))).isEmpty());
    QCOMPARE(parser.append(decoder(bytes.mid(split))), QVector<int>{20});
    QCOMPARE(parser.append(QStringLiteral("QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\",\"percent\":30}\n")),
             QVector<int>{30});
}

void TranscodeProgressParserTest::ignoresUnrelatedMalformedAndRegressingLines()
{
    TranscodeProgressParser parser;
    const QString lines = QStringLiteral(
        "[12:34:56.789] [INFO ] Demux 进度: 90%\n"
        "random 进度: 100%\n"
        "QFS_PROGRESS broken-json\n"
        "QFS_PROGRESS {\"version\":2,\"stage\":\"transcode\",\"percent\":80}\n"
        "QFS_PROGRESS {\"version\":1.5,\"stage\":\"transcode\",\"percent\":80}\n"
        "QFS_PROGRESS {\"version\":1,\"stage\":\"mux\",\"percent\":80}\n"
        "QFS_PROGRESS {\"version\":1,\"stage\":\"demux\",\"percent\":90}\n"
        "QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\",\"percent\":10}\n"
        "QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\",\"percent\":10}\n"
        "QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\",\"percent\":10.5}\n"
        "QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\",\"percent\":101}\n"
        "QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\",\"percent\":5}\n"
        "QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\",\"percent\":99}\n"
        "QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\",\"percent\":100}\n");
    QCOMPARE(parser.append(lines), (QVector<int>{10, 99}));
    QVERIFY(parser.append(QString(5000, QLatin1Char('x'))).isEmpty());
    QVERIFY(parser.append(QStringLiteral("\n")).isEmpty());
    parser.reset();
    QCOMPARE(parser.append(QStringLiteral("QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\",\"percent\":30}\n")),
             QVector<int>{30});
}

void TranscodeProgressParserTest::resetsBetweenTasks()
{
    TranscodeProgressParser parser;
    QCOMPARE(parser.append(QStringLiteral("QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\",\"percent\":99}\n")),
             QVector<int>{99});
    parser.reset();
    QCOMPARE(parser.append(QStringLiteral("QFS_PROGRESS {\"version\":1,\"stage\":\"transcode\",\"percent\":10}\n")),
             QVector<int>{10});
}

void TranscodeProgressParserTest::keepsHumanLogAndHidesMachineEvents()
{
    TranscodeProgressParser parser;
    QString visible;
    const QString first = QStringLiteral("[12:34:56.789] [INFO ] Demux 进度: 20%\nQFS_PROG");
    QVERIFY(parser.append(first, &visible).isEmpty());
    QCOMPARE(visible, QStringLiteral("[12:34:56.789] [INFO ] Demux 进度: 20%\n"));
    visible.clear();
    QCOMPARE(parser.append(QStringLiteral("RESS {\"version\":1,\"stage\":\"transcode\",\"percent\":20}\n"),
                           &visible), QVector<int>{20});
    QVERIFY(visible.isEmpty());
}

QTEST_APPLESS_MAIN(TranscodeProgressParserTest)
#include "test_transcode_progress_parser.moc"
