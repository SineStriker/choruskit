#ifndef IWINDOW_H
#define IWINDOW_H

#include <CoreApi/actionitem.h>
#include <CoreApi/objectpool.h>
#include <CoreApi/windowelementsadaptor.h>

namespace Core {

    namespace Internal {
        class CorePlugin;
    }

    class IWindowPrivate;

    class CKAPPCORE_EXPORT IWindow : public ObjectPool, public WindowElementsAdaptor {
        Q_OBJECT
    public:
        explicit IWindow(const QString &id, QObject *parent = nullptr);
        ~IWindow();

        enum State {
            Invalid,
            WindowSetup,
            Initialized,
            Running,
            Exiting,
            Deleted,
        };
        Q_ENUM(State)

        void load();
        void exit();
        
        State state() const;
        inline bool isEffectivelyClosed() const;

        bool closeAsExit() const;
        void setCloseAsExit(bool on);

    public:
        QString id() const;

        void addWidget(const QString &id, QWidget *w);
        void removeWidget(const QString &id);
        QWidget *widget(const QString &id) const;
        QList<QWidget *> widgets() const;

        void addActionItem(ActionItem *item);
        void addActionItems(const QList<ActionItem *> &items);
        void removeActionItem(ActionItem *item);
        void removeActionItem(const QString &id);
        ActionItem *actionItem(const QString &id) const;
        QList<ActionItem *> actionItems() const;

        void addTopLevelMenu(const QString &id, QWidget *w);
        void removeTopLevelMenu(const QString &id);
        QWidget *topLevelMenu(const QString &id) const;
        QMap<QString, QWidget *> topLevelMenus() const;

        enum ShortcutContextPriority {
            Resident,
            Mutable,
        };
        void addShortcutContext(QWidget *w, ShortcutContextPriority priority);
        void removeShortcutContext(QWidget *w);
        QList<QWidget *> shortcutContexts() const;

        bool hasDragFileHandler(const QString &suffix);
        void setDragFileHandler(const QString &suffix, QObject *obj, const char *member, int maxCount = 0);
        void removeDragFileHandler(const QString &suffix);

    Q_SIGNALS:
        void widgetAdded(const QString &id, QWidget *w);
        void aboutToRemoveWidget(const QString &id, QWidget *w);

        void initializationDone();
        void loadingStateChanged(State state);

    protected:
        virtual QWidget *createWindow(QWidget *parent) const = 0;

        virtual void nextLoadingState(State nextState);

        virtual void topLevelMenuAdded(const QString &id, QWidget *w);
        virtual void topLevelMenuRemoved(const QString &id, QWidget *w);

        virtual void actionItemAdded(ActionItem *item);
        virtual void actionItemRemoved(ActionItem *item);

    protected:
        IWindow(IWindowPrivate &d, const QString &id, QObject *parent = nullptr);
        QScopedPointer<IWindowPrivate> d_ptr;

        Q_DECLARE_PRIVATE(IWindow)

        friend class WindowSystem;
        friend class WindowSystemPrivate;
        friend class ICore;
        friend class ICorePrivate;
        friend class Internal::CorePlugin;

    public:
        template <class T>
        inline T *cast();

        template <class T>
        inline const T *cast() const;

        template <class T, class... Args>
        static inline T *create(Args &&...args);
    };

    inline bool IWindow::isEffectivelyClosed() const {
        return state() >= Exiting;
    }

    template <class T>
    inline T *IWindow::cast() {
        static_assert(std::is_base_of<IWindow, T>::value, "T should inherit from Core::IWindow");
        return qobject_cast<T *>(this);
    }

    template <class T>
    inline const T *IWindow::cast() const {
        static_assert(std::is_base_of<IWindow, T>::value, "T should inherit from Core::IWindow");
        return qobject_cast<T *>(this);
    }

    template <class T, class... Args>
    inline T *IWindow::create(Args &&...arguments) {
        static_assert(std::is_base_of<IWindow, T>::value, "T should inherit from Core::IWindow");
        auto p = new T(arguments...);
        p->load();
        return p;
    }

}

#endif // IWINDOW_H
