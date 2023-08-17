#include "IWindow.h"
#include "IWindow_p.h"

#include "ICoreBase.h"
#include "IWindowAddOn_p.h"
#include "ShortcutContext_p.h"
#include "WindowCloseFilter_p.h"
#include "WindowSystem_p.h"

#include <QDebug>
#include <QEvent>

#include <private/qwidget_p.h>

static const int DELAYED_INITIALIZE_INTERVAL = 5; // ms

namespace Core {

#define myWarning(func) (qWarning().nospace() << "Core::IWindow::" << (func) << "():").space()

    class QWidgetHacker : public QWidget {
    public:
        int actionCount() const {
            auto d = static_cast<QWidgetPrivate *>(d_ptr.data());
            return d->actions.count();
        }

        friend class IWindow;
        friend class IWindowPrivate;
    };

    IWindowPrivate::IWindowPrivate() {
    }

    IWindowPrivate::~IWindowPrivate() {
        tryStopDelayedTimer();
    }

    void IWindowPrivate::init() {
    }

    void IWindowPrivate::changeLoadState(IWindow::State state) {
        Q_Q(IWindow);
        q->nextLoadingState(state);
        this->state = state;
        emit q->loadingStateChanged(state);
    }

    void IWindowPrivate::setWindow(QWidget *w, WindowSystemPrivate *d) {
        Q_Q(IWindow);

        q->setWindow(w);

        shortcutCtx = new QMShortcutContext(this);

        closeFilter = new WindowCloseFilter(this, q->window());
        connect(closeFilter, &WindowCloseFilter::windowClosed, this, &IWindowPrivate::_q_windowClosed);

        // Setup window
        changeLoadState(IWindow::WindowSetup);

        // Call all add-ons
        auto facs = d->addOnFactories.value(id);
        for (auto it = facs.begin(); it != facs.end(); ++it) {
            const auto &mo = it.key();
            const auto &fac = it.value();

            IWindowAddOn *addOn;
            if (fac) {
                addOn = fac();
            } else {
                addOn = qobject_cast<IWindowAddOn *>(mo->newInstance());
            }

            if (!addOn) {
                myWarning(__func__) << "window add-on factory creates null instance:" << mo->className();
                continue;
            }

            addOn->d_ptr->iWin = q;
            addOns.push_back(addOn);
        }

        // Initialize
        for (auto &addOn : qAsConst(addOns)) {
            // Call 1
            addOn->initialize();
        }

        changeLoadState(IWindow::Initialized);

        // ExtensionsInitialized
        for (auto it2 = addOns.rbegin(); it2 != addOns.rend(); ++it2) {
            auto &addOn = *it2;
            // Call 2
            addOn->extensionsInitialized();
        }

        // Add-ons finished
        changeLoadState(IWindow::Running);

        // Delayed initialize
        delayedInitializeQueue = addOns;

        delayedInitializeTimer = new QTimer();
        delayedInitializeTimer->setInterval(DELAYED_INITIALIZE_INTERVAL);
        delayedInitializeTimer->setSingleShot(true);
        connect(delayedInitializeTimer, &QTimer::timeout, this, &IWindowPrivate::nextDelayedInitialize);
        delayedInitializeTimer->start();
    }

    void IWindowPrivate::deleteAllAddOns() {
        for (auto it2 = addOns.rbegin(); it2 != addOns.rend(); ++it2) {
            auto &addOn = *it2;
            // Call 1
            delete addOn;
        }
    }

    void IWindowPrivate::tryStopDelayedTimer() {
        // Stop delayed initializations
        if (delayedInitializeTimer) {
            if (delayedInitializeTimer->isActive()) {
                delayedInitializeTimer->stop();
            }
            delete delayedInitializeTimer;
            delayedInitializeTimer = nullptr;
        }
    }

    void IWindowPrivate::nextDelayedInitialize() {
        Q_Q(IWindow);

        while (!delayedInitializeQueue.empty()) {
            auto addOn = delayedInitializeQueue.front();
            delayedInitializeQueue.pop_front();

            bool delay = addOn->delayedInitialize();
            if (delay)
                break; // do next delayedInitialize after a delay
        }
        if (delayedInitializeQueue.empty()) {
            delete delayedInitializeTimer;
            delayedInitializeTimer = nullptr;
            emit q->initializationDone();
        } else {
            delayedInitializeTimer->start();
        }
    }

    void IWindowPrivate::_q_windowClosed(QWidget *w) {
        Q_Q(IWindow);

        Q_UNUSED(w);

        tryStopDelayedTimer();

        if (!w->isHidden())
            w->hide();

        changeLoadState(IWindow::Closed);

        ICoreBase::instance()->windowSystem()->d_func()->windowClosed(q);

        delete shortcutCtx;
        shortcutCtx = nullptr;

        // Delete addOns
        deleteAllAddOns();

        changeLoadState(IWindow::Deleted);

        q->setWindow(nullptr);
        delete q;
    }

    void IWindow::load() {
        auto winMgr = ICoreBase::instance()->windowSystem();
        auto d = winMgr->d_func();
        d->iWindows.append(this);

        // Get quit control
        qApp->setQuitOnLastWindowClosed(false);

        // Create window
        auto win = createWindow(nullptr);

        // Add to indexes
        d->windowMap.insert(win, this);

        win->setAttribute(Qt::WA_DeleteOnClose);
        connect(qApp, &QApplication::aboutToQuit, win,
                &QWidget::close); // Ensure closing window when quit

        d_func()->setWindow(win, d);

        emit winMgr->windowCreated(this);

        win->show();
    }

    IWindow::State IWindow::state() const {
        Q_D(const IWindow);
        return d->state;
    }

    QString IWindow::id() const {
        Q_D(const IWindow);
        return d->id;
    }

    void IWindow::addWidget(const QString &id, QWidget *w) {
        Q_D(IWindow);
        if (!w) {
            myWarning(__func__) << "trying to add null widget";
            return;
        }
        if (d->actionItemMap.contains(id)) {
            myWarning(__func__) << "trying to add duplicated widget:" << id;
            return;
        }
        d->widgetMap.insert(id, w);
        emit widgetAdded(id, w);
    }

    void IWindow::removeWidget(const QString &id) {
        Q_D(IWindow);
        auto it = d->widgetMap.find(id);
        if (it == d->widgetMap.end()) {
            myWarning(__func__) << "action item does not exist:" << id;
            return;
        }
        auto w = it.value();
        emit aboutToRemoveWidget(id, w);
        d->widgetMap.erase(it);
    }

    QWidget *IWindow::widget(const QString &id) const {
        Q_D(const IWindow);
        auto it = d->widgetMap.find(id);
        if (it != d->widgetMap.end()) {
            return it.value();
        }
        return nullptr;
    }

    QWidgetList IWindow::widgets() const {
        Q_D(const IWindow);
        return d->widgetMap.values();
    }

    void IWindow::addActionItem(ActionItem *item) {
        Q_D(IWindow);
        if (!item) {
            myWarning(__func__) << "trying to add null action item";
            return;
        }

        if (!item->spec()) {
            myWarning(__func__) << "trying to add unidentified item" << item->id();
            return;
        }

        if (d->actionItemMap.contains(item->id())) {
            myWarning(__func__) << "trying to add duplicated action item:" << item->id();
            return;
        }
        d->actionItemMap.append(item->id(), item);

        actionItemAdded(item);
    }

    void IWindow::addActionItems(const QList<Core::ActionItem *> &items) {
        for (const auto &item : items) {
            addActionItem(item);
        }
    }

    void IWindow::removeActionItem(Core::ActionItem *item) {
        if (item == nullptr) {
            myWarning(__func__) << "trying to remove null item";
            return;
        }
        removeActionItem(item->id());
    }

    void IWindow::removeActionItem(const QString &id) {
        Q_D(IWindow);
        auto it = d->actionItemMap.find(id);
        if (it == d->actionItemMap.end()) {
            myWarning(__func__) << "action item does not exist:" << id;
            return;
        }
        auto item = it.value();
        d->actionItemMap.erase(it);

        actionItemRemoved(item);
    }

    ActionItem *IWindow::actionItem(const QString &id) const {
        Q_D(const IWindow);
        return d->actionItemMap.value(id, nullptr);
    }

    QList<ActionItem *> IWindow::actionItems() const {
        Q_D(const IWindow);
        return d->actionItemMap.values();
    }

    void IWindow::addTopLevelMenu(const QString &id, QWidget *w) {
        Q_D(IWindow);
        if (!w) {
            myWarning(__func__) << "trying to add null widget";
            return;
        }

        if (d->topLevelMenuMap.contains(id)) {
            myWarning(__func__) << "trying to add duplicated widget:" << id;
            return;
        }
        d->topLevelMenuMap.insert(id, w);
        topLevelMenuAdded(id, w);
    }

    void IWindow::removeTopLevelMenu(const QString &id) {
        Q_D(IWindow);
        auto it = d->topLevelMenuMap.find(id);
        if (it == d->topLevelMenuMap.end()) {
            myWarning(__func__) << "widget does not exist:" << id;
            return;
        }
        auto w = it.value();
        d->topLevelMenuMap.erase(it);

        topLevelMenuAdded(id, w);
    }

    QWidget *IWindow::topLevelMenu(const QString &id) const {
        Q_D(const IWindow);
        return d->topLevelMenuMap.value(id, nullptr);
    }

    QMap<QString, QWidget *> IWindow::topLevelMenus() const {
        Q_D(const IWindow);
        return d->topLevelMenuMap;
    }

    void IWindow::addShortcutContext(QWidget *w, ShortcutContextPriority priority) {
        Q_D(IWindow);
        d->shortcutCtx->addWidget(w, priority);
    }

    void IWindow::removeShortcutContext(QWidget *w) {
        Q_D(IWindow);
        d->shortcutCtx->removeWidget(w);
    }

    QList<QWidget *> IWindow::shortcutContexts() const {
        Q_D(const IWindow);
        return d->shortcutCtx->widgets();
    }

    bool IWindow::hasDragFileHandler(const QString &suffix) {
        Q_D(const IWindow);
        if (suffix.isEmpty())
            return false;

        return d->dragFileHandlerMap.contains(suffix.toLower());
    }

    void IWindow::setDragFileHandler(const QString &suffix, QObject *obj, const char *member, int maxCount) {
        Q_D(IWindow);

        if (suffix.isEmpty())
            return;

        if (!obj || maxCount < 0) {
            removeDragFileHandler(suffix);
            return;
        }
        d->dragFileHandlerMap[suffix.toLower()] = {obj, member, maxCount};
    }

    void IWindow::removeDragFileHandler(const QString &suffix) {
        Q_D(IWindow);
        if (suffix.isEmpty())
            return;

        d->dragFileHandlerMap.remove(suffix.toLower());
    }

    IWindow::IWindow(const QString &id, QObject *parent) : IWindow(*new IWindowPrivate(), id, parent) {
    }

    IWindow::~IWindow() {
    }

    void IWindow::nextLoadingState(Core::IWindow::State nextState) {
        Q_UNUSED(nextState)
    }

    void IWindow::topLevelMenuAdded(const QString &id, QWidget *w) {
        // Do nothing
    }

    void IWindow::topLevelMenuRemoved(const QString &id, QWidget *w) {
        // Do nothing
    }

    void IWindow::actionItemAdded(ActionItem *item) {
        // Do nothing
    }

    void IWindow::actionItemRemoved(ActionItem *item) {
        // Do nothing
    }

    IWindow::IWindow(IWindowPrivate &d, const QString &id, QObject *parent) : ObjectPool(parent), d_ptr(&d) {
        d.q_ptr = this;
        d.id = id;

        d.init();
    }

} // namespace Core
