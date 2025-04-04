// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "mainwindow.h"
#include "dialog.h"

#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSqlRecord>
#include <QSqlRelationalTableModel>
#include <QTableView>

MainWindow::MainWindow(const QString &artistTable, const QString &albumTable,
                       QFile *albumDetails, QWidget *parent)
     : QMainWindow(parent)
{
    file = albumDetails;
    readAlbumData();

    model = new QSqlRelationalTableModel(this);
    model->setTable(albumTable);
    model->setRelation(2, QSqlRelation(artistTable, "id", "artist"));
    model->select();

    QGroupBox *artists = createArtistGroupBox();
    QGroupBox *albums = createAlbumGroupBox();
    QGroupBox *details = createDetailsGroupBox();

    artistView->setCurrentIndex(0);
    Dialog::setInitialAlbumAndArtistId(model->rowCount(), artistView->count());

    connect(model, &QSqlRelationalTableModel::rowsInserted,
            this, &MainWindow::adjustHeader);
    connect(model, &QSqlRelationalTableModel::rowsRemoved,
            this, &MainWindow::adjustHeader);

    QGridLayout *layout = new QGridLayout;
    layout->addWidget(artists, 0, 0);
    layout->addWidget(albums, 1, 0);
    layout->addWidget(details, 0, 1, 2, 1);
    layout->setColumnStretch(1, 1);
    layout->setColumnMinimumWidth(0, 500);

    QWidget *widget = new QWidget;
    widget->setLayout(layout);
    setCentralWidget(widget);
    createMenuBar();

    showImageLabel();
    resize(850, 400);
    setWindowTitle(tr("Music Archive"));
}

void MainWindow::changeArtist(int row)
{
    if (row > 0) {
        QModelIndex index = model->relationModel(2)->index(row, 1);
        model->setFilter("artist = '" + index.data().toString() + '\'') ;
        showArtistProfile(index);
    } else if (row == 0) {
        model->setFilter(QString());
        showImageLabel();
    } else {
        return;
    }
}

void MainWindow::showArtistProfile(const QModelIndex &index)
{
    QSqlRecord record = model->relationModel(2)->record(index.row());

    QString name = record.value("artist").toString();
    QString count = record.value("albumcount").toString();
    profileLabel->setText(tr("Artist : %1 \n" \
                             "Number of Albums: %2").arg(name).arg(count));

    profileLabel->show();
    iconLabel->show();

    titleLabel->hide();
    trackList->hide();
    imageLabel->hide();
}

void MainWindow::showAlbumDetails(const QModelIndex &index)
{
    QSqlRecord record = model->record(index.row());

    QString artist = record.value("artist").toString();
    QString title = record.value("title").toString();
    QString year = record.value("year").toString();
    QString albumId = record.value("albumid").toString();

    showArtistProfile(indexOfArtist(artist));
    titleLabel->setText(tr("Title: %1 (%2)").arg(title).arg(year));
    titleLabel->show();

    QDomNodeList albums = albumData.elementsByTagName("album");
    for (int i = 0; i < albums.count(); ++i) {
        QDomNode album = albums.item(i);
        if (album.toElement().attribute("id") == albumId) {
            getTrackList(album.toElement());
            break;
        }
    }
    if (trackList->count() != 0)
        trackList->show();
}

void MainWindow::getTrackList(QDomNode album)
{
    trackList->clear();

    const QDomNodeList tracks = album.childNodes();
    for (int i = 0; i < tracks.count(); ++i) {

        const QDomNode track = tracks.item(i);
        const QString trackNumber = track.toElement().attribute("number");

        QListWidgetItem *item = new QListWidgetItem(trackList);
        item->setText(trackNumber + ": " + track.toElement().text());
    }
}

void MainWindow::addAlbum()
{
    Dialog *dialog = new Dialog(model, albumData, file, this);
    int accepted = dialog->exec();

    if (accepted == QDialog::Accepted) {
        int lastRow = model->rowCount() - 1;
        albumView->selectRow(lastRow);
        albumView->scrollToBottom();
        showAlbumDetails(model->index(lastRow, 0));
    }
}

void MainWindow::deleteAlbum()
{
    const QModelIndexList selection = albumView->selectionModel()->selectedRows(0);

    if (!selection.empty()) {
        const QModelIndex &idIndex = selection.at(0);
        int id = idIndex.data().toInt();
        QString title = idIndex.sibling(idIndex.row(), 1).data().toString();
        QString artist = idIndex.sibling(idIndex.row(), 2).data().toString();

        QMessageBox::StandardButton button;
        button = QMessageBox::question(this, tr("Delete Album"),
                                       tr("Are you sure you want to "
                                          "delete '%1' by '%2'?")
                                       .arg(title, artist),
                                       QMessageBox::Yes | QMessageBox::No);

        if (button == QMessageBox::Yes) {
            removeAlbumFromFile(id);
            removeAlbumFromDatabase(idIndex);
            decreaseAlbumCount(indexOfArtist(artist));

            showImageLabel();
        }
    } else {
        QMessageBox::information(this, tr("Delete Album"),
                                 tr("Select the album you want to delete."));
    }
}

void MainWindow::removeAlbumFromFile(int id)
{
    QDomNodeList albums = albumData.elementsByTagName("album");
    for (int i = 0; i < albums.count(); ++i) {
        QDomNode node = albums.item(i);
        if (node.toElement().attribute("id").toInt() == id) {
            albumData.elementsByTagName("archive").item(0).removeChild(node);
            break;
        }
    }
/*
    The following code is commented out since the example uses an in
    memory database, i.e., altering the XML file will bring the data
    out of sync.

    if (!file->open(QIODevice::WriteOnly)) {
        return;
    } else {
        QTextStream stream(file);
        albumData.elementsByTagName("archive").item(0).save(stream, 4);
        file->close();
    }
*/
}

void MainWindow::removeAlbumFromDatabase(const QModelIndex &index)
{
    model->removeRow(index.row());
    // to avoid a blank row, see QSqlTableModel::removeRows()
    model->select();
}

void MainWindow::decreaseAlbumCount(const QModelIndex &artistIndex)
{
    int row = artistIndex.row();
    QModelIndex albumCountIndex = artistIndex.sibling(row, 2);
    int albumCount = albumCountIndex.data().toInt();

    QSqlTableModel *artists = model->relationModel(2);

    if (albumCount == 1) {
        artists->removeRow(row);
        showImageLabel();
    } else {
        artists->setData(albumCountIndex, QVariant(albumCount - 1));
        artists->submitAll();
    }
}

void MainWindow::readAlbumData()
{
    if (!file->open(QIODevice::ReadOnly))
        return;

    if (!albumData.setContent(file)) {
        file->close();
        return;
    }
    file->close();
}

QGroupBox* MainWindow::createArtistGroupBox()
{
    artistView = new QComboBox;
    artistView->setModel(model->relationModel(2));
    artistView->setModelColumn(1);

    connect(artistView, &QComboBox::currentIndexChanged,
            this, &MainWindow::changeArtist);

    QGroupBox *box = new QGroupBox(tr("Artist"));

    QGridLayout *layout = new QGridLayout;
    layout->addWidget(artistView, 0, 0);
    box->setLayout(layout);

    return box;
}

QGroupBox* MainWindow::createAlbumGroupBox()
{
    QGroupBox *box = new QGroupBox(tr("Album"));

    albumView = new QTableView;
    albumView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    albumView->setSortingEnabled(true);
    albumView->setSelectionBehavior(QAbstractItemView::SelectRows);
    albumView->setSelectionMode(QAbstractItemView::SingleSelection);
    albumView->setShowGrid(false);
    albumView->verticalHeader()->hide();
    albumView->setAlternatingRowColors(true);
    albumView->setModel(model);
    adjustHeader();

    QLocale locale = albumView->locale();
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    albumView->setLocale(locale);

    connect(albumView, &QTableView::clicked,
            this, &MainWindow::showAlbumDetails);
    connect(albumView, &QTableView::activated,
            this, &MainWindow::showAlbumDetails);

    QVBoxLayout *layout = new QVBoxLayout;
    layout->addWidget(albumView, 0, { });
    box->setLayout(layout);

    return box;
}

QGroupBox* MainWindow::createDetailsGroupBox()
{
    QGroupBox *box = new QGroupBox(tr("Details"));

    profileLabel = new QLabel;
    profileLabel->setWordWrap(true);
    profileLabel->setAlignment(Qt::AlignBottom);

    titleLabel = new QLabel;
    titleLabel->setWordWrap(true);
    titleLabel->setAlignment(Qt::AlignBottom);

    iconLabel = new QLabel();
    iconLabel->setAlignment(Qt::AlignBottom | Qt::AlignRight);
    iconLabel->setPixmap(QPixmap(":/images/icon.png"));

    imageLabel = new QLabel;
    imageLabel->setWordWrap(true);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setPixmap(QPixmap(":/images/image.png"));

    trackList = new QListWidget;

    QGridLayout *layout = new QGridLayout;
    layout->addWidget(imageLabel, 0, 0, 3, 2);
    layout->addWidget(profileLabel, 0, 0);
    layout->addWidget(iconLabel, 0, 1);
    layout->addWidget(titleLabel, 1, 0, 1, 2);
    layout->addWidget(trackList, 2, 0, 1, 2);
    layout->setRowStretch(2, 1);
    box->setLayout(layout);

    return box;
}

void MainWindow::createMenuBar()
{
    QAction *addAction = new QAction(tr("&Add album..."), this);
    QAction *deleteAction = new QAction(tr("&Delete album..."), this);
    QAction *quitAction = new QAction(tr("&Quit"), this);
    QAction *aboutAction = new QAction(tr("&About"), this);
    QAction *aboutQtAction = new QAction(tr("About &Qt"), this);

    addAction->setShortcut(tr("Ctrl+A"));
    deleteAction->setShortcut(tr("Ctrl+D"));
    quitAction->setShortcuts(QKeySequence::Quit);

    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(addAction);
    fileMenu->addAction(deleteAction);
    fileMenu->addSeparator();
    fileMenu->addAction(quitAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(aboutAction);
    helpMenu->addAction(aboutQtAction);

    connect(addAction, &QAction::triggered,
            this, &MainWindow::addAlbum);
    connect(deleteAction, &QAction::triggered,
            this, &MainWindow::deleteAlbum);
    connect(quitAction, &QAction::triggered,
            qApp, &QCoreApplication::quit);
    connect(aboutAction, &QAction::triggered,
            this, &MainWindow::about);
    connect(aboutQtAction, &QAction::triggered,
            qApp, &QApplication::aboutQt);
}

void MainWindow::showImageLabel()
{
    profileLabel->hide();
    titleLabel->hide();
    iconLabel->hide();
    trackList->hide();

    imageLabel->show();
}

QModelIndex MainWindow::indexOfArtist(const QString &artist) const
{
    QSqlTableModel *artistModel = model->relationModel(2);

    for (int i = 0; i < artistModel->rowCount(); i++) {
        QSqlRecord record =  artistModel->record(i);
        if (record.value("artist") == artist)
            return artistModel->index(i, 1);
    }
    return QModelIndex();
}

void MainWindow::adjustHeader()
{
    albumView->hideColumn(0);
    albumView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    albumView->resizeColumnToContents(2);
    albumView->resizeColumnToContents(3);
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About Music Archive"),
            tr("<p>The <b>Music Archive</b> example shows how to present "
               "data from different data sources in the same application. "
               "The album titles, and the corresponding artists and release dates, "
               "are kept in a database, while each album's tracks are stored "
               "in an XML file. </p><p>The example also shows how to add as "
               "well as remove data from both the database and the "
               "associated XML file using the API provided by the Qt SQL and "
               "Qt XML modules, respectively.</p>"));
}
