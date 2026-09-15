#include "filter_browser_panel.h"
#include "tokenize.h"
#include <algorithm>
#include <QSettings>
#include <QTabWidget>
#include <QTabBar>
#include <QMenu>
#include <QInputDialog>
#include <QColorDialog>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>

CollectionTabBar::CollectionTabBar(QWidget *parent) : QTabBar(parent) {
    setAcceptDrops(true);
}

void CollectionTabBar::dragEnterEvent(QDragEnterEvent *event) {
    if(event->mimeData()->hasText())
        event->acceptProposedAction();
}

void CollectionTabBar::dragMoveEvent(QDragMoveEvent *event) {
    if(!event->mimeData()->hasText()) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint pos = event->position().toPoint();
#else
    const QPoint pos = event->pos();
#endif
    const int index = tabAt(pos);
    // Only collection tabs can receive a filter - "Tous" holds everything
    // already and "Favoris" has the heart for that.
    const int wanted = (index >= 2) ? index : -1;
    if(wanted != highlight_tab) {
        highlight_tab = wanted;
        update();
    }
    if(wanted >= 0)
        event->acceptProposedAction();
    else
        event->ignore();
}

void CollectionTabBar::dragLeaveEvent(QDragLeaveEvent *event) {
    Q_UNUSED(event);
    highlight_tab = -1;
    update();
}

void CollectionTabBar::dropEvent(QDropEvent *event) {
    highlight_tab = -1;
    update();
    if(!event->mimeData()->hasText()) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint pos = event->position().toPoint();
#else
    const QPoint pos = event->pos();
#endif
    const int index = tabAt(pos);
    if(index < 2) return;
    emit filterDroppedOnTab(index, event->mimeData()->text());
    event->acceptProposedAction();
}

void CollectionTabBar::paintEvent(QPaintEvent *event) {
    QTabBar::paintEvent(event);
    if(highlight_tab < 0) return;
    // Outline the tab the filter would land in.
    QPainter painter(this);
    painter.setPen(QPen(QColor(90, 200, 255), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(tabRect(highlight_tab).adjusted(1, 1, -2, -2));
}

CollectionTabWidget::CollectionTabWidget(QWidget *parent) : QTabWidget(parent) {
    bar = new CollectionTabBar(this);
    setTabBar(bar);
}

FilterBrowserPanel::FilterBrowserPanel(QWidget *parent) : QWidget(parent) {
    QSettings settings("LostSideDead", "Acid Cam Qt");
    for(const QString &name : settings.value("LabFavorites").toStringList())
        favorites.insert(name);
    collections = loadFilterCollections();
    collection_colors = loadFilterCollectionColors();
    filter_renames = loadFilterRenames();

    createControls();
    syncCollectionTabs();
    refreshAllPanels();
}

void FilterBrowserPanel::reload() {
    QSettings settings("LostSideDead", "Acid Cam Qt");
    favorites.clear();
    for(const QString &name : settings.value("LabFavorites").toStringList())
        favorites.insert(name);
    collections = loadFilterCollections();
    collection_colors = loadFilterCollectionColors();
    filter_renames = loadFilterRenames();
    syncCollectionTabs();
    refreshAllPanels();
}

void FilterBrowserPanel::saveFavorites() {
    QSettings settings("LostSideDead", "Acid Cam Qt");
    settings.setValue("LabFavorites", QStringList(favorites.values()));
}

void FilterBrowserPanel::connectPanel(FilterListWidget *panel) {
    panel->setIconSize(QSize(18, 18));
    panel->setToolTip(tr("Clique pour essayer, glisse pour l'appliquer, clic droit pour trier."));
    panel->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(panel, SIGNAL(heartClicked(QListWidgetItem *)), this, SLOT(toggleFavoriteFromAny(QListWidgetItem *)));
    connect(panel, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(onItemClicked(QListWidgetItem *)));
    connect(panel, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(showFilterContextMenu(const QPoint &)));
}

void FilterBrowserPanel::createControls() {
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    CollectionTabWidget *tabs = new CollectionTabWidget(this);
    filter_tabs = tabs;
    CollectionTabBar *tab_bar = tabs->collectionTabBar();
    tab_bar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tab_bar, SIGNAL(customContextMenuRequested(const QPoint &)),
            this, SLOT(showTabContextMenu(const QPoint &)));
    connect(tab_bar, SIGNAL(filterDroppedOnTab(int, QString)),
            this, SLOT(onFilterDroppedOnTab(int, QString)));

    QWidget *all_tab = new QWidget(this);
    QVBoxLayout *all_layout = new QVBoxLayout(all_tab);
    all_layout->setContentsMargins(0, 4, 0, 0);
    all_filters_search = new QLineEdit(this);
    all_filters_search->setPlaceholderText(tr("Rechercher un filtre..."));
    connect(all_filters_search, SIGNAL(textChanged(const QString &)), this, SLOT(searchChanged(const QString &)));
    all_layout->addWidget(all_filters_search);
    all_filters_panel = new FilterListWidget(this);
    all_filters_panel->setSourceColor(filterNeutralColor());
    connectPanel(all_filters_panel);
    all_layout->addWidget(all_filters_panel);
    filter_tabs->addTab(all_tab, tr("Tous"));

    QWidget *fav_tab = new QWidget(this);
    QVBoxLayout *fav_layout = new QVBoxLayout(fav_tab);
    fav_layout->setContentsMargins(0, 4, 0, 0);
    favorites_panel = new FilterListWidget(this);
    favorites_panel->setSourceColor(filterFavoriteColor());
    connectPanel(favorites_panel);
    fav_layout->addWidget(favorites_panel);
    filter_tabs->addTab(fav_tab, tr("Favoris"));

    root->addWidget(filter_tabs);
}

QString FilterBrowserPanel::realFilterName(QListWidgetItem *item) const {
    if(!item) return QString();
    const QString real = item->data(Qt::UserRole).toString();
    return real.isEmpty() ? item->text() : real;
}

QString FilterBrowserPanel::displayName(const QString &realName) const {
    const QString renamed = filter_renames.value(realName);
    return renamed.isEmpty() ? realName : renamed;
}

QString FilterBrowserPanel::collectionForPanel(QWidget *panel) const {
    for(auto it = collection_panels.begin(); it != collection_panels.end(); ++it)
        if(it.value() == panel)
            return it.key();
    return QString();
}

QColor FilterBrowserPanel::collectionColor(const QString &name) const {
    const QColor stored(collection_colors.value(name));
    if(stored.isValid())
        return stored;
    // No colour recorded (older collection): a stable one derived from its
    // position, so at least it stays the same between launches.
    QStringList names = collections.keys();
    names.sort(Qt::CaseInsensitive);
    return defaultCollectionColor(names.indexOf(name));
}

void FilterBrowserPanel::applyCollectionTabColors() {
    for(int i = 2; i < filter_tabs->count(); ++i) {
        const QString name = filter_tabs->tabText(i);
        const QColor color = collectionColor(name);
        filter_tabs->setTabIcon(i, collectionDotIcon(color));
        filter_tabs->tabBar()->setTabTextColor(i, color);
        // Keep the drag colour in step, so recolouring a collection also
        // recolours what gets dropped from it afterwards.
        if(FilterListWidget *panel = collection_panels.value(name))
            panel->setSourceColor(color);
    }
}

void FilterBrowserPanel::fillPanel(FilterListWidget *panel, const QStringList &realNames) {
    panel->clear();
    // Sorted on what the user actually reads, so a renamed filter lands
    // where its new name says it should.
    QStringList sorted = realNames;
    std::sort(sorted.begin(), sorted.end(), [this](const QString &a, const QString &b) {
        return displayName(a).compare(displayName(b), Qt::CaseInsensitive) < 0;
    });
    for(const QString &real : sorted) {
        QListWidgetItem *item = new QListWidgetItem(heartIcon(favorites.contains(real)), displayName(real));
        item->setData(Qt::UserRole, real);
        panel->addItem(item);
    }
}

void FilterBrowserPanel::syncCollectionTabs() {
    // Tabs 0/1 are "Tous" and "Favoris" and stay put; everything after is
    // rebuilt from the current collections.
    while(filter_tabs->count() > 2) {
        QWidget *w = filter_tabs->widget(2);
        filter_tabs->removeTab(2);
        delete w;
    }
    collection_panels.clear();

    QStringList names = collections.keys();
    names.sort(Qt::CaseInsensitive);
    for(const QString &name : names) {
        QWidget *tab = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(tab);
        layout->setContentsMargins(0, 4, 0, 0);

        FilterListWidget *panel = new FilterListWidget(this);
        panel->setSourceColor(collectionColor(name));
        connectPanel(panel);
        layout->addWidget(panel);

        collection_panels.insert(name, panel);
        filter_tabs->addTab(tab, name);
    }
    applyCollectionTabColors();
}

void FilterBrowserPanel::populateFavoritesPanel() {
    fillPanel(favorites_panel, QStringList(favorites.values()));
}

void FilterBrowserPanel::populateCollectionPanels() {
    for(auto it = collection_panels.begin(); it != collection_panels.end(); ++it)
        fillPanel(it.value(), collections.value(it.key()));
}

void FilterBrowserPanel::populateAllFiltersPanel(const QString &filterText) {
    std::vector<std::string> tokens;
    if(!filterText.isEmpty())
        token::tokenize(toLowerStr(filterText.toStdString()), std::string(" "), tokens);

    QStringList names;
    for(int i = 0; i < ac::draw_max - 6; ++i) {
        const std::string &name = ac::draw_strings[i];
        if(isBrokenFilter(name))
            continue;
        const QString qname = QString::fromStdString(name);
        if(!tokens.empty()) {
            // Search the real name and the rename, so either finds it.
            const std::string haystack =
                toLowerStr(name) + " " + toLowerStr(displayName(qname).toStdString());
            bool match = false;
            for(auto &tok : tokens) {
                if(haystack.find(tok) != std::string::npos) {
                    match = true;
                    break;
                }
            }
            if(!match) continue;
        }
        names << qname;
    }
    fillPanel(all_filters_panel, names);
}

void FilterBrowserPanel::refreshAllPanels() {
    populateAllFiltersPanel(all_filters_search ? all_filters_search->text() : QString());
    populateFavoritesPanel();
    populateCollectionPanels();
}

void FilterBrowserPanel::searchChanged(const QString &text) {
    populateAllFiltersPanel(text);
}

void FilterBrowserPanel::onItemClicked(QListWidgetItem *item) {
    if(!item) return;
    emit filterClicked(realFilterName(item));
}

void FilterBrowserPanel::toggleFavoriteFromAny(QListWidgetItem *item) {
    if(!item) return;
    const QString real = realFilterName(item);
    if(favorites.contains(real))
        favorites.remove(real);
    else
        favorites.insert(real);
    saveFavorites();
    // Every panel shows the same heart for the same filter, so refresh the
    // lot rather than patching individual rows.
    refreshAllPanels();
}

void FilterBrowserPanel::showFilterContextMenu(const QPoint &pos) {
    FilterListWidget *list = qobject_cast<FilterListWidget *>(sender());
    if(!list) return;
    QListWidgetItem *item = list->itemAt(pos);
    if(!item) return;

    const QString real = realFilterName(item);
    const QString current_collection = collectionForPanel(list);

    QMenu menu(this);
    QAction *fav_action = menu.addAction(favorites.contains(real)
        ? tr("Retirer des favoris")
        : tr("Mettre en favoris"));

    QMenu *collection_menu = menu.addMenu(tr("Ajouter à une collection"));
    QAction *new_collection_action = collection_menu->addAction(tr("Nouvelle collection..."));
    QStringList collection_names = collections.keys();
    collection_names.sort(Qt::CaseInsensitive);
    QMap<QAction *, QString> collection_actions;
    if(!collection_names.isEmpty())
        collection_menu->addSeparator();
    for(const QString &name : collection_names) {
        QAction *a = collection_menu->addAction(collectionDotIcon(collectionColor(name)), name);
        a->setCheckable(true);
        a->setChecked(collections.value(name).contains(real));
        collection_actions.insert(a, name);
    }

    QAction *remove_here = nullptr;
    if(!current_collection.isEmpty())
        remove_here = menu.addAction(tr("Retirer de « %1 »").arg(current_collection));

    menu.addSeparator();
    QAction *rename_action = menu.addAction(tr("Renommer..."));

    QAction *chosen = menu.exec(list->mapToGlobal(pos));
    if(!chosen) return;

    if(chosen == fav_action) {
        toggleFavoriteFromAny(item);
        return;
    }

    if(chosen == new_collection_action) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Nouvelle collection"),
            tr("Nom de la collection :"), QLineEdit::Normal, QString(), &ok).trimmed();
        if(!ok || name.isEmpty()) return;
        if(collections.contains(name) && collections.value(name).contains(real)) {
            emit statusMessage(tr("« %1 » est déjà dans « %2 ».").arg(displayName(real), name));
            return;
        }

        const QColor suggested = defaultCollectionColor(collections.size());
        const QColor color = QColorDialog::getColor(suggested, this,
            tr("Couleur de la collection « %1 »").arg(name));
        // Cancelling the colour picker keeps the suggested one rather than
        // leaving the collection without any colour.
        collection_colors.insert(name, (color.isValid() ? color : suggested).name());
        saveFilterCollectionColors(collection_colors);

        collections[name] << real;
        saveFilterCollections(collections);
        syncCollectionTabs();
        refreshAllPanels();
        emit statusMessage(tr("« %1 » ajouté à la nouvelle collection « %2 ».").arg(displayName(real), name));
        return;
    }

    if(collection_actions.contains(chosen)) {
        const QString name = collection_actions.value(chosen);
        QStringList &members = collections[name];
        if(members.contains(real)) {
            members.removeAll(real);
            emit statusMessage(tr("« %1 » retiré de « %2 ».").arg(displayName(real), name));
        } else {
            members << real;
            emit statusMessage(tr("« %1 » ajouté à « %2 ».").arg(displayName(real), name));
        }
        saveFilterCollections(collections);
        refreshAllPanels();
        return;
    }

    if(remove_here && chosen == remove_here) {
        collections[current_collection].removeAll(real);
        saveFilterCollections(collections);
        refreshAllPanels();
        emit statusMessage(tr("« %1 » retiré de « %2 ».").arg(displayName(real), current_collection));
        return;
    }

    if(chosen == rename_action) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Renommer le filtre"),
            tr("Nom affiché pour « %1 » (laisse vide pour revenir au nom d'origine) :").arg(real),
            // Pre-filled with the name currently shown, so it can be edited
            // rather than retyped from scratch.
            QLineEdit::Normal, displayName(real), &ok).trimmed();
        if(!ok) return;
        if(name.isEmpty() || name == real)
            filter_renames.remove(real);
        else
            filter_renames.insert(real, name);
        saveFilterRenames(filter_renames);
        refreshAllPanels();
        emit renamesChanged();
        emit statusMessage(name.isEmpty()
            ? tr("« %1 » a retrouvé son nom d'origine.").arg(real)
            : tr("« %1 » s'appelle maintenant « %2 ».").arg(real, name));
    }
}

void FilterBrowserPanel::onFilterDroppedOnTab(int index, QString realName) {
    if(index < 2 || index >= filter_tabs->count() || realName.isEmpty()) return;
    const QString name = filter_tabs->tabText(index);
    if(!collections.contains(name)) return;

    QStringList &members = collections[name];
    if(members.contains(realName)) {
        emit statusMessage(tr("« %1 » est déjà dans « %2 ».").arg(displayName(realName), name));
        return;
    }
    members << realName;
    saveFilterCollections(collections);
    refreshAllPanels();
    emit statusMessage(tr("« %1 » ajouté à « %2 ».").arg(displayName(realName), name));
}

void FilterBrowserPanel::showTabContextMenu(const QPoint &pos) {
    const int index = filter_tabs->tabBar()->tabAt(pos);
    if(index < 2) return;  // "Tous" and "Favoris" are not collections
    const QString name = filter_tabs->tabText(index);

    QMenu menu(this);
    QAction *color_action = menu.addAction(collectionDotIcon(collectionColor(name)),
        tr("Changer la couleur..."));
    QAction *rename_action = menu.addAction(tr("Renommer la collection..."));
    QAction *delete_action = menu.addAction(tr("Supprimer la collection"));
    QAction *chosen = menu.exec(filter_tabs->tabBar()->mapToGlobal(pos));
    if(!chosen) return;

    if(chosen == color_action) {
        const QColor color = QColorDialog::getColor(collectionColor(name), this,
            tr("Couleur de la collection « %1 »").arg(name));
        if(!color.isValid()) return;
        collection_colors.insert(name, color.name());
        saveFilterCollectionColors(collection_colors);
        applyCollectionTabColors();
        emit statusMessage(tr("Couleur de « %1 » mise à jour.").arg(name));
        return;
    }

    if(chosen == rename_action) {
        bool ok = false;
        const QString new_name = QInputDialog::getText(this, tr("Renommer la collection"),
            tr("Nouveau nom :"), QLineEdit::Normal, name, &ok).trimmed();
        if(!ok || new_name.isEmpty() || new_name == name) return;
        if(collections.contains(new_name)) {
            QMessageBox::warning(this, tr("Nom déjà utilisé"),
                tr("Une collection « %1 » existe déjà.").arg(new_name));
            return;
        }
        collections.insert(new_name, collections.take(name));
        // The colour is keyed by name, so it has to follow the rename.
        if(collection_colors.contains(name))
            collection_colors.insert(new_name, collection_colors.take(name));
        saveFilterCollections(collections);
        saveFilterCollectionColors(collection_colors);
        syncCollectionTabs();
        refreshAllPanels();
        emit statusMessage(tr("Collection renommée en « %1 ».").arg(new_name));
        return;
    }

    if(chosen == delete_action) {
        if(QMessageBox::question(this, tr("Supprimer la collection"),
               tr("Supprimer la collection « %1 » ?\n\nLes filtres eux-mêmes ne sont pas supprimés.").arg(name))
           != QMessageBox::Yes)
            return;
        collections.remove(name);
        collection_colors.remove(name);
        saveFilterCollections(collections);
        saveFilterCollectionColors(collection_colors);
        syncCollectionTabs();
        refreshAllPanels();
        emit statusMessage(tr("Collection « %1 » supprimée.").arg(name));
    }
}
