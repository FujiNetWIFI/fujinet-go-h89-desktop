/*
 * The display loads :/shaders/display.{vert,frag}.qsb. If qt_add_shaders is
 * given the wrong PREFIX the files land elsewhere, the pipeline gets empty
 * shaders and the window silently falls back to software; this fails
 * instead.
 *
 * Copyright (C) 2026 Thomas Cherryhomes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <QFile>
#include <QtTest>
#include <rhi/qshader.h>

class DisplayShadersTest : public QObject {
    Q_OBJECT
private slots:
    void shadersAreWhereTheDisplayLoadsThem_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("vert") << QStringLiteral(":/shaders/display.vert.qsb");
        QTest::newRow("frag") << QStringLiteral(":/shaders/display.frag.qsb");
    }
    void shadersAreWhereTheDisplayLoadsThem()
    {
        QFETCH(QString, path);
        QFile f(path);
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(path + " is not in the resources"));
        const QShader s = QShader::fromSerialized(f.readAll());
        QVERIFY2(s.isValid(), qPrintable(path + " is not a valid shader"));
        QVERIFY(!s.availableShaders().isEmpty());
    }
};

QTEST_MAIN(DisplayShadersTest)
#include "display_shaders_test.moc"
