#ifndef ACTIONITEM_P_H
#define ACTIONITEM_P_H

#include <QPointer>

#include "ActionItem.h"
#include "ActionSpec.h"

namespace Core {

    class ActionItemPrivate : public QObject {
        Q_DECLARE_PUBLIC(ActionItem)
    public:
        ActionItemPrivate();
        virtual ~ActionItemPrivate();

        void init();

        bool getSpec();

        ActionItem *q_ptr;

        ActionSpec *spec;

        QString id;
        ActionItem::Type type;

        bool autoDelete;

        QPointer<QAction> action;
        QPointer<QMenu> menu;
        QPointer<QWidgetAction> widgetAction;

        QString specificName;
        QPair<QString, QString> commandCheckedDesc;

    private:
        void _q_actionShortcutsChanged();
        void _q_actionIconChanged();
    };

}

#endif // ACTIONITEM_P_H
