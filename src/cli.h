#pragma once

#include <QStringList>

bool isCliCommand(const QString &word);
int runCli(const QStringList &arguments);
