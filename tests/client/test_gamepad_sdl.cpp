#include "app/input/QcGamepadBackend.h"
#include "clientcore/input/QcGamepadSource.h"

#if QC_HAVE_SDL_GAMEPAD
#include "app/input/QcSdlGamepadSource.h"

#include <SDL3/SDL.h>
#endif

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

const QString sourceDir = QStringLiteral(QTCRAFT_SOURCE_DIR);

struct ProcessResult
{
    int exitCode = -1;
    QString output;
};

// Runs a program with QC_SDL_CACHE set to `cacheRoot` (when given); stdout and stderr together.
ProcessResult runProcess(const QString &program, const QStringList &arguments, const QString &cacheRoot = {})
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    if (!cacheRoot.isEmpty()) {
        environment.insert(u"QC_SDL_CACHE"_s, cacheRoot);
    }
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForFinished(60000) || process.exitStatus() != QProcess::NormalExit) {
        return {};
    }
    return {process.exitCode(), QString::fromUtf8(process.readAll())};
}

// Runs scripts/build-sdl.sh (of `projectDir`) with --print-prefix, which never builds, and returns
// its output line; empty when it failed.
QString printedPrefix(const QStringList &extraArguments, const QString &cacheRoot = {}, const QString &projectDir = sourceDir)
{
    const ProcessResult result = runProcess(u"sh"_s, QStringList{projectDir + u"/scripts/build-sdl.sh"_s} + extraArguments + QStringList{u"--print-prefix"_s}, cacheRoot);
    return result.exitCode == 0 ? result.output.trimmed() : QString();
}

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

bool writeText(const QString &path, const QString &text)
{
    QFile file(path);
    return QDir().mkpath(QFileInfo(path).path()) && file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(text.toUtf8()) >= 0;
}

QString pinnedTag()
{
    return readText(sourceDir + u"/3rdparty/sdl3-tag.txt"_s);
}

// CONFIG_VERSION of scripts/build-sdl.sh, the last part of every cache entry's name.
QString configVersion()
{
    const QRegularExpressionMatch match = QRegularExpression(u"^CONFIG_VERSION=(\\w+)$"_s, QRegularExpression::MultilineOption).match(readText(sourceDir + u"/scripts/build-sdl.sh"_s));
    return match.captured(1);
}

QString machineArch()
{
    QProcess uname;
    uname.start(u"uname"_s, {u"-m"_s});
    return uname.waitForFinished(10000) ? QString::fromUtf8(uname.readAllStandardOutput()).trimmed() : QString();
}

// A fake SDL3 CMake package of `version` in the cache entry of `tag` (no library, an interface
// target), enough for cmake/QcGamepad.cmake to find it.
bool writeFakeEntry(const QString &cacheRoot, const QString &tag, const QString &version)
{
    const QString dir = cacheRoot + u'/' + tag + u'-' + machineArch() + u'-' + configVersion() + u"/lib/cmake/SDL3/"_s;
    return writeText(dir + u"SDL3Config.cmake"_s, u"add_library(SDL3::SDL3-static INTERFACE IMPORTED)\nset(SDL3_SDL3-static_FOUND TRUE)\n"_s)
        && writeText(dir + u"SDL3ConfigVersion.cmake"_s, u"set(PACKAGE_VERSION %1)\nset(PACKAGE_VERSION_COMPATIBLE TRUE)\n"_s.arg(version));
}

#if QC_HAVE_SDL_GAMEPAD
// A virtual SDL joystick of gamepad type with every gamepad button and axis in SDL's order.
struct VirtualJoystick
{
    SDL_JoystickID id = 0;
    SDL_Joystick *joystick = nullptr;

    bool attach(quint16 vendor, quint16 product, const char *name)
    {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.vendor_id = vendor;
        desc.product_id = product;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = name;
        id = SDL_AttachVirtualJoystick(&desc);
        joystick = id ? SDL_OpenJoystick(id) : nullptr;
        return joystick != nullptr;
    }

    void detach()
    {
        if (joystick) {
            SDL_CloseJoystick(joystick);
            SDL_DetachVirtualJoystick(id);
        }
        joystick = nullptr;
    }

    VirtualJoystick() = default;
    ~VirtualJoystick() { detach(); }
    Q_DISABLE_COPY_MOVE(VirtualJoystick)
};

// The pad of `source` with this name, or nullopt.
std::optional<QcGamepadInfo> padNamed(const QcGamepadSource &source, const QString &name)
{
    const QList<QcGamepadInfo> pads = source.pads();
    for (const QcGamepadInfo &pad : pads) {
        if (pad.name == name) {
            return pad;
        }
    }
    return std::nullopt;
}
#endif

} // namespace

// The SDL3 gamepad backend (decision E-540): it starts without any pad connected and without a
// window, and the build finds SDL in the shared cache laid out by tag and architecture.
class TestGamepadSdl : public QObject
{
    Q_OBJECT

private slots:
    // SDL's gamepad subsystem starts, polls and stops, twice in a row (SDL counts its users).
    // A pad connected to the machine may show up; none is needed.
    void backendStartsWithoutDevice()
    {
        if (!QcGamepadBackend::compiledIn()) {
            QSKIP("built with QC_GAMEPAD=OFF");
        }

        for (int round = 0; round < 2; ++round) {
            const std::unique_ptr<QcGamepadSource> source = QcGamepadBackend::create();
            QVERIFY2(source, "SDL did not start, see the qtcraft.gamepad warning");
            QVERIFY(source->isAvailable());
            QCOMPARE(source->backendName(), u"sdl3"_s);
            source->poll();
            QTest::qWait(20);
            source->poll();
            const QList<QcGamepadInfo> pads = source->pads();
            for (const QcGamepadInfo &pad : pads) {
                QVERIFY(pad.id > 0);
                QVERIFY(source->contains(pad.id));
            }
        }
    }

    // The cache entry is <cache>/<tag>-<arch>; the universal build of the release bundle has an
    // entry of its own.
    void cacheLayoutByTagAndArchitecture()
    {
#ifdef Q_OS_WIN
        QSKIP("scripts/build-sdl.sh needs a POSIX shell");
#endif
        const QString tag = pinnedTag();
        QVERIFY(tag.startsWith(u"release-3."_s));
        const QString config = configVersion();
        QVERIFY(!config.isEmpty());
        QTemporaryDir cache;
        QVERIFY(cache.isValid());

        const QString arch = machineArch();
        QCOMPARE(printedPrefix({}, cache.path()), cache.path() + u'/' + tag + u'-' + arch + u'-' + config);
        QCOMPARE(printedPrefix({u"--universal"_s}, cache.path()), cache.path() + u'/' + tag + u"-universal-"_s + config);
        QCOMPARE(printedPrefix({u"--arch"_s, u"x86_64"_s}, cache.path()), cache.path() + u'/' + tag + u"-x86_64-"_s + config);
        QVERIFY2(QDir(cache.path()).isEmpty(), "--print-prefix must not build or create anything");

        // Wrong use ends with exit code 2.
        const QString script = sourceDir + u"/scripts/build-sdl.sh"_s;
        QCOMPARE(runProcess(u"sh"_s, {script, u"--arch"_s, u"x86/64"_s, u"--print-prefix"_s}, cache.path()).exitCode, 2);
        QCOMPARE(runProcess(u"sh"_s, {script, u"--arch"_s, u"x86_64"_s, u"--universal"_s, u"--print-prefix"_s}, cache.path()).exitCode, 2);
        QCOMPARE(runProcess(u"sh"_s, {script, u"--arch"_s}, cache.path()).exitCode, 2);
    }

    // This build took SDL3 from the finished cache entry of the pinned tag, not from a build
    // directory.
    void buildUsesTheCache()
    {
        if (!QcGamepadBackend::compiledIn()) {
            QSKIP("built with QC_GAMEPAD=OFF");
        }
        if (qEnvironmentVariableIsSet("QC_SDL_CACHE")) {
            QSKIP("QC_SDL_CACHE is set: the cache of this build may differ from the test's");
        }
#ifdef Q_OS_WIN
        // The configure step runs the script through Git Bash (QC_SDL_SHELL); the test asks it through sh.
        QSKIP("scripts/build-sdl.sh needs a POSIX shell");
#endif

        const QString configuredDir = QDir::cleanPath(QStringLiteral(QTCRAFT_SDL3_DIR));
        const QString sdlArch = QStringLiteral(QTCRAFT_SDL_ARCH);
        QStringList arguments;
        if (sdlArch == u"universal"_s) {
            arguments = {u"--universal"_s};
        } else if (sdlArch != u"native"_s) {
            arguments = {u"--arch"_s, sdlArch};
        }
        const QString prefix = printedPrefix(arguments);
        QVERIFY(!prefix.isEmpty());
        QVERIFY2(configuredDir.startsWith(QDir::cleanPath(prefix) + u'/'), qPrintable(configuredDir + u" is not below "_s + prefix));
        QVERIFY(QFile::exists(prefix + u"/.complete"_s));
        QVERIFY(prefix.contains(pinnedTag()));
        // The licence texts the release bundle carries.
        for (const QString &file : {u"LICENSE.txt"_s, u"HIDAPI-LICENSE.txt"_s, u"LIBM-NOTICE.txt"_s}) {
            QVERIFY2(QFileInfo(prefix + u'/' + file).size() > 0, qPrintable(file));
        }
    }

    // An empty tag file is wrong use, not a cache entry named "-<arch>".
    void emptyTagIsRejected()
    {
#ifdef Q_OS_WIN
        QSKIP("scripts/build-sdl.sh needs a POSIX shell");
#endif
        QTemporaryDir project;
        QVERIFY(project.isValid());
        QVERIFY(QDir().mkpath(project.path() + u"/scripts"_s));
        QVERIFY(QFile::copy(sourceDir + u"/scripts/build-sdl.sh"_s, project.path() + u"/scripts/build-sdl.sh"_s));
        QVERIFY(writeText(project.path() + u"/3rdparty/sdl3-tag.txt"_s, u" \n"_s));
        const ProcessResult result = runProcess(u"sh"_s, {project.path() + u"/scripts/build-sdl.sh"_s, u"--print-prefix"_s}, project.path() + u"/cache"_s);
        QCOMPARE(result.exitCode, 2);
        QVERIFY2(result.output.contains(u"empty"_s), qPrintable(result.output));
    }

    // cmake/QcGamepad.cmake in a small project of its own with fake cache entries: a new tag picks
    // its own entry although SDL3_DIR is cached, an entry of the wrong version fails clearly, and an
    // SDL3_DIR the user sets outside the cache is kept.
    void configurePicksTheEntryOfTheTag()
    {
#ifdef Q_OS_WIN
        QSKIP("scripts/build-sdl.sh needs a POSIX shell");
#endif
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString project = root.path() + u"/project"_s;
        const QString cache = root.path() + u"/cache"_s;
        const QString build = root.path() + u"/build"_s;
        QVERIFY(QDir().mkpath(project + u"/scripts"_s) && QDir().mkpath(project + u"/cmake"_s));
        QVERIFY(QFile::copy(sourceDir + u"/scripts/build-sdl.sh"_s, project + u"/scripts/build-sdl.sh"_s));
        QVERIFY(QFile::copy(sourceDir + u"/cmake/QcGamepad.cmake"_s, project + u"/cmake/QcGamepad.cmake"_s));
        QVERIFY(writeText(project + u"/CMakeLists.txt"_s,
                          u"cmake_minimum_required(VERSION 3.21)\nproject(QcGamepadProbe NONE)\n"
                          "list(APPEND CMAKE_MODULE_PATH \"${PROJECT_SOURCE_DIR}/cmake\")\ninclude(QcGamepad)\n"
                          "file(WRITE \"${CMAKE_BINARY_DIR}/sdl3-dir.txt\" \"${SDL3_DIR}\")\n"_s));
        QVERIFY(writeFakeEntry(cache, u"release-3.4.18"_s, u"3.4.18"_s));
        QVERIFY(writeFakeEntry(cache, u"release-3.4.20"_s, u"3.4.20"_s));
        QVERIFY(writeFakeEntry(cache, u"release-3.4.21"_s, u"3.4.19"_s));

        const QString cmake = QStringLiteral(QTCRAFT_CMAKE_COMMAND);
        const auto configure = [&](const QString &tag, const QStringList &extra = {}) {
            if (!writeText(project + u"/3rdparty/sdl3-tag.txt"_s, tag + u'\n')) {
                return ProcessResult{};
            }
            return runProcess(cmake, QStringList{u"-S"_s, project, u"-B"_s, build, u"-DQC_SDL_AUTO_BUILD=OFF"_s} + extra, cache);
        };
        const QString entryEnd = u'-' + machineArch() + u'-' + configVersion() + u"/lib/cmake/SDL3"_s;

        ProcessResult result = configure(u"release-3.4.18"_s);
        QVERIFY2(result.exitCode == 0, qPrintable(result.output));
        QCOMPARE(readText(build + u"/sdl3-dir.txt"_s), cache + u"/release-3.4.18"_s + entryEnd);

        result = configure(u"release-3.4.20"_s);
        QVERIFY2(result.exitCode == 0, qPrintable(result.output));
        QCOMPARE(readText(build + u"/sdl3-dir.txt"_s), cache + u"/release-3.4.20"_s + entryEnd);

        result = configure(u"release-3.4.21"_s);
        QCOMPARE(result.exitCode, 1);
        QVERIFY2(result.output.contains(u"is version 3.4.19"_s), qPrintable(result.output));

        // The user's own SDL3 outside the cache stays, as long as its version matches the tag.
        const QString userDir = root.path() + u"/user/lib/cmake/SDL3"_s;
        QVERIFY(QDir().mkpath(userDir));
        for (const QString &file : {u"/SDL3Config.cmake"_s, u"/SDL3ConfigVersion.cmake"_s}) {
            QVERIFY(QFile::copy(cache + u"/release-3.4.18"_s + entryEnd + file, userDir + file));
        }
        result = configure(u"release-3.4.18"_s, {u"-DSDL3_DIR="_s + userDir});
        QVERIFY2(result.exitCode == 0, qPrintable(result.output));
        QCOMPARE(readText(build + u"/sdl3-dir.txt"_s), userDir);
        result = configure(u"release-3.4.18"_s);
        QVERIFY2(result.exitCode == 0, qPrintable(result.output));
        QCOMPARE(readText(build + u"/sdl3-dir.txt"_s), userDir);
    }

#if QC_HAVE_SDL_GAMEPAD
    // SDL's virtual joystick through the real backend: hot-plug signals, the family from vendor
    // and product, the button order, normalised axes (-32768 clamped to -1, triggers 0..1), the
    // poll interval (fast only while a pad is open).
    void sdlVirtualJoystick()
    {
        QcSdlGamepadSource source(8);
        QVERIFY2(source.isAvailable(), qPrintable(source.initError()));
        QSignalSpy connected(&source, &QcGamepadSource::padConnected);
        QSignalSpy disconnected(&source, &QcGamepadSource::padDisconnected);
        source.poll();
        if (source.pads().isEmpty()) {
            QCOMPARE(source.pollIntervalMs(), QcSdlGamepadSource::idleIntervalMs);
        }
        connected.clear();

        VirtualJoystick xbox;
        QVERIFY2(xbox.attach(0x045e, 0x0b12, "QtCraft test Xbox"), SDL_GetError());
        source.poll();
        QCOMPARE(connected.count(), 1);
        const std::optional<QcGamepadInfo> pad = padNamed(source, u"QtCraft test Xbox"_s);
        QVERIFY(pad);
        QCOMPARE(connected.at(0).at(0).toInt(), pad->id);
        QCOMPARE(pad->type, QcGamepadType::Xbox);
        QCOMPARE(source.pollIntervalMs(), 8);
        QCOMPARE(source.state(pad->id), QcGamepadState{});

        checkButtons(source, pad->id, xbox.joystick);
        checkAxes(source, pad->id, xbox.joystick);

        xbox.detach();
        source.poll();
        QCOMPARE(disconnected.count(), 1);
        QCOMPARE(disconnected.at(0).at(0).toInt(), pad->id);
        QVERIFY(!source.contains(pad->id));
        if (source.pads().isEmpty()) {
            QCOMPARE(source.pollIntervalMs(), QcSdlGamepadSource::idleIntervalMs);
        }
    }

    // The family of other pads: SDL's own type first, the vendor and the name otherwise.
    void sdlVirtualJoystickTypes()
    {
        QcSdlGamepadSource source(8);
        QVERIFY(source.isAvailable());
        const struct
        {
            quint16 vendor;
            quint16 product;
            const char *name;
            QcGamepadType type;
        } cases[] = {
            {0x054c, 0x0ce6, "QtCraft test DualSense", QcGamepadType::PlayStation},
            {0x057e, 0x2009, "QtCraft test Pro", QcGamepadType::SwitchPro},
            {0x1209, 0x0001, "QtCraft test pad", QcGamepadType::Generic},
        };
        for (const auto &test : cases) {
            VirtualJoystick joystick;
            QVERIFY2(joystick.attach(test.vendor, test.product, test.name), SDL_GetError());
            source.poll();
            const std::optional<QcGamepadInfo> pad = padNamed(source, QString::fromUtf8(test.name));
            QVERIFY2(pad, test.name);
            QCOMPARE(pad->type, test.type);
            joystick.detach();
            source.poll();
            QVERIFY(!source.contains(pad->id));
        }
    }
#else
    void sdlVirtualJoystick()
    {
        QSKIP("built with QC_GAMEPAD=OFF");
    }
#endif

private:
#if QC_HAVE_SDL_GAMEPAD
    // Each button alone: exactly the QcGamepadButton of the same index is pressed.
    static void checkButtons(QcSdlGamepadSource &source, int id, SDL_Joystick *joystick)
    {
        for (std::size_t i = 0; i < qcGamepadButtonCount; ++i) {
            QVERIFY(SDL_SetJoystickVirtualButton(joystick, static_cast<int>(i), true));
            source.poll();
            for (std::size_t j = 0; j < qcGamepadButtonCount; ++j) {
                QVERIFY2(source.state(id).pressed(QcGamepadButton(j)) == (i == j), qPrintable(u"button %1 while %2 is down"_s.arg(j).arg(i)));
            }
            QVERIFY(SDL_SetJoystickVirtualButton(joystick, static_cast<int>(i), false));
        }
        source.poll();
        QCOMPARE(source.state(id), QcGamepadState{});
    }

    static void checkAxes(QcSdlGamepadSource &source, int id, SDL_Joystick *joystick)
    {
        QSignalSpy changed(&source, &QcGamepadSource::stateChanged);
        QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, SDL_JOYSTICK_AXIS_MIN));
        QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, SDL_JOYSTICK_AXIS_MAX));
        QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHTX, 16384));
        QVERIFY(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_JOYSTICK_AXIS_MAX));
        source.poll();
        QCOMPARE(changed.count(), 1);
        const QcGamepadState state = source.state(id);
        QCOMPARE(state.axis(QcGamepadAxis::LeftX), -1.0f);
        QCOMPARE(state.axis(QcGamepadAxis::LeftY), 1.0f);
        QVERIFY(std::abs(state.axis(QcGamepadAxis::RightX) - 0.5f) < 1e-3f);
        QCOMPARE(state.axis(QcGamepadAxis::RightY), 0.0f);
        // A virtual trigger rests at the joystick minimum, which SDL's gamepad reports as 0.
        QCOMPARE(state.axis(QcGamepadAxis::LeftTrigger), 0.0f);
        QCOMPARE(state.axis(QcGamepadAxis::RightTrigger), 1.0f);

        // Polling an unchanged pad emits nothing.
        source.poll();
        QCOMPARE(changed.count(), 1);
    }
#endif
};

QTEST_GUILESS_MAIN(TestGamepadSdl)
#include "test_gamepad_sdl.moc"
