#pragma once

// A QtTest executable run as several parts, so that each process stays well
// inside its CTest timeout: `-part FROM..BEFORE` runs the test functions from
// FROM up to, not including, BEFORE, in declaration order. Either end may be
// empty (the first function, past the last one). A function added anywhere
// falls into the part around it, so parts cut at the same seams run every
// function exactly once. With -functions, only the part's functions are
// listed (hikari_add_qttest's PARTS check uses this).
//
// HIKARI_QTEST_MAIN_IN_PARTS(TestObject) replaces QTEST_MAIN(TestObject);
// without -part the executable behaves as before.

#include <QByteArray>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QtTest/QtTest>

#include <cstdio>

namespace hikari::testing {

// The test functions as QtTest picks them (qtestcase.cpp isValidSlot): private
// slots without parameters returning void, other than the four special ones
// and the _data functions, in declaration order.
inline QStringList testFunctions(const QMetaObject &meta)
{
    QStringList out;
    for (int i = 0; i < meta.methodCount(); ++i) {
        const QMetaMethod method = meta.method(i);
        if (method.access() != QMetaMethod::Private || method.methodType() != QMetaMethod::Slot
            || method.parameterCount() != 0 || method.returnType() != QMetaType::Void)
            continue;
        const QByteArray name = method.name();
        if (name.isEmpty() || name.endsWith("_data") || name == "initTestCase" || name == "cleanupTestCase"
            || name == "init" || name == "cleanup")
            continue;
        out << QString::fromLatin1(name);
    }
    return out;
}

// The functions of `spec` ("FROM..BEFORE") among `functions`, or an error.
struct PartSelection {
    QStringList functions;
    QString error;
};

inline PartSelection selectPart(const QStringList &functions, const QString &spec)
{
    const qsizetype dots = spec.indexOf(QStringLiteral(".."));
    if (dots < 0)
        return {{}, QStringLiteral("-part wants FROM..BEFORE, got \"%1\"").arg(spec)};
    const QString from = spec.left(dots), before = spec.mid(dots + 2);
    const qsizetype first = from.isEmpty() ? 0 : functions.indexOf(from);
    const qsizetype end = before.isEmpty() ? functions.size() : functions.indexOf(before);
    if (first < 0)
        return {{}, QStringLiteral("-part: no test function %1").arg(from)};
    if (end < 0)
        return {{}, QStringLiteral("-part: no test function %1").arg(before)};
    if (first >= end)
        return {{}, QStringLiteral("-part: %1 does not come before %2").arg(from, before)};
    return {functions.mid(first, end - first), {}};
}

// The command line after QApplication took its own options, with -part
// replaced by its functions. done(): the part was listed or refused, and
// main returns exitCode().
class QtTestPart
{
public:
    QtTestPart(const QMetaObject &meta, int argc, char **argv)
    {
        for (int i = 0; i < argc; ++i)
            args << QString::fromLocal8Bit(argv[i]);
        const qsizetype at = args.indexOf(QStringLiteral("-part"));
        if (at < 0)
            return;
        if (at + 1 >= args.size()) {
            fail(QStringLiteral("-part wants FROM..BEFORE"));
            return;
        }
        const PartSelection part = selectPart(testFunctions(meta), args.at(at + 1));
        if (!part.error.isEmpty()) {
            fail(part.error);
            return;
        }
        args.remove(at, 2);
        if (args.contains(QStringLiteral("-functions"))) {
            for (const QString &function : part.functions)
                std::printf("%s()\n", qPrintable(function));
            std::fflush(stdout);
            code = 0;
            return;
        }
        args << part.functions;
    }

    bool done() const { return code >= 0; }
    int exitCode() const { return code; }
    const QStringList &arguments() const { return args; }

private:
    void fail(const QString &error)
    {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        code = 2;
    }

    QStringList args;
    int code = -1;
};

} // namespace hikari::testing

// QTEST_MAIN with -part (see above): the application object first, as
// QTEST_MAIN makes it, so the command line is read after Qt's own options.
#define HIKARI_QTEST_MAIN_IN_PARTS(TestObject)                                                     \
    int main(int argc, char *argv[])                                                               \
    {                                                                                              \
        QTEST_MAIN_SETUP()                                                                         \
        const hikari::testing::QtTestPart part(TestObject::staticMetaObject, argc, argv);         \
        if (part.done())                                                                           \
            return part.exitCode();                                                                \
        TestObject tc;                                                                             \
        QTEST_SET_MAIN_SOURCE_PATH                                                                 \
        return QTest::qExec(&tc, part.arguments());                                                \
    }
