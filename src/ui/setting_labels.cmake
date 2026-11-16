set(context "HikariSub.Options")
string(ASCII 59 semicolon)
string(REPLACE "|" ";" pages "${PAGES}")
set(found "")
set(rows "")
foreach(page IN LISTS pages)
    file(READ "${page}" text)
    # CMake lists split on ; and on unbalanced brackets.
    string(REPLACE ";" "@semi@" text "${text}")
    string(REPLACE "[" "@open@" text "${text}")
    string(REPLACE "]" "@close@" text "${text}")
    string(REGEX MATCHALL "docsLabel: qsTr\\(\"[^\"]+\"\\)[^{}]*setting: \"[^\"]+\"" documented "${text}")
    string(REGEX MATCHALL "setting: \"[^\"]+\"[^{}]*(text|name|label): qsTr\\(\"[^\"]+\"\\)" own "${text}")
    string(REGEX MATCHALL "text: qsTr\\(\"[^\"]+\"\\)[^{}]*{[^{}]*setting: \"[^\"]+\"" inside "${text}")
    foreach(match IN LISTS documented)
        string(REGEX MATCH "^docsLabel: qsTr\\(\"([^\"]+)\"\\).*setting: \"([^\"]+)\"$" _ "${match}")
        list(APPEND rows "${CMAKE_MATCH_2}=${CMAKE_MATCH_1}")
    endforeach()
    foreach(match IN LISTS own)
        string(REGEX MATCH "^setting: \"([^\"]+)\".*qsTr\\(\"([^\"]+)\"\\)$" _ "${match}")
        list(APPEND rows "${CMAKE_MATCH_1}=${CMAKE_MATCH_2}")
    endforeach()
    foreach(match IN LISTS inside)
        string(REGEX MATCH "^text: qsTr\\(\"([^\"]+)\"\\).*setting: \"([^\"]+)\"$" _ "${match}")
        list(APPEND rows "${CMAKE_MATCH_2}=${CMAKE_MATCH_1}")
    endforeach()
endforeach()

set(entries "")
foreach(row IN LISTS rows)
    string(FIND "${row}" "=" at)
    string(SUBSTRING "${row}" 0 ${at} setting)
    math(EXPR at "${at} + 1")
    string(SUBSTRING "${row}" ${at} -1 label)
    if(setting IN_LIST found OR label MATCHES "\\\\$")
        continue()
    endif()
    list(APPEND found "${setting}")
    string(REPLACE "@semi@" "${semicolon}" label "${label}")
    string(REPLACE "@open@" "[" label "${label}")
    string(REPLACE "@close@" "]" label "${label}")
    string(APPEND entries "    {\"${setting}\", QT_TRANSLATE_NOOP(\"${context}\", \"${label}\")},\n")
endforeach()

file(CONFIGURE OUTPUT "${OUT}.new" @ONLY CONTENT [=[#include "setting_labels.h"

#include <QCoreApplication>

namespace hikari::ui {
namespace {

struct Label {
    const char *setting;
    const char *text;
};

constexpr Label kLabels[] = {
@entries@};

}

QString SettingLabels::of(const QString &setting)
{
    for (const Label &label : kLabels)
        if (setting == QLatin1String(label.setting))
            return QCoreApplication::translate("@context@", label.text);
    return {};
}

QStringList SettingLabels::labelled()
{
    QStringList out;
    for (const Label &label : kLabels)
        out << QLatin1String(label.setting);
    return out;
}

}
]=])
file(COPY_FILE "${OUT}.new" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.new")
