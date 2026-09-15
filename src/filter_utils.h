/*
 * Acid Cam v2 - Qt/OpenCV Edition
 * Shared filter-list helpers used by both the Lab and the Montage editor.
 */

#ifndef __FILTER_UTILS_H__
#define __FILTER_UTILS_H__

#include "qtheaders.h"
#include <QMap>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

std::string toLowerStr(std::string text);
bool isBrokenFilter(const std::string &name);
QIcon heartIcon(bool filled);
extern const int HeartIconZone;

// App-wide filter organisation, stored in the same QSettings as the shared
// favorites so every tab sees the same collections and renames. Kept here
// rather than in one window so the other tabs can adopt it without
// duplicating the storage format.
//
// Collections map a collection name to the list of *real* filter names it
// holds; renames map a real filter name to the label shown to the user.
// Only the display changes - lookups into filter_map always use the real
// name, which is also what gets saved into a montage file.
QMap<QString, QStringList> loadFilterCollections();
void saveFilterCollections(const QMap<QString, QStringList> &collections);
QMap<QString, QString> loadFilterRenames();
void saveFilterRenames(const QMap<QString, QString> &renames);

// Per-collection colour, stored separately from the members so the existing
// collection data stays readable as-is. Values are "#rrggbb" strings.
QMap<QString, QString> loadFilterCollectionColors();
void saveFilterCollectionColors(const QMap<QString, QString> &colors);
// Small filled dot used both on the collection tab and in the
// "add to a collection" menu.
QIcon collectionDotIcon(const QColor &color);
// Distinct fallback colour when the user does not pick one.
QColor defaultCollectionColor(int index);

// Colour a dragged filter carries when it does not come from a collection
// tab: brown from "Tous" (and for any filter that belongs to no collection),
// the heart's red from "Favoris".
QColor filterNeutralColor();
QColor filterFavoriteColor();
// Mime type carrying that colour alongside the filter name during a drag.
extern const char *FilterColorMimeType;

// Several hundred libacidcam filters combine the frame with the output of a
// *second* filter, which has to be chosen explicitly - their name always
// says so. Run without one (ac::subfilter left at -1) they either do nothing
// at all or produce garbage, so every tab has to ask for it.
bool filterNeedsSubFilter(const QString &realName);
// Modal picker returning the chosen filter's index into ac::draw_strings,
// or -1 when the user cancels. `current` preselects a previous choice.
// Only filters that are not themselves sub-filter hosts are offered.
int chooseSubFilter(QWidget *parent, const QString &hostDisplayName, int current = -1);
// "Host → Sub" once a sub-filter is set, plain host name otherwise. Used for
// layer rows, timeline clips and status messages.
QString subFilterLabel(const QString &hostDisplayName, int subfilter);

// A filter list where each row shows a clickable favorite heart and, once
// dragging starts on the rest of the row, exports the filter name as plain
// text so it can be dropped onto a Montage V2 effect track or a photo.
// Rows may carry the real filter name in Qt::UserRole when the visible text
// is a user-defined rename; the drag always exports the real name.
// A view that turns a filter dragged onto it into a real, permanent effect
// (as opposed to a click on the list, which only previews). Qt needs all
// three handlers: the default dragMoveEvent refuses the drop, which looks
// exactly like drag-and-drop silently not working.
class FilterDropLabel : public QLabel {
    Q_OBJECT
public:
    explicit FilterDropLabel(QWidget *parent = nullptr);

signals:
    void filterDropped(QString filterName);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};

class FilterListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit FilterListWidget(QWidget *parent = nullptr);
    // Colour this list stands for (its collection's colour, the favorites'
    // red, or the neutral brown of "Tous"). It travels with the drag so the
    // drop target can tint the clip by where the filter was taken from -
    // a filter in two collections keeps the colour of the tab it came from.
    void setSourceColor(const QColor &color) { source_color = color; }
    QColor sourceColor() const { return source_color; }
signals:
    void heartClicked(QListWidgetItem *item);
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void startDrag(Qt::DropActions supportedActions) override;
private:
    QColor source_color;
};

#endif
