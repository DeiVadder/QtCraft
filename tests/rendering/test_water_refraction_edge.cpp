// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The refraction of the realistic water keeps edges under it in place (work package 79.82,
// decision E-906): the game renders a pool offscreen at quality High, seen steeply from above its
// shore, where sand one block under the surface meets deep water. The edge between the two runs
// straight across the picture; the waves may bend it a little, but never by more than a share of
// the picture's height (before 79.82 it swung by about 3 %, after it by 0.8 %).

#include <QImage>
#include <QPointF>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// The most the edge may move up and down across the middle of the picture, as a share of its
// height.
constexpr double maxSwing = 0.01;

// The settings of the run: the realistic mode at quality High (screen-space reflections too).
constexpr auto settings = "[debug]\nlevel=off\n[audio]\nmuted=true\n[render]\nmode=realistic\nquality=high\n";

// A pool in front of the player (yaw 180 looks along +z): water three blocks deep, under the
// first two rows a shore of sand up to the grass, then two rows of sand one block under the
// surface; the player flies above the shallow rows and looks down at the edge to the deep water.
// Flying is on before the teleport (the steps run in the order of their times however slow the
// start), so the eye stays at the height it was teleported to.
QStringList arguments()
{
    return {u"--world-type"_s, u"flat"_s, u"--seed"_s, u"7"_s, u"--no-audio"_s, u"--no-mods"_s, u"--background"_s,
            u"--look"_s, u"180,-60"_s, u"--game-mode"_s, u"creative"_s,
            u"--chat"_s, u"/fill ~-7 ~-4 ~2 ~7 ~-1 ~16 water"_s,
            u"--chat"_s, u"/fill ~-7 ~-4 ~2 ~7 ~-2 ~5 sand"_s,
            u"--chat"_s, u"/fill ~-7 ~-4 ~2 ~7 ~-1 ~3 sand"_s,
            u"--chat"_s, u"/time set day"_s,
            u"--chat"_s, u"/gamerule daylightCycle false"_s,
            u"--overlay"_s, u"@100 key:hideHud"_s,
            u"--overlay"_s, u"@200 fly"_s,
            u"--chat"_s, u"@600 /tp ~ ~1 ~3"_s};
}

// The width (pixels) of the strip of columns averaged into one; single texels of the sand under
// the water can be as dark and blue as the deep water.
constexpr int strip = 16;

// Whether the strip at (x, y) shows the sand (yellowish) rather than the deep water (blue).
bool sandy(const QImage &image, int x, int y)
{
    int red = 0;
    int blue = 0;
    for (int i = 0; i < strip; ++i) {
        red += qRed(image.pixel(x + i, y));
        blue += qBlue(image.pixel(x + i, y));
    }
    return red > blue;
}

// The row where the column turns from the deep water to the sand under the shallow water: the
// bottom of the band less the number of its sandy rows (a few dark texels or bright glints on
// either side shift it by a row each, not to their place).
int edgeRow(const QImage &image, int x, int top, int bottom)
{
    int sand = 0;
    for (int y = top; y < bottom; ++y) {
        if (sandy(image, x, y)) {
            ++sand;
        }
    }
    return bottom - sand;
}

// How far (pixels) the points move up and down around the straight line that fits them best: the
// spread of their distances from it (a slightly tilted edge is no swing).
double swingAroundLine(const QList<QPointF> &points)
{
    const double count = static_cast<double>(points.size());
    double sumX = 0.0;
    double sumY = 0.0;
    for (const QPointF &point : points) {
        sumX += point.x();
        sumY += point.y();
    }
    const double meanX = sumX / count;
    const double meanY = sumY / count;
    double across = 0.0;
    double along = 0.0;
    for (const QPointF &point : points) {
        across += (point.x() - meanX) * (point.y() - meanY);
        along += (point.x() - meanX) * (point.x() - meanX);
    }
    const double slope = along > 0.0 ? across / along : 0.0;

    double lowest = 0.0;
    double highest = 0.0;
    for (const QPointF &point : points) {
        const double off = point.y() - (meanY + slope * (point.x() - meanX));
        lowest = std::min(lowest, off);
        highest = std::max(highest, off);
    }
    return highest - lowest;
}

} // namespace

class TestWaterRefractionEdge : public QObject
{
    Q_OBJECT

private slots:
    void theEdgeUnderTheWaterStaysStraight()
    {
        // QC_WATER_EDGE_IMAGE checks a picture taken before instead of rendering one.
        if (const QString given = qEnvironmentVariable("QC_WATER_EDGE_IMAGE"); !given.isEmpty()) {
            const QImage image(given);
            QVERIFY2(!image.isNull(), qPrintable(given));
            checkEdge(image);
            return;
        }

        QTemporaryDir data;
        QVERIFY(data.isValid());
        QFile file(data.filePath(u"settings.ini"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(settings);
        file.close();

        const QString shot = data.filePath(u"shore.png"_s);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(u"QTCRAFT_OFFSCREEN"_s, u"1"_s);
        environment.insert(u"QTCRAFT_DATA_DIR"_s, data.path());
        environment.insert(u"QTCRAFT_SCREENSHOT"_s, shot);
        environment.insert(u"QTCRAFT_SCREENSHOT_FRAMES"_s, u"240"_s);
        environment.insert(u"QTCRAFT_WINDOW_SIZE"_s, u"1280x720"_s);
        environment.insert(u"QTCRAFT_ANIMATION_TICK"_s, u"40"_s);
        QProcess game;
        game.setProcessEnvironment(environment);
        game.setProcessChannelMode(QProcess::MergedChannels);
        game.start(QStringLiteral(QTCRAFT_APP_PATH), arguments());
        // Generous for sanitizer builds; the test's TIMEOUT (300 s) stops it before that anyway.
        QVERIFY(game.waitForFinished(290'000));
        QVERIFY2(game.exitCode() == 0, game.readAll().right(2000).constData());

        const QImage image(shot);
        QVERIFY2(!image.isNull(), qPrintable(shot));
        checkEdge(image);
    }

private:
    // The edge lies at about 44 % of the height, above the shore (sand above the water) at about
    // 72 %; it is looked for in the middle 60 % of the columns (the pool's sides and their shadows
    // show at the left and right).
    static void checkEdge(const QImage &image)
    {
        const int top = image.height() / 4;
        const int bottom = image.height() * 62 / 100;
        QList<QPointF> points;
        for (int x = image.width() / 5; x < image.width() * 4 / 5; x += strip) {
            points.append(QPointF(x, edgeRow(image, x, top, bottom)));
        }
        // The scene: the edge lies inside the band, the sand reaches its bottom.
        for (const QPointF &point : points) {
            QVERIFY2(point.y() > top + 4 && point.y() < bottom - 4, qPrintable(u"no edge at x %1"_s.arg(point.x())));
        }

        const double swing = swingAroundLine(points) / image.height();
        const QString report = u"the edge swings by %1 % of the height"_s.arg(100.0 * swing, 0, 'f', 2);
        qInfo().noquote() << report;
        QVERIFY2(swing <= maxSwing, qPrintable(report));
    }
};

QTEST_GUILESS_MAIN(TestWaterRefractionEdge)
#include "test_water_refraction_edge.moc"
