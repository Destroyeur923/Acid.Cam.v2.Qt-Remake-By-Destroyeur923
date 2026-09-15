#include "filter_utils.h"
#include <QMouseEvent>
#include <QPainter>
#include <QDrag>
#include <QMimeData>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDialog>
#include <QDialogButtonBox>
#include <algorithm>

const int HeartIconZone = 24;

namespace {
QSettings appSettings() {
    return QSettings("LostSideDead", "Acid Cam Qt");
}
}

QMap<QString, QStringList> loadFilterCollections() {
    QMap<QString, QStringList> result;
    QSettings settings = appSettings();
    const QJsonDocument doc =
        QJsonDocument::fromJson(settings.value("FilterCollections").toString().toUtf8());
    if(!doc.isObject())
        return result;
    const QJsonObject root = doc.object();
    for(auto it = root.begin(); it != root.end(); ++it) {
        QStringList members;
        for(const QJsonValue &v : it.value().toArray())
            members << v.toString();
        result.insert(it.key(), members);
    }
    return result;
}

void saveFilterCollections(const QMap<QString, QStringList> &collections) {
    QJsonObject root;
    for(auto it = collections.begin(); it != collections.end(); ++it) {
        QJsonArray members;
        for(const QString &name : it.value())
            members.append(name);
        root.insert(it.key(), members);
    }
    QSettings settings = appSettings();
    settings.setValue("FilterCollections", QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

QMap<QString, QString> loadFilterRenames() {
    QMap<QString, QString> result;
    QSettings settings = appSettings();
    const QJsonDocument doc =
        QJsonDocument::fromJson(settings.value("FilterRenames").toString().toUtf8());
    if(!doc.isObject())
        return result;
    const QJsonObject root = doc.object();
    for(auto it = root.begin(); it != root.end(); ++it)
        result.insert(it.key(), it.value().toString());
    return result;
}

void saveFilterRenames(const QMap<QString, QString> &renames) {
    QJsonObject root;
    for(auto it = renames.begin(); it != renames.end(); ++it)
        root.insert(it.key(), it.value());
    QSettings settings = appSettings();
    settings.setValue("FilterRenames", QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

QMap<QString, QString> loadFilterCollectionColors() {
    QMap<QString, QString> result;
    QSettings settings = appSettings();
    const QJsonDocument doc =
        QJsonDocument::fromJson(settings.value("FilterCollectionColors").toString().toUtf8());
    if(!doc.isObject())
        return result;
    const QJsonObject root = doc.object();
    for(auto it = root.begin(); it != root.end(); ++it)
        result.insert(it.key(), it.value().toString());
    return result;
}

void saveFilterCollectionColors(const QMap<QString, QString> &colors) {
    QJsonObject root;
    for(auto it = colors.begin(); it != colors.end(); ++it)
        root.insert(it.key(), it.value());
    QSettings settings = appSettings();
    settings.setValue("FilterCollectionColors", QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

QIcon collectionDotIcon(const QColor &color) {
    QPixmap pix(14, 14);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(color);
    painter.setPen(QPen(QColor(25, 25, 25), 1));
    painter.drawEllipse(1, 1, 11, 11);
    return QIcon(pix);
}

QColor defaultCollectionColor(int index) {
    static const QColor palette[] = {
        QColor(220, 90, 90),  QColor(90, 160, 220), QColor(110, 190, 110),
        QColor(220, 160, 70), QColor(170, 120, 220), QColor(80, 200, 195),
        QColor(220, 120, 180), QColor(200, 200, 90)
    };
    const int count = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    if(index < 0) index = 0;
    return palette[index % count];
}

const char *FilterColorMimeType = "application/x-acidcam-filter-color";

bool filterNeedsSubFilter(const QString &realName) {
    return realName.contains(QStringLiteral("SubFilter"));
}

QString subFilterLabel(const QString &hostDisplayName, int subfilter) {
    if(subfilter < 0 || subfilter >= static_cast<int>(ac::draw_strings.size()))
        return hostDisplayName;
    const QString sub = QString::fromStdString(ac::draw_strings[subfilter]);
    const QMap<QString, QString> renames = loadFilterRenames();
    const QString shown = renames.value(sub);
    return hostDisplayName + QString::fromUtf8(" → ") + (shown.isEmpty() ? sub : shown);
}

int chooseSubFilter(QWidget *parent, const QString &hostDisplayName, int current) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Sous-filtre pour « %1 »").arg(hostDisplayName));
    dialog.resize(420, 520);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *info = new QLabel(QObject::tr(
        "« %1 » combine l'image avec un deuxième filtre.\nChoisis lequel :").arg(hostDisplayName), &dialog);
    info->setWordWrap(true);
    layout->addWidget(info);

    QLineEdit *search = new QLineEdit(&dialog);
    search->setPlaceholderText(QObject::tr("Rechercher..."));
    layout->addWidget(search);

    QListWidget *list = new QListWidget(&dialog);
    layout->addWidget(list, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    // Double-clicking a row is the fast path.
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    const QMap<QString, QString> renames = loadFilterRenames();
    // A sub-filter host cannot itself be used as a sub-filter, and the broken
    // ones stay out here too.
    struct Entry { QString shown; int index; };
    QVector<Entry> entries;
    for(int i = 0; i < ac::draw_max - 6; ++i) {
        const std::string &name = ac::draw_strings[i];
        const QString qname = QString::fromStdString(name);
        if(isBrokenFilter(name) || filterNeedsSubFilter(qname))
            continue;
        const QString renamed = renames.value(qname);
        entries.push_back({renamed.isEmpty() ? qname : renamed, filter_map[name].filter});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.shown.compare(b.shown, Qt::CaseInsensitive) < 0;
    });

    auto refill = [&](const QString &needle) {
        list->clear();
        for(const Entry &e : entries) {
            if(!needle.isEmpty() && !e.shown.contains(needle, Qt::CaseInsensitive))
                continue;
            QListWidgetItem *item = new QListWidgetItem(e.shown, list);
            item->setData(Qt::UserRole, e.index);
            if(e.index == current)
                list->setCurrentItem(item);
        }
        if(!list->currentItem() && list->count() > 0)
            list->setCurrentRow(0);
    };
    refill(QString());
    QObject::connect(search, &QLineEdit::textChanged, refill);

    if(dialog.exec() != QDialog::Accepted || !list->currentItem())
        return -1;
    return list->currentItem()->data(Qt::UserRole).toInt();
}

// Brown: the "no particular collection" colour, used by the "Tous" tab.
QColor filterNeutralColor() { return QColor(124, 78, 42); }
// Same red as the filled heart, so a favorite reads as a favorite.
QColor filterFavoriteColor() { return QColor(220, 40, 70); }

FilterDropLabel::FilterDropLabel(QWidget *parent) : QLabel(parent) {
    setAcceptDrops(true);
}

void FilterDropLabel::dragEnterEvent(QDragEnterEvent *event) {
    if(event->mimeData()->hasText())
        event->acceptProposedAction();
}

void FilterDropLabel::dragMoveEvent(QDragMoveEvent *event) {
    if(event->mimeData()->hasText())
        event->acceptProposedAction();
}

void FilterDropLabel::dropEvent(QDropEvent *event) {
    if(event->mimeData()->hasText()) {
        emit filterDropped(event->mimeData()->text());
        event->acceptProposedAction();
    }
}

FilterListWidget::FilterListWidget(QWidget *parent) : QListWidget(parent) {
    setDragEnabled(true);
}

std::string toLowerStr(std::string text) {
    for(auto &c : text) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return text;
}

bool isBrokenFilter(const std::string &name) {
    static const char *excluded[] = {
        "SetCurrentFrameStateAsSource", "ExpandSquareVertical", "DistortPixelate128",
        "Zoom", "AcidShuffleMedian", "MatrixColorBlur", "AlphaBlendArrayExpand",
        "ImageXorSmooth", "SketchFilter", "SlideSub", "Histogram", "Desktop",
        "MultiVideo", "Solo", "Bars", "BilateralFilter", "BilateralFilterFade",
        "BoxFilter", "CurrentDesktopRect", "HorizontalTrailsInter", "IntertwineAlpha",
        "IntertwineAlphaBlend", "IntertwineVideo640", "RandomAlphaBlendFilter",
        "RandomOrigFrame", "RectangleGlitch", "SquareSwap64x32", "VideoColorMap", 0
    };
    for(int i = 0; excluded[i] != 0; ++i)
        if(name.find(excluded[i]) != std::string::npos)
            return true;
    return false;
}

QIcon heartIcon(bool filled) {
    QPixmap pix(HeartIconZone, HeartIconZone);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    QFont font = painter.font();
    font.setPointSize(12);
    painter.setFont(font);
    painter.setPen(filled ? QColor(220, 40, 70) : QColor(130, 130, 130));
    painter.drawText(pix.rect(), Qt::AlignCenter, filled ? QString::fromUtf8("♥") : QString::fromUtf8("♡"));
    return QIcon(pix);
}

void FilterListWidget::mousePressEvent(QMouseEvent *event) {
    QListWidgetItem *item = itemAt(event->pos());
    if(item) {
        QRect r = visualItemRect(item);
        if(event->pos().x() - r.left() < HeartIconZone) {
            emit heartClicked(item);
            return;
        }
    }
    QListWidget::mousePressEvent(event);
}

void FilterListWidget::startDrag(Qt::DropActions supportedActions) {
    Q_UNUSED(supportedActions);
    QListWidgetItem *item = currentItem();
    if(!item) return;
    QMimeData *mime = new QMimeData();
    // Rows that were renamed keep the real filter name in UserRole; the drop
    // target always needs that one to look the filter up.
    const QString real = item->data(Qt::UserRole).toString();
    mime->setText(real.isEmpty() ? item->text() : real);
    // Where it was taken from, so the drop target can colour the result.
    if(source_color.isValid())
        mime->setData(FilterColorMimeType, source_color.name().toUtf8());
    QDrag *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
}
