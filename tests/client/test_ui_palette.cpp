#include "clientcore/settings/QcCharacterProfile.h"
#include "clientcore/settings/QcUiPalette.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMetaEnum>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryFile>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QSet<QString> paletteNames()
{
    QSet<QString> names;
    for (const QcUiPalette::Entry &entry : QcUiPalette::entries()) {
        names.insert(QString::fromLatin1(entry.name));
    }
    return names;
}

// Every QML file of the module, in the topic folders below QTCRAFT_QML_DIR as well (decision E-670).
QStringList qmlFiles()
{
    QStringList files;
    QDirIterator it(QStringLiteral(QTCRAFT_QML_DIR), {u"*.qml"_s}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files.append(it.next());
    }
    files.sort();
    return files;
}

// The path of the QML file of a type, in whichever folder of the module it lies; empty if none.
QString qmlFileOf(const QString &type)
{
    const QStringList files = qmlFiles();
    for (const QString &path : files) {
        if (QFileInfo(path).fileName() == type + u".qml"_s) {
            return path;
        }
    }
    return {};
}

// Lines of a QML file with a colour of their own; a line may opt out with the marker comment
// "not a UI colour" (e.g. the colour choices of the character screen).
QStringList ownColours(const QString &path)
{
    static const QRegularExpression literal(
        uR"re("#[0-9a-fA-F]{3,8}"|"(white|black|red|green|blue|yellow|gray|grey|orange|transparent)"|Qt\.(rgba|rgb|hsla|darker|lighter)\()re"_s);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {u"cannot open"_s};
    }

    QStringList found;
    int number = 0;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine());
        ++number;
        if (literal.match(line).hasMatch() && !line.contains(u"not a UI colour"_s)) {
            found.append(u"%1:%2: %3"_s.arg(QFileInfo(path).fileName()).arg(number).arg(line.trimmed()));
        }
    }
    return found;
}

// The element a line belongs to: the nearest line above it that opens a block and is indented less.
std::pair<QString, qsizetype> enclosingElement(const QStringList &lines, qsizetype line)
{
    // "Type {" or an element given to a property ("screen: MessageScreen {", LazyScreen).
    static const QRegularExpression opening(uR"(^(\s*)(?:[a-z]\w*:\s*)?([A-Z]\w*)\s*\{\s*$)"_s);
    const qsizetype indent = lines[line].size() - lines[line].trimmed().size();
    for (qsizetype i = line - 1; i >= 0; --i) {
        const QRegularExpressionMatch match = opening.match(lines[i]);
        if (match.hasMatch() && match.capturedLength(1) < indent) {
            return {match.captured(2), i};
        }
    }
    return {};
}

// Whether the element opened at `start` sets textFormat: Text.PlainText on its own level.
bool setsPlainText(const QStringList &lines, qsizetype start, qsizetype indent)
{
    for (qsizetype i = start + 1; i < lines.size(); ++i) {
        const qsizetype own = lines[i].size() - lines[i].trimmed().size();
        if (!lines[i].trimmed().isEmpty() && own < indent) {
            return false;
        }
        if (own == indent && lines[i].trimmed() == u"textFormat: Text.PlainText"_s) {
            return true;
        }
    }
    return false;
}

// Whether a numeric literal in a property's value is a size of its own: above 1 and not a factor or
// divisor ("2 * Ui.space.l", "width / 2").
bool ownNumber(QStringView value, const QRegularExpressionMatch &number)
{
    if (number.captured(1).toDouble() <= 1.0) {
        return false;
    }
    const QStringView before = value.first(number.capturedStart(1)).trimmed();
    const QStringView after = value.sliced(number.capturedEnd(1)).trimmed();
    return !before.endsWith(u'*') && !before.endsWith(u'/') && !after.startsWith(u'*') && !after.startsWith(u'/');
}

// Whether a line (without comment and strings) gives a property a number above 1 of its own.
bool assignsOwnNumber(const QString &code)
{
    // Properties whose numbers are no sizes: ranges and values (also of a validator), stacking,
    // counts (lines, characters, grid columns and spans), stretch factors, decimals of a shown
    // value, frame rates, and times in milliseconds.
    static const QSet<QString> notSizes = {u"stepSize"_s, u"from"_s, u"to"_s, u"value"_s, u"bottom"_s, u"top"_s,
                                           u"opacity"_s, u"z"_s, u"maximumLineCount"_s, u"maximumLength"_s,
                                           u"columns"_s, u"rows"_s, u"columnSpan"_s, u"rowSpan"_s,
                                           u"horizontalStretchFactor"_s, u"verticalStretchFactor"_s, u"model"_s,
                                           u"valueText"_s, u"framesPerSecond"_s, u"interval"_s, u"duration"_s};
    static const QRegularExpression assignment(
        uR"(^\s*(?:readonly\s+)?(?:required\s+)?(?:property\s+[\w<>]+\s+)?([\w.]+)\s*:\s*(.+)$)"_s);
    static const QRegularExpression number(uR"((?<![\w.])(\d+(?:\.\d+)?|\.\d+)(?![\w.]))"_s);

    const QRegularExpressionMatch match = assignment.match(code);
    if (!match.hasMatch()) {
        return false;
    }
    const QString name = match.captured(1).section(u'.', -1);
    if (notSizes.contains(name) || name.endsWith(u"Ms"_s)) {
        return false;
    }
    const QString value = match.captured(2);
    for (auto it = number.globalMatch(value); it.hasNext();) {
        if (ownNumber(value, it.next())) {
            return true;
        }
    }
    return false;
}

// Lines of a QML file with a size of its own instead of a step of Ui (decision E-845): a literal
// passed to a px()/hudPx() or to Qt.size(), or a number above 1 given to a property or declared with
// one (an allow-list names the properties whose numbers are no sizes; factors are allowed).
QStringList ownSizes(const QString &path)
{
    static const QRegularExpression scaled(uR"(\b\w*\.(px|hudPx)\(\s*[\d.]|\bQt\.size\(\s*[\d.])"_s);
    static const QRegularExpression strings(uR"re("(?:[^"\\]|\\.)*"|\x27(?:[^\x27\\]|\\.)*\x27)re"_s);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {u"cannot open"_s};
    }

    QStringList found;
    int number = 0;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine());
        ++number;
        QString code = line.section(u"//"_s, 0, 0);
        code.replace(strings, u"\"\""_s);
        if (scaled.match(code).hasMatch() || assignsOwnNumber(code)) {
            found.append(u"%1:%2: %3"_s.arg(QFileInfo(path).fileName()).arg(number).arg(line.trimmed()));
        }
    }
    return found;
}

} // namespace

class TestUiPalette : public QObject
{
    Q_OBJECT

private slots:
    void entriesFollowTheRoles()
    {
        const std::span<const QcUiPalette::Entry> entries = QcUiPalette::entries();
        QCOMPARE(qsizetype(entries.size()), qsizetype(QcUiPalette::Role::RoleCount));
        for (std::size_t i = 0; i < entries.size(); ++i) {
            QCOMPARE(int(entries[i].role), int(i));
        }
        QCOMPARE(paletteNames().size(), qsizetype(entries.size()));
    }

    // The variants change only colours, and the names choose them.
    void presetsChangeOnlyColours()
    {
        QCOMPARE(QcUiPalette::presetFromName(u"b"), QcUiPalette::Preset::QtDark);
        QCOMPARE(QcUiPalette::presetFromName(u"Neon"), QcUiPalette::Preset::QtNeon);
        QVERIFY(!QcUiPalette::presetFromName(u"D"));
        QcUiPalette::setPreset(QcUiPalette::Preset::QtNeon);
        QCOMPARE(QcUiPalette::color(QcUiPalette::Role::Accent), QColor(0x2c, 0xde, 0x85));
        QCOMPARE(QcUiPalette::color(QcUiPalette::Role::MenuTop), QColor(0x0c, 0x12, 0x1c));
        entriesFollowTheRoles();
        QcUiPalette::setPreset(QcUiPalette::Preset::Classic);
        QCOMPARE(QcUiPalette::color(QcUiPalette::Role::Accent), QColor(0x41, 0xcd, 0x52));
        QCOMPARE(QcUiPalette::defaultPreset, QcUiPalette::Preset::QtNeon);
    }

    void contrastFollowsWcag()
    {
        // Reference values of the WCAG 2 formula.
        QCOMPARE(QcUiPalette::contrast(Qt::white, Qt::black), 21.0);
        QCOMPARE(QcUiPalette::contrast(Qt::white, Qt::white), 1.0);
        QVERIFY(std::abs(QcUiPalette::contrast(QColor(0x76, 0x76, 0x76), Qt::white) - 4.54) < 0.01);
        QCOMPARE(QcUiPalette::composite(QColor(0, 0, 0, 128), Qt::white), QColor(127, 127, 127));
    }

    void cleanup() { QcUiPalette::setPreset(QcUiPalette::defaultPreset); }

    // Every colour variant (QcUiPalette::Preset, decision E-852) is checked like the classic one.
    static void addPresets()
    {
        QTest::addColumn<QcUiPalette::Preset>("preset");
        QTest::newRow("A classic") << QcUiPalette::Preset::Classic;
        QTest::newRow("B qt") << QcUiPalette::Preset::QtDark;
        QTest::newRow("C neon") << QcUiPalette::Preset::QtNeon;
    }

    void everyTextPairIsReadableOverEveryWorld_data() { addPresets(); }

    // Every text of every screen stays readable over snow, sand, sky, water, grass, night and caves,
    // in every colour variant.
    void everyTextPairIsReadableOverEveryWorld()
    {
        QFETCH(QcUiPalette::Preset, preset);
        QcUiPalette::setPreset(preset);
        const QList<QcUiPalette::TextPair> pairs = QcUiPalette::textPairs();
        QVERIFY(pairs.size() >= 15);
        for (const QcUiPalette::TextPair &pair : pairs) {
            const double minimum = pair.large ? QcUiPalette::largeTextMinimum : QcUiPalette::normalTextMinimum;
            const double worst = QcUiPalette::worstContrast(pair);
            QVERIFY2(worst >= minimum, qPrintable(u"%1: %2 < %3"_s.arg(QString::fromLatin1(pair.where)).arg(worst, 0, 'f', 2).arg(minimum)));
        }
    }

    // The colour swatches of the character screen have an edge that stands out (3:1, WCAG for
    // controls) on the panel over every world and against every colour they offer, so the dark hair
    // swatch does not vanish into the dark panel.
    void swatchBorderStandsOut()
    {
        const QcUiPalette::TextPair onPanel{"swatch border", QcUiPalette::Role::SwatchBorder, {QcUiPalette::Role::Scrim, QcUiPalette::Role::Panel}};
        QVERIFY2(QcUiPalette::worstContrast(onPanel) >= 3.0, qPrintable(QString::number(QcUiPalette::worstContrast(onPanel))));
        const QColor border = QcUiPalette::color(QcUiPalette::Role::SwatchBorder);
        QCOMPARE(border.alpha(), 255);
        for (const QList<quint32> *colors : {&QcCharacterProfile::skinColors(), &QcCharacterProfile::hairColors(), &QcCharacterProfile::clothColors()}) {
            for (const quint32 rgb : *colors) {
                // Dark swatches need the edge; light ones stand out from the dark panel themselves.
                const QColor swatch = QColor::fromRgb(rgb);
                const double againstPanel = QcUiPalette::contrast(swatch, QcUiPalette::composite(QcUiPalette::color(QcUiPalette::Role::Panel), Qt::black));
                QVERIFY2(againstPanel >= 3.0 || QcUiPalette::contrast(border, swatch) >= 3.0, qPrintable(QString::number(rgb, 16)));
            }
        }
        // The old edge (the slot border, dark and half transparent) fails for the dark hair colour.
        const QColor slotEdge = QcUiPalette::composite(QcUiPalette::color(QcUiPalette::Role::SlotBorder), QColor::fromRgb(0x262626));
        QVERIFY(QcUiPalette::contrast(slotEdge, QColor::fromRgb(0x1e1a18)) < 3.0);
    }

    // White text straight on the world (what the HUD did before) fails over snow: the check is not
    // trivially satisfied.
    void bareTextOverSnowFails()
    {
        const QcUiPalette::TextPair bare{"bare", QcUiPalette::Role::Text, {}};
        QVERIFY(QcUiPalette::worstContrast(bare) < 1.5);
    }

    // QML sees every colour of the palette (QcUiTheme lists them one by one).
    void themeOffersEveryRole()
    {
        QFile header(QStringLiteral(QTCRAFT_THEME_HEADER));
        QVERIFY(header.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(header.readAll());
        for (const QcUiPalette::Entry &entry : QcUiPalette::entries()) {
            const QString name = QString::fromLatin1(entry.name);
            QVERIFY2(text.contains(u"Q_PROPERTY(QColor %1 MEMBER m_%1 CONSTANT FINAL)"_s.arg(name)), qPrintable(name));
            QVERIFY2(text.contains(u"QColor m_%1 = QcUiPalette::color(QcUiPalette::Role::"_s.arg(name)), qPrintable(name));
        }
    }

    // Text from the network (chat, player and server names, reasons, labels in the world) is shown
    // as plain text: Qt's automatic format would render a name like <img src=http://...> as HTML
    // and load the image on every other client.
    void networkTextIsPlain()
    {
        static const QRegularExpression networkText(
            uR"(^\s*text:.*\b(statusText|serverName|motd|machineLabel|targetLabel|lastAddress|screen\.failure|worldLabel\.(title|detail)|modelData\.(name|description|license|credits|error|problems)|packs\.(notices|userDirectory)|playerName)\b)"_s);
        const auto plainFile = [](const QString &type) {
            QFile file(qmlFileOf(type));
            return file.open(QIODevice::ReadOnly | QIODevice::Text) && QString::fromUtf8(file.readAll()).contains(u"textFormat: Text.PlainText"_s);
        };
        // HudLabel carries the chat lines.
        QVERIFY(plainFile(u"HudLabel"_s));

        QStringList problems;
        int checked = 0;
        const QStringList files = qmlFiles();
        for (const QString &path : files) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
            const QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n');
            for (qsizetype i = 0; i < lines.size(); ++i) {
                if (!networkText.match(lines[i]).hasMatch()) {
                    continue;
                }
                const auto [type, start] = enclosingElement(lines, i);
                const qsizetype indent = lines[i].size() - lines[i].trimmed().size();
                const bool plain = type == u"Label"_s || type == u"Text"_s ? setsPlainText(lines, start, indent) : plainFile(type);
                const bool textInput = type == u"TextField"_s || type == u"TextArea"_s;
                ++checked;
                if (!plain && !textInput) {
                    problems.append(u"%1:%2: %3 shows network text without Text.PlainText"_s.arg(QFileInfo(path).fileName()).arg(i + 1).arg(type));
                }
            }
        }
        QVERIFY(checked >= 17);
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(u'\n')));
    }

    // The palette of the controls (E-279) sets every role, and the texts the controls draw on it
    // stay readable; disabled texts are dimmer but legible (3:1). In every colour variant.
    void controlPaletteIsReadable_data() { addPresets(); }

    void controlPaletteIsReadable()
    {
        QFETCH(QcUiPalette::Preset, preset);
        QcUiPalette::setPreset(preset);
        const QPalette palette = QcUiPalette::controlPalette();
        const QMetaEnum roles = QMetaEnum::fromType<QPalette::ColorRole>();
        for (int role = 0; role < QPalette::NColorRoles; ++role) {
            if (role != QPalette::NoRole) {
                QVERIFY2(palette.isBrushSet(QPalette::Active, QPalette::ColorRole(role)), roles.valueToKey(role));
            }
        }

        const QList<QcUiPalette::ControlPair> pairs = QcUiPalette::controlPairs();
        QVERIFY(pairs.size() >= 8);
        for (const QcUiPalette::ControlPair &pair : pairs) {
            const QColor background = palette.color(pair.background);
            QCOMPARE(background.alpha(), 255);
            const double ratio = QcUiPalette::contrast(QcUiPalette::composite(palette.color(pair.text), background), background);
            QVERIFY2(ratio >= QcUiPalette::normalTextMinimum,
                     qPrintable(u"%1 on %2: %3"_s.arg(QLatin1StringView(roles.valueToKey(pair.text)), QLatin1StringView(roles.valueToKey(pair.background))).arg(ratio, 0, 'f', 2)));
        }
        const QColor button = palette.color(QPalette::Button);
        QVERIFY(QcUiPalette::contrast(palette.color(QPalette::Disabled, QPalette::ButtonText), button) >= 3.0);

        // The macOS system palette in the dark scheme (black tooltip text on 25 % white over the
        // panel) fails the same check: it is what made the tooltips unreadable.
        const QColor panel = QcUiPalette::composite(QcUiPalette::color(QcUiPalette::Role::Panel), Qt::black);
        const QColor systemTooltip = QcUiPalette::composite(QColor::fromRgba(0x3fffffff), panel);
        QVERIFY(QcUiPalette::contrast(Qt::black, systemTooltip) < QcUiPalette::normalTextMinimum);
    }

    // Tooltips are ThemedToolTip (E-279): the attached ToolTip and a bare ToolTip take the style's
    // colours. Only ThemedToolTip.qml itself builds on ToolTip.
    void tooltipsAreThemed()
    {
        static const QRegularExpression styleToolTip(uR"(\bToolTip\s*(\.|\{))"_s);
        static const QRegularExpression themed(uR"(\bThemedToolTip\s*\{)"_s);
        QStringList problems;
        int themedUses = 0;
        const QStringList files = qmlFiles();
        for (const QString &path : files) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
            const QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n');
            const bool component = QFileInfo(path).fileName() == u"ThemedToolTip.qml"_s;
            for (qsizetype i = 0; i < lines.size(); ++i) {
                const QString code = lines[i].section(u"//"_s, 0, 0);
                if (!component && styleToolTip.match(code).hasMatch()) {
                    problems.append(u"%1:%2: %3"_s.arg(QFileInfo(path).fileName()).arg(i + 1).arg(lines[i].trimmed()));
                }
                themedUses += themed.match(code).hasMatch() ? 1 : 0;
            }
        }
        QVERIFY2(problems.isEmpty(), qPrintable(u"use ThemedToolTip instead:\n"_s + problems.join(u'\n')));
        // Handbook recipes, inventory slots and the recent-server rows.
        QVERIFY(themedUses >= 3);
    }

    // Screens take all colours from the palette, and only names it has.
    void screensUseOnlyThePalette()
    {
        const QStringList files = qmlFiles();
        QVERIFY(files.size() >= 18);
        const QSet<QString> names = paletteNames();
        static const QRegularExpression themeUse(uR"(\bTheme\.(\w+))"_s);

        QStringList problems;
        for (const QString &path : files) {
            problems += ownColours(path);
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
            const QString text = QString::fromUtf8(file.readAll());
            for (auto match = themeUse.globalMatch(text); match.hasNext();) {
                const QString name = match.next().captured(1);
                if (!names.contains(name)) {
                    problems.append(u"%1: unknown Theme.%2"_s.arg(QFileInfo(path).fileName(), name));
                }
            }
        }
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(u'\n')));
    }

    // The shared controls, the settings, the main menu with its pages and screens (world list, new
    // world, multiplayer, LAN, mods), the character screen and the handbook's screen with its list
    // take every gap, margin, size, radius and font size from the named steps of Ui (decisions
    // E-845, E-852), so they change in one place. Ui.qml itself holds the values. Still open
    // (stage 76.2): the handbook's page, recipes and links, the inventories, the HUD, and the
    // death, disconnect and message screens.
    void menusUseOnlyTheSizeSteps()
    {
        static const QStringList stillOpen = {u"/DeathScreen.qml"_s, u"/DisconnectScreen.qml"_s, u"/MessageScreen.qml"_s,
                                          u"/HandbookPage.qml"_s, u"/HandbookRecipe.qml"_s, u"/HandbookLink.qml"_s};
        QStringList problems;
        int checked = 0;
        const QStringList files = qmlFiles();
        for (const QString &path : files) {
            const bool folder = path.contains(u"/controls/"_s) || path.contains(u"/settings/"_s) || path.contains(u"/menus/"_s)
                                || path.contains(u"/character/"_s) || path.contains(u"/handbook/"_s);
            const bool covered = folder && std::ranges::none_of(stillOpen, [&path](const QString &name) { return path.endsWith(name); });
            // Ui.qml holds the values; an item slot is sized at the HUD's or the screen's factor
            // (ItemSlot.px), in the HUD and on the screens alike.
            if (!covered || path.endsWith(u"/Ui.qml"_s) || path.endsWith(u"/ItemSlot.qml"_s)) {
                continue;
            }
            ++checked;
            problems += ownSizes(path);
        }
        QVERIFY(checked >= 50);
        QVERIFY2(problems.isEmpty(), qPrintable(u"use a step of Ui (Ui.space, Ui.size, Ui.screen, Ui.type, Ui.radius):\n"_s + problems.join(u'\n')));

        // The check finds what it looks for.
        QTemporaryFile probe;
        QVERIFY(probe.open());
        probe.write("    spacing: Ui.px(10)\n    radius: 3\n    padding: 0\n    font.pixelSize: Ui.type.body\n"
                    "    width: parent.width - 2 * Ui.space.l\n    sourceSize: Qt.size(96, 48)\n"
                    "    property real gap: 10\n    x: parent.width - 12\n    stepSize: 5\n"
                    "    implicitWidth: slot.hudPx(52)\n    text: \"12 chunks\"\n    keepAliveMs: 60000\n");
        probe.close();
        QCOMPARE(ownSizes(probe.fileName()).size(), 6);
    }
};

QTEST_GUILESS_MAIN(TestUiPalette)
#include "test_ui_palette.moc"
