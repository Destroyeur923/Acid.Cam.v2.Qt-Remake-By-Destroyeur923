/*
 * Acid Cam v2 - Qt/OpenCV Edition
 * The "Tous / Favoris / collections" filter browser shared by the Lab, the
 * Photo tab and Montage V2.
 *
 * It owns the favorites, the collections, their colours and the filter
 * renames - all stored app-wide in QSettings, so every tab always shows the
 * same organisation. Rows carry the real filter name in Qt::UserRole while
 * showing the user's rename, and dragging a row out exports the real name.
 */

#ifndef __FILTER_BROWSER_PANEL_H__
#define __FILTER_BROWSER_PANEL_H__

#include "qtheaders.h"
#include "filter_utils.h"
#include <QSet>
#include <QMap>
#include <QTabBar>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>

// Tab bar that also accepts a filter dragged from one of the lists, so a
// filter can be filed into a collection by dropping it on the collection's
// tab instead of going through the right-click menu.
class CollectionTabBar : public QTabBar {
    Q_OBJECT
public:
    explicit CollectionTabBar(QWidget *parent = nullptr);

signals:
    void filterDroppedOnTab(int index, QString realName);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int highlight_tab = -1;
};

// QTabWidget::setTabBar is protected, so installing the drop-aware tab bar
// has to happen from inside a subclass.
class CollectionTabWidget : public QTabWidget {
    Q_OBJECT
public:
    explicit CollectionTabWidget(QWidget *parent = nullptr);
    CollectionTabBar *collectionTabBar() const { return bar; }

private:
    CollectionTabBar *bar;
};

class FilterBrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit FilterBrowserPanel(QWidget *parent = nullptr);

    // Label shown for a filter: its rename when it has one, else itself.
    QString displayName(const QString &realName) const;
    // Re-reads the organisation from QSettings; useful when another window
    // may have changed it while this one was open.
    void reload();

signals:
    // Always the real filter name, never a rename.
    void filterClicked(QString realName);
    // A rename happened: hosts showing filter names elsewhere (layer list,
    // timeline clips...) should refresh their own labels.
    void renamesChanged();
    void statusMessage(QString text);

private slots:
    void onItemClicked(QListWidgetItem *item);
    void toggleFavoriteFromAny(QListWidgetItem *item);
    void searchChanged(const QString &text);
    void showFilterContextMenu(const QPoint &pos);
    void showTabContextMenu(const QPoint &pos);
    void onFilterDroppedOnTab(int index, QString realName);

private:
    void createControls();
    void saveFavorites();
    void populateAllFiltersPanel(const QString &filterText = QString());
    void populateFavoritesPanel();
    void populateCollectionPanels();
    void syncCollectionTabs();
    void refreshAllPanels();
    void fillPanel(FilterListWidget *panel, const QStringList &realNames);
    void connectPanel(FilterListWidget *panel);
    QString realFilterName(QListWidgetItem *item) const;
    QString collectionForPanel(QWidget *panel) const;
    QColor collectionColor(const QString &name) const;
    void applyCollectionTabColors();

    QTabWidget *filter_tabs;
    FilterListWidget *all_filters_panel;
    QLineEdit *all_filters_search;
    FilterListWidget *favorites_panel;
    // Collection name -> its tab's list. Tabs 0 and 1 are always "Tous" and
    // "Favoris"; every tab after that is a collection.
    QMap<QString, FilterListWidget *> collection_panels;

    QSet<QString> favorites;
    QMap<QString, QStringList> collections;
    QMap<QString, QString> collection_colors;  // collection -> "#rrggbb"
    QMap<QString, QString> filter_renames;     // real name -> shown name
};

#endif
