#ifndef CC_DATABASE_DEFINE_H
#define CC_DATABASE_DEFINE_H

#include <QtCore/qglobal.h>

#if defined(UI_DATABASE_LIBRARY)
#  define UI_DATABASE_EXPORT Q_DECL_EXPORT
#else
#  define UI_DATABASE_EXPORT Q_DECL_IMPORT
#endif

#endif // CC_DATABASE_DEFINE_H
