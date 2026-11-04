#pragma once
#include <QByteArray>
QByteArray DmgFixture(const QByteArray &plain, unsigned keyBits = 128, bool aesWrap = false,
                      bool version1 = false);
