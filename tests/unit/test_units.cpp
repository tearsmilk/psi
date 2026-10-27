#include "core/units/units.h"
#include "modules/draft/input.h"
#include <QtTest>

using namespace psi;

class TestUnits : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void factors() {
        QCOMPARE(Units::toMm(Unit::Millimeter), 1.0);
        QCOMPARE(Units::toMm(Unit::Centimeter), 10.0);
        QCOMPARE(Units::toMm(Unit::Meter), 1000.0);
        QCOMPARE(Units::toMm(Unit::Inch), 25.4);
    }
    void keysRoundTrip() {
        for (Unit u : {Unit::Millimeter, Unit::Centimeter, Unit::Meter, Unit::Inch}) {
            auto back = Units::fromKey(Units::key(u));
            QVERIFY(back.has_value());
            QCOMPARE(int(*back), int(u));
        }
        QVERIFY(!Units::fromKey("furlong").has_value());
    }
    void parsePlain() {
        QCOMPARE(Units::parse("125", Unit::Millimeter).value(), 125.0);
        QCOMPARE(Units::parse("12,5", Unit::Millimeter).value(), 12.5);
        QCOMPARE(Units::parse("2", Unit::Meter).value(), 2000.0);
        QCOMPARE(Units::parse("-3.5", Unit::Centimeter).value(), -35.0);
    }
    void parseSuffixes() {
        QCOMPARE(Units::parse("80 cm", Unit::Millimeter).value(), 800.0);
        QCOMPARE(Units::parse("80см", Unit::Millimeter).value(), 800.0);
        QCOMPARE(Units::parse("1 m", Unit::Millimeter).value(), 1000.0);
        QCOMPARE(Units::parse("2'", Unit::Millimeter).value(), 609.6);
        QVERIFY(std::abs(Units::parse("1 1/2 in", Unit::Millimeter).value() - 38.1) < 1e-9);
        QVERIFY(std::abs(Units::parse("10\"", Unit::Millimeter).value() - 254.0) < 1e-9);
    }
    void parseInvalid() {
        QVERIFY(!Units::parse("", Unit::Millimeter).has_value());
        QVERIFY(!Units::parse("abc", Unit::Millimeter).has_value());
        QVERIFY(!Units::parse("12 parsecs", Unit::Millimeter).has_value());
    }
    void format() {
        QCOMPARE(Units::format(1234.5, Unit::Millimeter, 1), QStringLiteral("1234.5"));
        QCOMPARE(Units::format(1234.5, Unit::Centimeter, 2, true), QStringLiteral("123.45 см"));
        QCOMPARE(Units::format(2000, Unit::Meter, 3), QStringLiteral("2"));
        QCOMPARE(Units::format(2500, Unit::Meter, 3), QStringLiteral("2.5"));
        QCOMPARE(Units::format(25.4, Unit::Inch, 2, true), QStringLiteral("1 ″"));
        QCOMPARE(Units::formatAngle(kPi / 2, 1), QStringLiteral("90°"));
        QCOMPARE(Units::formatAngle(kPi / 4, 1), QStringLiteral("45°"));
        QCOMPARE(Units::formatAngle(0.5, 2), QStringLiteral("28.65°"));
    }
    void inputParserPoints() {
        auto p = InputParser::point("100,50", Unit::Millimeter, std::nullopt, std::nullopt);
        QVERIFY(p); QCOMPARE(p->x, 100.0); QCOMPARE(p->y, 50.0);
        p = InputParser::point("@10,-5", Unit::Millimeter, Point{100, 100}, std::nullopt);
        QVERIFY(p); QCOMPARE(p->x, 110.0); QCOMPARE(p->y, 95.0);
        p = InputParser::point("@100<90", Unit::Millimeter, Point{0, 0}, std::nullopt);
        QVERIFY(p); QVERIFY(std::abs(p->x) < 1e-9); QVERIFY(std::abs(p->y - 100) < 1e-9);
        p = InputParser::point("50", Unit::Millimeter, Point{0, 0}, Point{10, 0});
        QVERIFY(p); QVERIFY(std::abs(p->x - 50) < 1e-9); QVERIFY(std::abs(p->y) < 1e-9);
        p = InputParser::point("1 m; 2 m", Unit::Millimeter, std::nullopt, std::nullopt);
        QVERIFY(p); QCOMPARE(p->x, 1000.0); QCOMPARE(p->y, 2000.0);
        QVERIFY(!InputParser::point("foo", Unit::Millimeter, std::nullopt, std::nullopt));
        QVERIFY(!InputParser::point("50", Unit::Millimeter, std::nullopt, std::nullopt));   // no base point
    }
    void inputParserAngles() {
        QCOMPARE(InputParser::angleDeg("45").value(), 45.0);
        QCOMPARE(InputParser::angleDeg("-30,5°").value(), -30.5);
        QVERIFY(!InputParser::angleDeg("x").has_value());
    }
};

QTEST_GUILESS_MAIN(TestUnits)
#include "test_units.moc"
