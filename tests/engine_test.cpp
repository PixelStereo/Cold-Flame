#include "engine.h"
#include "gdtf.h"
#include "network.h"
#include <QtTest>
#include <zlib.h>
using namespace Flame;
class Tests : public QObject {
    Q_OBJECT
  private slots:
    void command() {
        Engine e;
        QString err;
        QVERIFY(e.command("Channel 1 Thru 4 At 50", &err));
        QCOMPARE(e.selection.size(), 4);
        QCOMPARE(e.output()[4], 50.);
        auto old = e.manual;
        QVERIFY(!e.command("1 thru 99999 at 50", &err));
        QCOMPARE(e.manual.size(), old.size());
        QVERIFY(!e.command("1 at 101"));
    }
    void tracking() {
        Engine e;
        e.selection = {1};
        e.level(30);
        e.record(0, "1", "Start", 0);
        e.manual.clear();
        e.selection = {2};
        e.level(70);
        e.record(0, "2", "Next", 0);
        QCOMPARE(e.tracked(0, 1)[1], 30.);
        e.manual.clear();
        e.go(0);
        QCOMPARE(e.output()[1], 30.);
        e.go(0);
        QCOMPARE(e.output()[2], 70.);
    }
    void paletteReferences() {
        Engine e;
        e.selection = {1};
        e.level(25);
        e.recordPalette(1);
        e.applyPalette(1);
        e.record(0, "1", "", 0);
        e.level(75);
        e.recordPalette(1);
        QCOMPARE(e.tracked(0, 0)[1], 75.);
    }
    void cueOnly() {
        Engine e;
        e.selection = {1};
        e.level(20);
        e.record(0, "1", "", 0);
        e.manual.clear();
        e.record(0, "2", "", 0);
        e.level(80);
        e.record(0, "1", "", 0, true);
        QCOMPARE(e.tracked(0, 0)[1], 80.);
        QCOMPARE(e.tracked(0, 1)[1], 20.);
    }
    void fadePause() {
        Engine e;
        e.selection = {1};
        e.level(100);
        e.record(0, "1", "", 2);
        e.manual.clear();
        e.go(0);
        e.tick(1);
        QCOMPARE(e.output()[1], 50.);
        e.sequences[0].paused = true;
        e.tick(1);
        QCOMPARE(e.output()[1], 50.);
        e.sequences[0].paused = false;
        e.tick(1);
        QCOMPARE(e.output()[1], 100.);
    }
    void prioritiesAndRelease() {
        Engine e;
        e.selection = {1};
        e.level(20);
        e.record(0, "1", "", 0);
        Sequence s;
        s.name = "Overlay";
        s.priority = 8;
        e.sequences << s;
        e.level(80);
        e.record(1, "1", "", 0);
        e.manual.clear();
        e.go(1);
        e.go(0);
        QCOMPARE(e.output()[1], 80.);
        e.release(1);
        QCOMPARE(e.output()[1], 20.);
        e.blackout = true;
        QCOMPARE(e.output()[1], 0.);
    }
    void persistence() {
        Engine e;
        e.selection = {1};
        e.level(35);
        e.recordPalette(1);
        e.applyPalette(1);
        e.record(0, "1", "test", 2);
        Engine copy;
        QVERIFY(copy.load(e.json()));
        QCOMPARE(copy.json(), e.json());
        auto j = e.json();
        j["version"] = 999;
        QVERIFY(!copy.load(j));
        QCOMPARE(copy.json(), e.json());
    }
    void packets() {
        QByteArray d(512, '\0');
        d[0] = char(255);
        auto a = artnet(d, 0, 1);
        QCOMPARE(a.size(), 530);
        QCOMPARE(a.left(8), QByteArray("Art-Net\0", 8));
        QCOMPARE(quint8(a[9]), 0x50);
        QCOMPARE(quint8(a[16]), 2);
        QCOMPARE(quint8(a[18]), 255);
        auto id = QUuid::createUuid();
        auto s = sacn(d, 1, 5, id);
        QCOMPARE(s.size(), 638);
        QCOMPARE(s.mid(22, 16), id.toRfc4122());
        QCOMPARE(quint8(s[111]), 5);
        QCOMPARE(quint8(s[114]), 1);
        QCOMPARE(quint8(s[126]), 255);
        QVERIFY(sacn(d, 0, 1, id).isEmpty());
    }
    void udpLoopback() {
        QUdpSocket receiver;
        QVERIFY(receiver.bind(QHostAddress::LocalHost, 6454));
        Engine e;
        e.selection = {1};
        e.level(100);
        Network n;
        n.enabled = true;
        n.firstUniverse = 0;
        n.send(e);
        QVERIFY(receiver.waitForReadyRead(1000));
        auto p = receiver.receiveDatagram().data();
        QCOMPARE(p.size(), 530);
        QCOMPARE(quint8(p[18]), 255);
        e.blackout = true;
        n.send(e);
        QVERIFY(receiver.waitForReadyRead(1000));
        QCOMPARE(quint8(receiver.receiveDatagram().data()[18]), 0);
    }
    void gdtfArchive() {
        QByteArray xml =
            "<GDTF><FixtureType Name=\"Wash\" Manufacturer=\"Test\"><DMXModes><DMXMode "
            "Name=\"Basic\"><DMXChannels><DMXChannel Offset=\"1,2\"><LogicalChannel "
            "Attribute=\"Pan\"/></DMXChannel></DMXChannels></DMXMode></DMXModes></FixtureType></"
            "GDTF>";
        QByteArray name = "description.xml", b(30, '\0');
        auto put16 = [](QByteArray &a, int p, quint16 v) {
            a[p] = char(v);
            a[p + 1] = char(v >> 8);
        };
        auto put32 = [&](QByteArray &a, int p, quint32 v) {
            put16(a, p, v);
            put16(a, p + 2, v >> 16);
        };
        quint32 crc = crc32(0, reinterpret_cast<const Bytef *>(xml.constData()), xml.size());
        b.replace(0, 4, QByteArray("PK\3\4", 4));
        put16(b, 26, name.size());
        put32(b, 14, crc);
        put32(b, 18, xml.size());
        put32(b, 22, xml.size());
        b += name + xml;
        int central = b.size();
        QByteArray c(46, '\0');
        c.replace(0, 4, QByteArray("PK\1\2", 4));
        put32(c, 16, crc);
        put32(c, 20, xml.size());
        put32(c, 24, xml.size());
        put16(c, 28, name.size());
        b += c + name;
        QByteArray end(22, '\0');
        end.replace(0, 4, QByteArray("PK\5\6", 4));
        put16(end, 8, 1);
        put16(end, 10, 1);
        put32(end, 12, c.size() + name.size());
        put32(end, 16, central);
        b += end;
        FixtureProfile profile;
        QString error;
        QVERIFY2(readGdtf(b, profile, error), qPrintable(error));
        QCOMPARE(profile.name, QString("Wash"));
        QCOMPARE(profile.modes[0].footprint, 2);
        QCOMPARE(profile.modes[0].attributes, QStringList{"Pan"});
        b[30 + name.size()] = '!';
        QVERIFY(!readGdtf(b, profile, error));
        QVERIFY(!readGdtf(QByteArray("broken"), profile, error));
    }
    void groupAndPaletteCommands() {
        Engine e;
        e.groups[1] = {{2, 0}, {4, 0}};
        QVERIFY(e.command("group 1"));
        QCOMPARE(e.selection, QSet<int>({2, 4}));
        e.level(60);
        e.recordPalette(2);
        e.level(0);
        QVERIFY(e.command("intensity 2"));
        QCOMPARE(e.output()[2], 60.);
    }
    void pixelLayer() {
        Engine e;
        e.pixels[1] = 80;
        QCOMPARE(e.output()[1], 80.);
        e.master = .5;
        QCOMPARE(e.output()[1], 40.);
        e.selection = {1};
        e.level(20);
        QCOMPARE(e.output()[1], 10.);
    }
    void osc() {
        QString a;
        float v;
        auto b = oscMessage("/coldflame/master", 0.5);
        QVERIFY(oscDecode(b, a, v));
        QCOMPARE(v, 0.5f);
        QCOMPARE(a, QString("/coldflame/master"));
        QVERIFY(!oscDecode(b.left(b.size() - 1), a, v));
    }
};
QTEST_GUILESS_MAIN(Tests)
#include "engine_test.moc"
