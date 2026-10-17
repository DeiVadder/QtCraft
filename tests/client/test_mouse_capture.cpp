#include "clientcore/camera/QcMouseCapture.h"

#include <QTest>

namespace {

using Change = QcMouseCapture::Change;

QcMouseCapture::Situation world(Qt::MouseButtons held = Qt::NoButton)
{
    return {true, true, false, held};
}

QcMouseCapture::Situation screenOpen()
{
    return {false, true, false, Qt::NoButton};
}

// A world entered with the mouse captured.
QcMouseCapture capturedInWorld()
{
    QcMouseCapture capture;
    capture.update(world());
    return capture;
}

} // namespace

class TestMouseCapture : public QObject
{
    Q_OBJECT

private slots:
    // Every screen (inventory, crafting, chest, furnace, pause, settings, handbook, chat, death)
    // takes the world's input away; closing it brings the capture back without a click.
    void closingAScreenCapturesAgain()
    {
        QcMouseCapture capture = capturedInWorld();
        QVERIFY(capture.captured());

        QCOMPARE(capture.update(screenOpen()), Change::Release);
        QVERIFY(!capture.captured());
        QCOMPARE(capture.update(screenOpen()), Change::None);

        QCOMPARE(capture.update(world()), Change::Capture);
        QVERIFY(capture.captured());
        // Nothing changes while the world keeps the input.
        QCOMPARE(capture.update(world()), Change::None);
        QVERIFY(capture.acceptRelease(Qt::LeftButton));
    }

    // The button that closed a screen (still held at that moment) does not mine or place when it is
    // released; the next press and release act normally.
    void heldButtonOfTheClosingClickDoesNothing()
    {
        QcMouseCapture capture = capturedInWorld();
        capture.update(screenOpen());

        QCOMPARE(capture.update(world(Qt::LeftButton | Qt::RightButton)), Change::Capture);
        QVERIFY(!capture.acceptRelease(Qt::LeftButton));
        QVERIFY(!capture.acceptRelease(Qt::RightButton));
        QVERIFY(capture.acceptRelease(Qt::LeftButton));
    }

    // Entering the world (the loading screen ends) captures like closing a screen.
    void enteringTheWorldCaptures()
    {
        QcMouseCapture capture;
        QCOMPARE(capture.update(world()), Change::Capture);
        QVERIFY(capture.captured());
    }

    // A click into the world while the mouse is free only captures it.
    void clickThatCapturesDoesNothing()
    {
        QcMouseCapture capture = capturedInWorld();
        capture.released();
        QVERIFY(!capture.acceptRelease(Qt::RightButton));

        QCOMPARE(capture.clickedIntoWorld(world(Qt::RightButton), Qt::RightButton), Change::Capture);
        QVERIFY(!capture.acceptRelease(Qt::RightButton));
        QVERIFY(capture.acceptRelease(Qt::RightButton));
        QCOMPARE(capture.clickedIntoWorld(world(), Qt::LeftButton), Change::None);
    }

    // Losing the focus frees the mouse; coming back to the window waits for a click, so switching
    // applications never grabs the mouse.
    void returningWindowWaitsForAClick()
    {
        QcMouseCapture capture = capturedInWorld();
        capture.released();
        QVERIFY(!capture.captured());

        QCOMPARE(capture.update(world()), Change::None);
        QVERIFY(!capture.captured());
        QCOMPARE(capture.clickedIntoWorld(world(Qt::LeftButton), Qt::LeftButton), Change::Capture);
    }

    // An inactive window or a scripted run never captures; the hooks learn that the world took the
    // input back.
    void inactiveWindowAndScriptedRunsStayFree()
    {
        QcMouseCapture capture;
        capture.update(screenOpen());
        QcMouseCapture::Situation inactive = world();
        inactive.windowActive = false;
        QCOMPARE(capture.update(inactive), Change::None);
        QVERIFY(capture.returnedWithoutCapture());
        QVERIFY(!capture.captured());
        QCOMPARE(capture.clickedIntoWorld(inactive, Qt::LeftButton), Change::None);

        capture.update(screenOpen());
        QVERIFY(!capture.returnedWithoutCapture());
        QcMouseCapture::Situation scripted = world();
        scripted.backgroundRun = true;
        QCOMPARE(capture.update(scripted), Change::None);
        QVERIFY(capture.returnedWithoutCapture());
        QCOMPARE(capture.clickedIntoWorld(scripted, Qt::LeftButton), Change::None);
    }
};

QTEST_GUILESS_MAIN(TestMouseCapture)
#include "test_mouse_capture.moc"
