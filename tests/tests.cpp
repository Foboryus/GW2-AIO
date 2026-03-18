#include <QtTest>
#include <QSignalSpy>

// Include headers to test
#include "core/Result.h"
#include "core/ZipExtractor.h"
#include "core/GW2Detector.h"
#include "core/CredentialManager.h"
#include "features/radial/RadialMenu.h"
#include "features/markers/MarkerModels.h"
#include "features/dps/ArcDPSModels.h"

/**
 * @brief Unit tests for GW2 AIO Manager
 */
class GW2AIOTests : public QObject
{
    Q_OBJECT
    
private slots:
    // Test fixtures
    void initTestCase();
    void cleanupTestCase();
    
    // Result type tests
    void testResultSuccess();
    void testResultError();
    void testResultValueOr();
    
    // GW2Detector tests
    void testGW2DetectorCommonPaths();
    
    // RadialMenu tests
    void testRadialMenuSerialization();
    void testRadialItemDefaults();
    
    // Marker tests
    void testMarkerDefaults();
    void testMarkerCategoryTree();
    
    // ArcDPS model tests
    void testCombatEventSize();
    void testStateChangeEnum();
    
private:
    QString m_testDataDir;
};

void GW2AIOTests::initTestCase()
{
    m_testDataDir = QDir::tempPath() + "/gw2aio_tests";
    QDir().mkpath(m_testDataDir);
}

void GW2AIOTests::cleanupTestCase()
{
    QDir(m_testDataDir).removeRecursively();
}

// Result type tests
void GW2AIOTests::testResultSuccess()
{
    Result<int> result = 42;
    
    QVERIFY(result.isSuccess());
    QVERIFY(!result.isError());
    QCOMPARE(result.value(), 42);
}

void GW2AIOTests::testResultError()
{
    Result<int> result = Result<int>::error("Something went wrong");
    
    QVERIFY(!result.isSuccess());
    QVERIFY(result.isError());
    QCOMPARE(result.error(), QString("Something went wrong"));
}

void GW2AIOTests::testResultValueOr()
{
    Result<int> success = 42;
    Result<int> failure = Result<int>::error("Failed");
    
    QCOMPARE(success.valueOr(0), 42);
    QCOMPARE(failure.valueOr(0), 0);
}

// GW2Detector tests
void GW2AIOTests::testGW2DetectorCommonPaths()
{
    GW2Detector detector;
    QStringList paths = detector.getCommonPaths();
    
    QVERIFY(!paths.isEmpty());
    QVERIFY(paths.contains("C:/Program Files/Guild Wars 2") || 
            paths.contains("C:/Program Files (x86)/Guild Wars 2"));
}

// RadialMenu tests
void GW2AIOTests::testRadialMenuSerialization()
{
    RadialMenu menu;
    menu.id = "test_menu";
    menu.name = "Test Menu";
    menu.hotkey = "V";
    
    RadialItem item;
    item.id = "item1";
    item.label = "Item 1";
    item.command = "/mount raptor";
    menu.items.append(item);
    
    QJsonObject json = menu.toJson();
    
    QCOMPARE(json["id"].toString(), QString("test_menu"));
    QCOMPARE(json["name"].toString(), QString("Test Menu"));
    QCOMPARE(json["hotkey"].toString(), QString("V"));
    QCOMPARE(json["items"].toArray().size(), 1);
    
    // Deserialize
    RadialMenu loaded = RadialMenu::fromJson(json);
    QCOMPARE(loaded.id, menu.id);
    QCOMPARE(loaded.name, menu.name);
    QCOMPARE(loaded.items.size(), 1);
}

void GW2AIOTests::testRadialItemDefaults()
{
    RadialItem item;
    
    QVERIFY(item.enabled);
    QVERIFY(item.iconPath.isEmpty());
    QVERIFY(!item.isSubmenu);
}

// Marker tests
void GW2AIOTests::testMarkerDefaults()
{
    Marker marker;
    
    QCOMPARE(marker.mapId, 0);
    QCOMPARE(marker.xpos, 0.0f);
    QCOMPARE(marker.ypos, 0.0f);
    QCOMPARE(marker.zpos, 0.0f);
    QCOMPARE(marker.iconSize, 1.0f);
    QCOMPARE(marker.fadeNear, 700.0f);
    QCOMPARE(marker.fadeFar, 900.0f);
    QVERIFY(!marker.autoTrigger);
}

void GW2AIOTests::testMarkerCategoryTree()
{
    MarkerCategory root;
    root.name = "Root";
    root.displayName = "Root Category";
    root.isEnabled = true;
    
    MarkerCategory child;
    child.name = "Child";
    child.displayName = "Child Category";
    root.children.append(child);
    
    QCOMPARE(root.children.size(), 1);
    QCOMPARE(root.children[0].name, QString("Child"));
}

// ArcDPS tests
void GW2AIOTests::testCombatEventSize()
{
    // EVTC combat events have a specific size
    // This ensures our struct matches the format
    QCOMPARE(sizeof(ArcDPS::CombatEvent), 64);  // Standard EVTC event size
}

void GW2AIOTests::testStateChangeEnum()
{
    // Test some known state change values
    QCOMPARE(static_cast<int>(ArcDPS::StateChange::EnterCombat), 1);
    QCOMPARE(static_cast<int>(ArcDPS::StateChange::ExitCombat), 2);
    QCOMPARE(static_cast<int>(ArcDPS::StateChange::ChangeUp), 3);
    QCOMPARE(static_cast<int>(ArcDPS::StateChange::ChangeDead), 4);
}

QTEST_MAIN(GW2AIOTests)
#include "tests.moc"
