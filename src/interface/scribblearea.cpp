/*

Pencil - Traditional Animation Software
Copyright (C) 2005-2007 Patrick Corrieri & Pascal Naidon

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation;

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

*/
#include <QtGui>
#include <QHashIterator>
#include <cmath>

#include "beziercurve.h"
#include "editor.h"
#include "layerbitmap.h"
#include "layervector.h"
#include "layercamera.h"
#include "bitmapimage.h"
#include "pencilsettings.h"

#include "pentool.h"
#include "penciltool.h"
#include "brushtool.h"
#include "buckettool.h"
#include "edittool.h"
#include "erasertool.h"
#include "eyedroppertool.h"
#include "handtool.h"
#include "movetool.h"
#include "polylinetool.h"
#include "selecttool.h"
#include "smudgetool.h"

#include "scribblearea.h"

#include "strokemanager.h"

#define round(f) ((int)(f + 0.5))


ScribbleArea::ScribbleArea(QWidget *parent, Editor *editor)
    : QWidget(parent)
{
    this->m_pEditor = editor;
    m_currentTool = NULL;

    m_toolSetHash.insert(PEN, new PenTool);
    m_toolSetHash.insert(PENCIL, new PencilTool);
    m_toolSetHash.insert(BRUSH, new BrushTool);
    m_toolSetHash.insert(ERASER, new EraserTool);
    m_toolSetHash.insert(BUCKET, new BucketTool);
    m_toolSetHash.insert(EDIT, new EditTool);
    m_toolSetHash.insert(EYEDROPPER, new EyedropperTool);
    m_toolSetHash.insert(HAND, new HandTool);
    m_toolSetHash.insert(MOVE, new MoveTool);
    m_toolSetHash.insert(POLYLINE, new PolylineTool);
    m_toolSetHash.insert(SELECT, new SelectTool);
    m_toolSetHash.insert(SMUDGE, new SmudgeTool);

    foreach (BaseTool *tool, getTools())
    {
        tool->initialize(editor, this);
    }

    m_currentTool = getTool(PENCIL);
    emit pencilOn();

    m_strokeManager = new StrokeManager();

    QSettings settings("Pencil", "Pencil");

    currentWidth = getTool(PENCIL)->properties.width;
    followContour = 0;

    curveOpacity = (100 - settings.value("curveOpacity").toInt()) / 100.0; // default value is 1.0
    int curveSmoothingLevel = settings.value("curveSmoothing").toInt();
    if (curveSmoothingLevel == 0) { curveSmoothingLevel = 20; settings.setValue("curveSmoothing", curveSmoothingLevel); } // default
    curveSmoothing = curveSmoothingLevel / 20.0; // default value is 1.0

    highResPosition = false; // default is false (because it does not work on Windows)
    if (settings.value("highResPosition").toString() == "true") { highResPosition = true; }
    antialiasing = true; // default value is true (because it's prettier)
    if (settings.value("antialiasing").toString() == "false") { antialiasing = false; }
    shadows = false; // default value is false
    if (settings.value("shadows").toString() == "true") { shadows = true; }
    gradients = 2;
    if (settings.value("gradients").toString() != "") { gradients = settings.value("gradients").toInt(); };

    tabletEraserBackupToolMode = -1;
    tabletInUse = false;
    tabletPressure = 1.0;
    setAttribute(Qt::WA_StaticContents); // ?
    modified = false;
    simplified = false;
    usePressure = true;
    makeInvisible = false;
    somethingSelected = false;
    setCursor(Qt::CrossCursor);
    onionPrev = true;
    onionNext = false;
    showThinLines = false;
    showAllLayers = 1;
    myView = QMatrix(); // identity matrix
    myTempView = QMatrix();
    transMatrix = QMatrix();
    centralView = QMatrix();

    QString background = settings.value("background").toString();
    //  if (background == "")
    background = "white";
    setBackgroundBrush(background);
    bufferImg = new BitmapImage(NULL);

    QRect newSelection(QPoint(0, 0), QSize(0, 0));
    mySelection =  newSelection;
    myTransformedSelection = newSelection;
    myTempTransformedSelection = newSelection;
    offset.setX(0);
    offset.setY(0);
    selectionTransformation.reset();

    tol = 7.0;

    readCanvasFromCache = true;
    mouseInUse = false;
    setMouseTracking(true); // reacts to mouse move events, even if the button is not pressed

    debugRect = QRectF(0, 0, 0, 0);

    setSizePolicy(QSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding));
    QPixmapCache::setCacheLimit(30 * 2 * 1024);
    //setAutoFillBackground (false);
    //setAttribute(Qt::WA_OpaquePaintEvent, false);
    //setAttribute(Qt::WA_NoSystemBackground, true);

    updateAll = false;
}

void ScribbleArea::setColour(const int i)
{
    if (currentTool()->type() == PENCIL)
    {
        getTool(PENCIL)->properties.colourNumber = i;
    }
    else if (currentTool()->type() == PEN || currentTool()->type() == POLYLINE)
    {
        getTool( PEN )->properties.colourNumber = i;
    }
    else if (currentTool()->type() == BRUSH)
    {
        getTool(BRUSH)->properties.colourNumber = i;
    }
    else if (currentTool()->type() == BUCKET)
    {
        getTool(BUCKET)->properties.colourNumber = i;
    }
    else if (currentTool()->type() == EYEDROPPER)
    {
        getTool(PENCIL)->properties.colourNumber = i;
        getTool( PEN )->properties.colourNumber = i;
        getTool(BRUSH)->properties.colourNumber = i;
    }
    m_pEditor->currentColor = m_pEditor->object->getColour(i).colour;
    updateFrame();
}

void ScribbleArea::setColour(const QColor colour)
{
    m_pEditor->currentColor = colour;
}

void ScribbleArea::resetColours()
{
    getTool( PEN )->properties.colourNumber = 0;
    getTool(PENCIL)->properties.colourNumber = 0;
    getTool(BRUSH)->properties.colourNumber = 1;
}

void ScribbleArea::setWidth(const qreal newWidth)
{
    QSettings settings("Pencil", "Pencil");
    if (currentTool()->type() == PENCIL)
    {
        getTool(PENCIL)->properties.width = newWidth;
        settings.setValue("pencilWidth", newWidth);
    }
    else if (currentTool()->type() == ERASER)
    {
        getTool(ERASER)->properties.width = newWidth;
        settings.setValue("eraserWidth", newWidth);
    }
    else if (currentTool()->type() == PEN || currentTool()->type() == POLYLINE)
    {
        getTool( PEN )->properties.width = newWidth;
        settings.setValue("penWidth", newWidth);
    }
    else if (currentTool()->type() == BRUSH)
    {
        getTool(BRUSH)->properties.width = newWidth;
        settings.setValue("brushWidth", newWidth);
    }
    currentWidth = newWidth;
    updateAllFrames();
    setCursor(currentTool()->cursor());
}

void ScribbleArea::setFeather(const qreal newFeather)
{
    QSettings settings("Pencil", "Pencil");
    if (currentTool()->type() == PENCIL)
    {
        getTool(PENCIL)->properties.feather = newFeather;
        settings.setValue("pencilOpacity", newFeather);
    }
    else if (currentTool()->type() == PEN || currentTool()->type() == POLYLINE)
    {
        getTool( PEN )->properties.feather = newFeather;
        settings.setValue("penOpacity", newFeather);
    }
    else if (currentTool()->type() == BRUSH)
    {
        getTool(BRUSH)->properties.feather = newFeather;
        settings.setValue("brushOpacity", newFeather);
    }
    currentWidth = currentTool()->properties.width; // could be unassigned the first time, must be assigned (to avoid black screenings)
    updateAllFrames();
    setCursor(currentTool()->cursor());
}

void ScribbleArea::setOpacity(const qreal newOpacity)
{
    QSettings settings("Pencil", "Pencil");
    if (currentTool()->type() == PENCIL)
    {
        getTool(PENCIL)->properties.opacity = newOpacity;
        settings.setValue("pencilOpacity", newOpacity);
    }
    if (currentTool()->type() == PEN || currentTool()->type() == POLYLINE)
    {
        getTool( PEN )->properties.opacity = newOpacity;
        settings.setValue("penOpacity", newOpacity);
    }
    if (currentTool()->type() == BRUSH)
    {
        getTool(BRUSH)->properties.opacity = newOpacity;
        settings.setValue("brushOpacity", newOpacity);
    }
    //currentWidth = newWidth;
    updateAllFrames();
}

void ScribbleArea::setInvisibility(const bool invisibility)
{
    QSettings settings("Pencil", "Pencil");
    if (currentTool()->type() == PENCIL)
    {
        getTool(PENCIL)->properties.invisibility = invisibility;
        settings.setValue("pencilOpacity", invisibility);
    }
    if (currentTool()->type() == PEN || currentTool()->type() == POLYLINE)
    {
        getTool( PEN )->properties.invisibility = invisibility;
        settings.setValue("penOpacity", invisibility);
    }
    makeInvisible = invisibility;
    updateAllFrames();
}

void ScribbleArea::setPressure(const bool pressure)
{
    QSettings settings("Pencil", "Pencil");
    if (currentTool()->type() == PENCIL)
    {
        getTool(PENCIL)->properties.pressure = pressure;
        settings.setValue("pencilOpacity", pressure);
    }
    if (currentTool()->type() == PEN || currentTool()->type() == POLYLINE)
    {
        getTool( PEN )->properties.pressure = pressure;
        settings.setValue("penOpacity", pressure);
    }
    if (currentTool()->type() == BRUSH)
    {
        getTool(BRUSH)->properties.pressure = pressure;
        settings.setValue("brushOpacity", pressure);
    }
    usePressure = pressure;
    updateAllFrames();
}

void ScribbleArea::setPreserveAlpha(const bool preserveAlpha)
{
    if (currentTool()->type() == PENCIL)
    {
        getTool(PENCIL)->properties.preserveAlpha = preserveAlpha;
    }
    if (currentTool()->type() == PEN || currentTool()->type() == POLYLINE)
    {
        getTool( PEN )->properties.preserveAlpha = preserveAlpha;
    }
    if (currentTool()->type() == BRUSH)
    {
        getTool(BRUSH)->properties.preserveAlpha = preserveAlpha;
    }
}

void ScribbleArea::setFollowContour(const bool followContour)
{
    this->followContour = followContour;
}

void ScribbleArea::setCurveOpacity(int newOpacity)
{
    curveOpacity = newOpacity / 100.0;
    QSettings settings("Pencil", "Pencil");
    settings.setValue("curveOpacity", 100 - newOpacity);
    updateAllVectorLayers();
}

void ScribbleArea::setCurveSmoothing(int newSmoothingLevel)
{
    curveSmoothing = newSmoothingLevel / 20.0;
    QSettings settings("Pencil", "Pencil");
    settings.setValue("curveSmoothing", newSmoothingLevel);
}

void ScribbleArea::setHighResPosition(int x)
{
    QSettings settings("Pencil", "Pencil");
    if (x == 0) { highResPosition = false; settings.setValue("highResPosition", "false"); }
    else { highResPosition = true; settings.setValue("highResPosition", "true"); }
}

void ScribbleArea::setAntialiasing(int x)
{
    QSettings settings("Pencil", "Pencil");
    if (x == 0) { antialiasing = false; settings.setValue("antialiasing", "false"); }
    else { antialiasing = true; settings.setValue("antialiasing", "true"); }
    updateAllVectorLayers();
}

void ScribbleArea::setGradients(int x)
{
    //if (x==0) { gradients = x; } else { gradients = x; }
    QSettings settings("Pencil", "Pencil");
    if (x > 0)
    {
        gradients = x;
        settings.setValue("gradients", gradients);
    }
    else
    {
        gradients = 2;
        gradients = settings.value("gradients").toInt();
    }
    //if (x==0) { antialiasing=false; settings.setValue("antialiasing","false"); }
    //else { antialiasing=true; settings.setValue("antialiasing","true"); }
    updateAllVectorLayers();
}

void ScribbleArea::setShadows(int x)
{
    QSettings settings("Pencil", "Pencil");
    if (x == 0) { shadows = false; settings.setValue("shadows", "false"); }
    else { shadows = true; settings.setValue("shadows", "true"); }
    update();
}

void ScribbleArea::setToolCursors(int x)
{
    pencilSettings()->setValue(kSettingToolCursor, (x != 0));
}

void ScribbleArea::setStyle(int x)
{
    QSettings settings("Pencil", "Pencil");
    if (x == 0) { settings.setValue("style", "default"); }
    else { settings.setValue("style", "aqua"); }
    update();
}

void ScribbleArea::setBackground(int number)
{
    if (number == 1) { setBackgroundBrush("checkerboard"); }
    if (number == 2) { setBackgroundBrush("white"); }
    if (number == 3) { setBackgroundBrush("grey"); }
    if (number == 4) { setBackgroundBrush("dots"); }
    if (number == 5) { setBackgroundBrush("weave"); }
    updateAllFrames();
}

void ScribbleArea::setBackgroundBrush(QString brushName)
{
    QSettings settings("Pencil", "Pencil");
    settings.setValue("background", brushName);
    backgroundBrush = getBackgroundBrush(brushName);
}

QBrush ScribbleArea::getBackgroundBrush(QString brushName)
{
    QBrush brush = QBrush(Qt::white);
    if (brushName == "white")
    {
        brush = QBrush(Qt::white);
    }
    if (brushName == "grey")
    {
        brush = QBrush(Qt::lightGray);
    }
    if (brushName == "checkerboard")
    {
        QPixmap pattern(16, 16);
        pattern.fill(QColor(255, 255, 255));
        QPainter painter(&pattern);
        painter.fillRect(QRect(0, 0, 8, 8), QColor(220, 220, 220));
        painter.fillRect(QRect(8, 8, 8, 8), QColor(220, 220, 220));
        painter.end();
        brush.setTexture(pattern);
    }
    if (brushName == "dots")
    {
        QPixmap pattern(":background/dots.png");
        brush.setTexture(pattern);
    }
    if (brushName == "weave")
    {
        QPixmap pattern(":background/weave.jpg");
        brush.setTexture(pattern);
    }
    if (brushName == "grid")
    {
        /*  QGraphicsScene* scene = new QGraphicsScene();
                            scene->setSceneRect(QRectF(0, 0, 500, 500));
                            scene->addPixmap(QPixmap(":background/grid.jpg"));*/
        brush.setTextureImage(QImage(":background/grid.jpg"));

    }
    return brush;
}

void ScribbleArea::updateFrame()
{
    updateFrame(m_pEditor->m_nCurrentFrameIndex);
}

void ScribbleArea::updateFrame(int frame)
{
    setView();
    int frameNumber = m_pEditor->getLastFrameAtFrame(frame);
    QPixmapCache::remove("frame" + QString::number(frameNumber));
    readCanvasFromCache = true;
    update();
}

void ScribbleArea::updateAllFrames()
{
    setView();
    QPixmapCache::clear();
    readCanvasFromCache = true;
    update();
    updateAll = false;
}

void ScribbleArea::updateAllVectorLayersAtCurrentFrame()
{
    updateAllVectorLayersAt(m_pEditor->m_nCurrentFrameIndex);
}

void ScribbleArea::updateAllVectorLayersAt(int frameNumber)
{
    for (int i = 0; i < m_pEditor->object->getLayerCount(); i++)
    {
        Layer *layer = m_pEditor->object->getLayer(i);
        if (layer->type == Layer::VECTOR) { ((LayerVector *)layer)->getLastVectorImageAtFrame(frameNumber, 0)->setModified(true); }
    }
    updateFrame(m_pEditor->m_nCurrentFrameIndex);
}

void ScribbleArea::updateAllVectorLayers()
{
    for (int i = 0; i < m_pEditor->object->getLayerCount(); i++)
    {
        Layer *layer = m_pEditor->object->getLayer(i);
        if (layer->type == Layer::VECTOR) { ((LayerVector *)layer)->setModified(true); }
    }
    updateAllFrames();
}

void ScribbleArea::setModified(int layerNumber, int frameNumber)
{
    Layer *layer = m_pEditor->object->getLayer(layerNumber);
    //if (layer->type == Layer::VECTOR) ((LayerVector*)layer)->getLastVectorImageAtFrame(frameNumber, 0)->setModified(true);
    if (layer->type == Layer::VECTOR) { ((LayerVector *)layer)->setModified(frameNumber, true); }
    if (layer->type == Layer::BITMAP) { ((LayerImage *)layer)->setModified(frameNumber, true); }
    emit modification(layerNumber);
    //updateFrame(frame);
    updateAllFrames();
}

void ScribbleArea::escape()
{
    deselectAll();
}

void ScribbleArea::keyPressEvent(QKeyEvent *event)
{
    switch (event->key())
    {
    case Qt::Key_Right:
        if (somethingSelected)
        {
            myTempTransformedSelection.translate(1, 0);
            myTransformedSelection = myTempTransformedSelection;
            calculateSelectionTransformation();
            update();
        }
        else
        {
            m_pEditor->scrubForward();
            event->ignore();
        }
        break;
    case Qt::Key_Left:
        if (somethingSelected)
        {
            myTempTransformedSelection.translate(-1, 0);
            myTransformedSelection = myTempTransformedSelection;
            calculateSelectionTransformation();
            update();
        }
        else
        {
            m_pEditor->scrubBackward();
            event->ignore();
        }
        break;
    case Qt::Key_Up:
        if (somethingSelected)
        {
            myTempTransformedSelection.translate(0, -1);
            myTransformedSelection = myTempTransformedSelection;
            calculateSelectionTransformation();
            update();
        }
        else
        {
            m_pEditor->previousLayer();
            event->ignore();
        }
        break;
    case Qt::Key_Down:
        if (somethingSelected)
        {
            myTempTransformedSelection.translate(0, 1);
            myTransformedSelection = myTempTransformedSelection;
            calculateSelectionTransformation();
            update();
        }
        else
        {
            m_pEditor->nextLayer();
            event->ignore();
        }
        break;
    case Qt::Key_Return:
        if (somethingSelected)
        {
            paintTransformedSelection();
            deselectAll();
        }
        else
        {
            if (currentTool()->type() == POLYLINE)
            {
                endPolyline();
            }
            else
            {
                event->ignore();
            }
        }
        break;
    case Qt::Key_Escape:
        if (somethingSelected || currentTool()->type() == POLYLINE)
        {
            escape();
        }
        break;
    case Qt::Key_Backspace:
        if (somethingSelected)
        {
            deleteSelection();
        }
        break;
    case Qt::Key_F1:
        simplified = true;
        emit outlinesChanged(simplified);
        gradients = 0;
        updateAllVectorLayersAtCurrentFrame();
        break;
    case Qt::Key_F2:
        gradients = 1;
        updateAllVectorLayersAtCurrentFrame();
        break;
    case Qt::Key_F3:
        gradients = 2;
        updateAllVectorLayersAtCurrentFrame();
        break;
    default:
        event->ignore();
    }
}

void ScribbleArea::keyReleaseEvent(QKeyEvent *event)
{
    switch (event->key())
    {
    case Qt::Key_F1:
        simplified = false;
        emit outlinesChanged(simplified);
        setGradients(-1);
        updateAllVectorLayersAtCurrentFrame();
        break;
    case Qt::Key_F2:
        setGradients(-1);
        updateAllVectorLayersAtCurrentFrame();
        break;
    case Qt::Key_F3:
        setGradients(-1);
        updateAllVectorLayersAtCurrentFrame();
        break;
    default:
        event->ignore();
    }
}

void ScribbleArea::tabletEvent(QTabletEvent *event)
{
    //qDebug() << "Device" << event->device() << "Pointer type" << event->pointerType();
    m_strokeManager->tabletEvent(event);

    if (event->type() == QEvent::TabletPress) { tabletInUse = true; }
    if (event->type() == QEvent::TabletRelease) { tabletInUse = false; }

    tabletPosition = event->hiResGlobalPos();
    tabletPressure = event->pressure();

    mousePressure.append(tabletPressure);

    currentTool()->adjustPressureSensitiveProperties(tabletPressure, event->pointerType() == QTabletEvent::Cursor);

    if (event->pointerType() == QTabletEvent::Eraser)
    {
        if (tabletEraserBackupToolMode == -1)
        {
            tabletEraserBackupToolMode = currentTool()->type();
            // memorise which tool was being used before switching to the eraser
            emit eraserOn();
        }
    }
    else
    {
        if (tabletEraserBackupToolMode != -1)   // restore the tool in use
        {
            switch (tabletEraserBackupToolMode)
            {
            case PENCIL:
                emit pencilOn();
                break;
            case PEN:
                emit penOn();
                break;
            default:
                emit pencilOn();
            }
            tabletEraserBackupToolMode = -1;
        }
    }
    event->ignore(); // indicates that the tablet event is not accepted yet, so that it is propagated as a mouse event)
}

void ScribbleArea::mousePressEvent(QMouseEvent *event)
{
    static const QString myToolModesDescription[] =
    {
        "Pencil",
        "Eraser",
        "Select",
        "Move",
        "Edit",
        "Hand",
        "Smudge",
        "Pen",
        "Polyline",
        "Bucket",
        "Eyedropper",
        "Colouring"
    };

    mouseInUse = true;

    if (!tabletInUse)   // a mouse is used instead of a tablet
    {
        tabletPressure = 1.0;
        currentTool()->adjustPressureSensitiveProperties(1.0, true);

        //----------------code for starting hand tool when middle mouse is pressed
        if (event->buttons() & Qt::MidButton)
        {
            //qDebug() << "Hand Start " << event->pos();
            prevMode = currentTool()->type();
            emit handOn();
        }
    }

    while (!mousePath.isEmpty()) { mousePath.removeAt(0); } // empty the mousePath
    while (!mousePressure.isEmpty()) { mousePressure.removeAt(0); } // empty the mousePressure

    if (!(event->button() == Qt::NoButton))    // if the user is pressing the left or right button
    {
        if (tabletInUse && highResPosition)
        { lastPixel = QPointF(event->pos()) + tabletPosition - QPointF(event->globalPos()); }
        else
        { lastPixel = QPointF(event->pos()); }

        bool invertible = true;
        lastPoint = myTempView.inverted(&invertible).map(QPointF(lastPixel));
        lastBrushPoint = lastPoint;
    }

    // --- interactive cursor/brush pointer resizing
    if ((event->modifiers() == Qt::ShiftModifier) && (currentTool()->properties.width > -1))  //resize width not locked
    {
        qDebug() << "resizing tool width from " << currentTool()->properties.width; //@
        resizingTool = true;
        resizingToolMode = rtmWIDTH;
        brushOrgSize = currentTool()->properties.width;
        return;
    }
    else if ((event->modifiers() == Qt::ControlModifier) && (currentTool()->properties.feather > -1))    //resize feather not locked
    {
        qDebug() << "resizing tool feather from " << currentTool()->properties.feather; //@
        resizingTool = true;
        resizingToolMode = rtmFEATHER;
        brushOrgSize = currentTool()->properties.feather;
        return;
    }
    else
    {
        qDebug() << "tool #" << currentTool()->type(); //@
        qDebug() << "pressEvt tool width " << currentTool()->properties.width;
        qDebug() << "pressEvt tool feather " << currentTool()->properties.feather;
        resizingTool = false;
    }
    // ---

    // ---- checks ------
    Layer *layer = m_pEditor->getCurrentLayer();
    if (layer == NULL) { return; }

    if (layer->type == Layer::VECTOR)
    {
        VectorImage *vectorImage = ((LayerVector *)layer)->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
        if (vectorImage == NULL) { return; }
    }
    if (layer->type == Layer::BITMAP)
    {
        BitmapImage *bitmapImage = ((LayerBitmap *)layer)->getLastBitmapImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
        if (bitmapImage == NULL) { return; }
    }

    if (!layer->visible && currentTool()->type() != HAND && (event->button() != Qt::RightButton))
    {
        QMessageBox::warning(this, tr("Warning"),
                             tr("You are drawing on a hidden layer! Please select another layer (or make the current layer visible)."),
                             QMessageBox::Ok,
                             QMessageBox::Ok);
        mouseInUse = false;
        return;
    }

    // --- end checks ----


    QPointF pos;
    if (tabletInUse && highResPosition) {
        pos = event->pos() - tabletPosition - event->globalPos();
    } else {
        pos = event->pos();
    }
    m_strokeManager->strokeStart(pos, tabletPressure);

    if (event->button() == Qt::RightButton)
    {
        getTool(HAND)->mousePressEvent(event);
        return;
    }

    currentTool()->mousePressEvent(event);
}

void ScribbleArea::mouseMoveEvent(QMouseEvent *event)
{
    Layer *layer = m_pEditor->getCurrentLayer();
    // ---- checks ------
    if (layer == NULL) { return; }
    if (layer->type == Layer::VECTOR)
    {
        VectorImage *vectorImage = ((LayerVector *)layer)->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
        if (vectorImage == NULL) { return; }
    }
    if (layer->type == Layer::BITMAP)
    {
        BitmapImage *bitmapImage = ((LayerBitmap *)layer)->getLastBitmapImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
        if (bitmapImage == NULL) { return; }
    }
    // ---- end checks ------

    if (tabletInUse  && highResPosition)
    {
        currentPixel = QPointF(event->pos())
                       + tabletPosition
                       - QPointF(event->globalPos());
    }
    else
    {
        currentPixel = event->pos();
    }
    bool invertible = true;
    currentPoint = myTempView.inverted(&invertible).map(QPointF(currentPixel));

    // the user is also pressing the mouse (dragging)
    if (event->buttons() & Qt::LeftButton || event->buttons() & Qt::RightButton)
    {
        offset = currentPoint - lastPoint;
        //Use: [SHIFT]+mouse/pen => scalingBrush=true
        if (resizingTool)
        {
            qreal newSize = brushOrgSize + offset.x();

            if (newSize < 0.2)
            {  newSize = 0.2; }
            else if (newSize > 200)
            { newSize = 200; }

            if (resizingToolMode == rtmWIDTH)
            {   m_pEditor->applyWidth(round(newSize)); }
            else if (resizingToolMode == rtmFEATHER)
            {   m_pEditor->applyFeather(round(newSize)); }
            return;
        }
        //
        mousePath.append(currentPoint);
    }

    QPointF pos;
    if (tabletInUse && highResPosition) {
        pos = event->pos() - tabletPosition - event->globalPos();
    } else {
        pos = event->pos();
    }
    m_strokeManager->strokeMove(pos, tabletPressure);

    if (event->buttons() == Qt::RightButton)
    {
        getTool(HAND)->mouseMoveEvent(event);
        return;
    }

    currentTool()->mouseMoveEvent(event);
}

void ScribbleArea::mouseReleaseEvent(QMouseEvent *event)
{
    mouseInUse = false;

    // ---- checks ------
    if (resizingTool) { return; } // [SHIFT]+drag OR [CTRL]+drag -> XXX make into its own tool

    Layer *layer = m_pEditor->getCurrentLayer();
    if (layer == NULL) { return; }
    if (layer->type == Layer::VECTOR)
    {
        VectorImage *vectorImage = ((LayerVector *)layer)->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
        if (vectorImage == NULL) { return; }
    }
    if (layer->type == Layer::BITMAP)
    {
        BitmapImage *bitmapImage = ((LayerBitmap *)layer)->getLastBitmapImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
        if (bitmapImage == NULL) { return; }
    }
    // ---- end checks ------

    QPointF pos;
    if (tabletInUse && highResPosition) {
        pos = event->pos() - tabletPosition - event->globalPos();
    } else {
        pos = event->pos();
    }
    m_strokeManager->strokeEnd(pos, tabletPressure);

    if (event->button() == Qt::RightButton)
    {
        getTool(HAND)->mouseReleaseEvent(event);
        return;
    }

    currentTool()->mouseReleaseEvent(event);
}

void ScribbleArea::mouseDoubleClickEvent(QMouseEvent *event)
{

    currentTool()->mouseDoubleClickEvent(event);
}

void ScribbleArea::paintBitmapBuffer()
{
    Layer *layer = m_pEditor->getCurrentLayer();
    // ---- checks ------
    if (layer == NULL) { return; }
    // Clear the temporary pixel path
    BitmapImage *targetImage = ((LayerBitmap *)layer)->getLastBitmapImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
    if (targetImage != NULL)
    {
        QPainter::CompositionMode cm = QPainter::CompositionMode_SourceOver;
        switch (currentTool()->type())
        {
        case ERASER:
            cm = QPainter::CompositionMode_DestinationOut;
            break;
        case BRUSH:
            if (getTool(BRUSH)->properties.preserveAlpha) { cm = QPainter::CompositionMode_SourceAtop; }
            if (followContour)
            {
                // writes on the layer below
                if (m_pEditor->m_nCurrentLayerIndex > 0)
                {
                    Layer *layer2 = m_pEditor->getCurrentLayer(-1);
                    if (layer2->type == Layer::BITMAP)
                    {
                        targetImage = ((LayerBitmap *)layer2)->getLastBitmapImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
                    }
                }
            }
            break;
        case PEN:
            if (getTool( PEN )->properties.preserveAlpha) { cm = QPainter::CompositionMode_SourceAtop; }
            break;
        case PENCIL:
            if (getTool(PENCIL)->properties.preserveAlpha) { cm = QPainter::CompositionMode_SourceAtop; }
            break;
        default: //nothing
            break;
        }
        targetImage->paste(bufferImg, cm);
    }
    QRect rect = myTempView.mapRect(bufferImg->boundaries);
    // Clear the buffer
    bufferImg->clear();

    //setModified(layer, editor->currentFrame);
    ((LayerImage *)layer)->setModified(m_pEditor->m_nCurrentFrameIndex, true);
    emit modification();
    QPixmapCache::remove("frame" + QString::number(m_pEditor->m_nCurrentFrameIndex));
    readCanvasFromCache = false;
    updateCanvas(m_pEditor->m_nCurrentFrameIndex, rect.adjusted(-1, -1, 1, 1));
    update(rect);
}

void ScribbleArea::grid()
{
    QPainter painter(this);
    painter.setWorldMatrixEnabled(true);
    painter.setWorldMatrix(centralView.inverted() * transMatrix * centralView);
    painter.drawPixmap(QPoint(0, 0), canvas);
    painter.drawImage(QPoint(100, 100), QImage(":background/grid")); //TODO The grid is being drawn but the white background over rides it!
    //      updateCanvas(editor->currentFrame, event.rect());


}

void ScribbleArea::paintEvent(QPaintEvent *event)
{
    //qDebug() << "paint event!" << QDateTime::currentDateTime() << event->rect(); //readCanvasFromCache << mouseInUse << editor->currentFrame;
    QPainter painter(this);

    // draws the background (if necessary)
    if (mouseInUse && currentTool()->type() == HAND)
    {
        painter.setWorldMatrix(myTempView);
        painter.setWorldMatrixEnabled(true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(backgroundBrush);
        painter.drawRect((myTempView).inverted().mapRect(QRect(-2, -2, width() + 3, height() + 3)));  // this is necessary to have the background move with the view
    }

    // process the canvas (or not)
    if (!mouseInUse && readCanvasFromCache)
    {
        // --- we retrieve the canvas from the cache; we create it if it doesn't exist
        int frameNumber = m_pEditor->getLastFrameAtFrame(m_pEditor->m_nCurrentFrameIndex);
        if (!QPixmapCache::find("frame" + QString::number(frameNumber), canvas))
        {
            updateCanvas(m_pEditor->m_nCurrentFrameIndex, event->rect());
            QPixmapCache::insert("frame" + QString::number(frameNumber), canvas);
        }
    }
    if (currentTool()->type() == MOVE)
    {
        Layer *layer = m_pEditor->getCurrentLayer();
        if (!layer) { return; }
        if (layer->type == Layer::VECTOR) { ((LayerVector *)layer)->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0)->setModified(true); }
        updateCanvas(m_pEditor->m_nCurrentFrameIndex, event->rect());
    }
    // paints the canvas
    painter.setWorldMatrixEnabled(true);
    painter.setWorldMatrix(centralView.inverted() * transMatrix * centralView);
    painter.drawPixmap(QPoint(0, 0), canvas);
    //  painter.drawImage(QPoint(100,100),QImage(":background/grid"));//TODO Success a grid is drawn
    Layer *layer = m_pEditor->getCurrentLayer();
    if (!layer) { return; }

    if (!m_pEditor->playing)    // we don't need to display the following when the animation is playing
    {
        painter.setWorldMatrix(myTempView);

        if (layer->type == Layer::VECTOR)
        {
            VectorImage *vectorImage = ((LayerVector *)layer)->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);

            if (currentTool()->type() == EDIT || currentTool()->type() == HAND)
            {
                //bufferImg->clear();
                painter.save();
                painter.setWorldMatrixEnabled(false);
                painter.setRenderHint(QPainter::Antialiasing, false);
                // ----- paints the edited elements
                QPen pen2(Qt::black, 0.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                painter.setPen(pen2);
                QColor colour;
                // ------------ vertices of the edited curves
                colour = QColor(200, 200, 200);
                painter.setBrush(colour);
                for (int k = 0; k < vectorSelection.curve.size(); k++)
                {
                    int curveNumber = vectorSelection.curve.at(k);
                    //QPainterPath path = vectorImage->curve[curveNumber].getStrokedPath();
                    //bufferImg->drawPath( myTempView.map(path), pen2, colour, QPainter::CompositionMode_SourceOver, false);
                    for (int vertexNumber = -1; vertexNumber < vectorImage->getCurveSize(curveNumber); vertexNumber++)
                    {
                        QPointF vertexPoint = vectorImage->getVertex(curveNumber, vertexNumber);
                        QRectF rectangle = QRectF((myView * transMatrix * centralView).map(vertexPoint) - QPointF(3.0, 3.0), QSizeF(7, 7));
                        if (rect().contains((myView * transMatrix * centralView).map(vertexPoint).toPoint()))
                        {

                            painter.drawRect(rectangle.toRect());
                            //bufferImg->drawRect( rectangle.toRect(), pen2, colour, QPainter::CompositionMode_SourceOver, false);
                        }
                    }

                }
                // ------------ selected vertices of the edited curves
                colour = QColor(100, 100, 255);
                painter.setBrush(colour);
                for (int k = 0; k < vectorSelection.vertex.size(); k++)
                {
                    VertexRef vertexRef = vectorSelection.vertex.at(k);
                    QPointF vertexPoint = vectorImage->getVertex(vertexRef);
                    QRectF rectangle0 = QRectF((myView * transMatrix * centralView).map(vertexPoint) - QPointF(3.0, 3.0), QSizeF(7, 7));
                    painter.drawRect(rectangle0.toRect());
                    //bufferImg->drawRect( rectangle0, pen2, colour, QPainter::CompositionMode_SourceOver, false);

                    /* --- draws the control points -- maybe editable in a future version (although not recommended)
                    QPointF c1Point = vectorImage->getC1(vertexRef.nextVertex());
                    QPointF c2Point = vectorImage->getC2(vertexRef);
                    QRectF rectangle1 = QRectF( myTempView.map(c1Point)-QPointF(3.0,3.0), QSize(7,7) );
                    QRectF rectangle2 = QRectF( myTempView.map(c2Point)-QPointF(3.0,3.0), QSize(7,7) );
                    bufferImg->drawLine( myTempView.map(vertexPoint), myTempView.map(c1Point), colour, QPainter::CompositionMode_SourceOver, antialiasing);
                    bufferImg->drawLine( myTempView.map(vertexPoint), myTempView.map(c2Point), colour, QPainter::CompositionMode_SourceOver, antialiasing);
                    bufferImg->drawRect( rectangle0, pen2, colour, QPainter::CompositionMode_SourceOver, false);
                    bufferImg->drawEllipse( rectangle1, pen2, Qt::white, QPainter::CompositionMode_SourceOver, false);
                    bufferImg->drawEllipse( rectangle2, pen2, Qt::white, QPainter::CompositionMode_SourceOver, false);*/
                }
                // ----- paints the closest vertices
                colour = QColor(255, 0, 0);
                painter.setBrush(colour);
                if (vectorSelection.curve.size() > 0)
                {
                    for (int k = 0; k < closestVertices.size(); k++)
                    {
                        VertexRef vertexRef = closestVertices.at(k);
                        QPointF vertexPoint = vectorImage->getVertex(vertexRef);
                        //if ( vectorImage->isSelected(vertexRef) ) vertexPoint = selectionTransformation.map( vertexPoint );
                        QRectF rectangle = QRectF((myView * transMatrix * centralView).map(vertexPoint) - QPointF(3.0, 3.0), QSizeF(7, 7));
                        painter.drawRect(rectangle.toRect());
                        //bufferImg->drawRect( rectangle.toRect(), pen2, colour, QPainter::CompositionMode_SourceOver, false);
                    }
                }
                painter.restore();
            }

            if (currentTool()->type() == MOVE)
            {
                // ----- paints the closest curves
                bufferImg->clear();
                QPen pen2(Qt::black, 0.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                QColor colour = QColor(100, 100, 255);
                for (int k = 0; k < closestCurves.size(); k++)
                {
                    qreal scale = myTempView.det();
                    BezierCurve myCurve = vectorImage->curve[closestCurves[k]];
                    if (myCurve.isPartlySelected()) { myCurve.transform(selectionTransformation); }
                    QPainterPath path = myCurve.getStrokedPath(1.2 / scale, false);
                    bufferImg->drawPath((myView * transMatrix * centralView).map(path), pen2, colour, QPainter::CompositionMode_SourceOver, antialiasing);
                }
            }

        }

        // paints the  buffer image
        if (m_pEditor->getCurrentLayer() != NULL)
        {
            painter.setOpacity(1.0);
            if (m_pEditor->getCurrentLayer()->type == Layer::BITMAP) { painter.setWorldMatrixEnabled(true); }
            if (m_pEditor->getCurrentLayer()->type == Layer::VECTOR) { painter.setWorldMatrixEnabled(false); }
            bufferImg->paintImage(painter);
        }

        // paints the selection outline
        if (somethingSelected && myTempTransformedSelection.isValid())
        {
            // outline of the transformed selection
            painter.setWorldMatrixEnabled(false);
            painter.setOpacity(1.0);
            QPolygon tempRect = (myView * transMatrix * centralView).mapToPolygon(myTempTransformedSelection.normalized().toRect());

            Layer *layer = m_pEditor->getCurrentLayer();
            if (layer != NULL)
            {
                if (layer->type == Layer::BITMAP)
                {
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(Qt::DashLine);
                }
                if (layer->type == Layer::VECTOR)
                {
                    painter.setBrush(QColor(0, 0, 0, 20));
                    painter.setPen(Qt::gray);
                }
                painter.drawPolygon(tempRect);

                if (layer->type != Layer::VECTOR || currentTool()->type() != SELECT)
                {
                    painter.setPen(Qt::SolidLine);
                    painter.setBrush(QBrush(Qt::gray));
                    painter.drawRect(tempRect.point(0).x() - 3, tempRect.point(0).y() - 3, 6, 6);
                    painter.drawRect(tempRect.point(1).x() - 3, tempRect.point(1).y() - 3, 6, 6);
                    painter.drawRect(tempRect.point(2).x() - 3, tempRect.point(2).y() - 3, 6, 6);
                    painter.drawRect(tempRect.point(3).x() - 3, tempRect.point(3).y() - 3, 6, 6);
                }
            }
        }

    }
    // clips to the frame of the camera
    if (layer->type == Layer::CAMERA)
    {
        QRect rect = ((LayerCamera *)layer)->getViewRect();
        rect.translate(width() / 2, height() / 2);
        painter.setWorldMatrixEnabled(false);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 160));
        painter.drawRect(QRect(0, 0, width(), (height() - rect.height()) / 2));
        painter.drawRect(QRect(0, rect.bottom(), width(), (height() - rect.height()) / 2));
        painter.drawRect(QRect(0, rect.top(), (width() - rect.width()) / 2, rect.height() - 1));
        painter.drawRect(QRect((width() + rect.width()) / 2, rect.top(), (width() - rect.width()) / 2, rect.height() - 1));
        painter.setPen(Qt::black);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect);
    }
    // outlines the frame of the viewport
    painter.setWorldMatrixEnabled(false);
    painter.setPen(QPen(Qt::gray, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRect(0, 0, width(), height()));
    // shadow
    if (shadows && !m_pEditor->playing && (!mouseInUse || currentTool()->type() == HAND))
    {
        int radius1 = 12;
        int radius2 = 8;
        QLinearGradient shadow = QLinearGradient(0, 0, 0, radius1);
        setGaussianGradient(shadow, Qt::black, 0.15, 0.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(shadow);
        painter.drawRect(QRect(0, 0, width(), radius1));
        shadow.setFinalStop(radius1, 0);
        painter.setBrush(shadow);
        painter.drawRect(QRect(0, 0, radius1, height()));
        shadow.setStart(0, height());
        shadow.setFinalStop(0, height() - radius2);
        painter.setBrush(shadow);
        painter.drawRect(QRect(0, height() - radius2, width(), height()));
        shadow.setStart(width(), 0);
        shadow.setFinalStop(width() - radius2, 0);
        painter.setBrush(shadow);
        painter.drawRect(QRect(width() - radius2, 0, width(), height()));
    }
    event->accept();
}

void ScribbleArea::updateCanvas(int frame, QRect rect)
{
    //qDebug() << "paint canvas!" << QDateTime::currentDateTime();
    // merge the different layers into the ScribbleArea
    QPainter painter(&canvas);
    if (myTempView.det() == 1.0)
    {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    }
    else
    {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, antialiasing);
    }
    painter.setClipRect(rect);
    painter.setClipping(true);
    setView();
    painter.setWorldMatrix(myTempView);
    painter.setWorldMatrixEnabled(true);

    // background
    painter.setPen(Qt::NoPen);
    painter.setBrush(backgroundBrush);
    painter.drawRect(myTempView.inverted().mapRect(QRect(-2, -2, width() + 3, height() + 3)));  // this is necessary to have the background move with the view

    // grid
    bool drawGrid = false;
    if (drawGrid)
    {
        painter.setOpacity(1.0);
        painter.setPen(Qt::gray);
        painter.setBrush(Qt::NoBrush);
        // What kind of grid do we want?
        //painter.drawRect(QRect(0,0, mySize.width(), mySize.height()));
        //painter.drawLine( QPoint(0,mySize.height()/2), QPoint(mySize.width(), mySize.height()/2) );
        //painter.drawLine( QPoint(mySize.width()/3, 0), QPoint(mySize.width()/3, mySize.height()) );
        //painter.drawLine( QPoint(mySize.width()*2/3, 0), QPoint(mySize.width()*2/3, mySize.height()) );
    }

    Object *object = m_pEditor->object;
    qreal opacity;
    for (int i = 0; i < object->getLayerCount(); i++)
    {
        opacity = 1.0;
        if (i != m_pEditor->m_nCurrentLayerIndex && (showAllLayers == 1)) { opacity = 0.4; }
        if (m_pEditor->getCurrentLayer()->type == Layer::CAMERA) { opacity = 1.0; }
        Layer *layer = (object->getLayer(i));
        if (layer->visible && (showAllLayers > 0 || i == m_pEditor->m_nCurrentLayerIndex))
        {
            // paints the bitmap images
            if (layer->type == Layer::BITMAP)
            {
                LayerBitmap *layerBitmap = (LayerBitmap *)layer;
                BitmapImage *bitmapImage = layerBitmap->getLastBitmapImageAtFrame(frame, 0);
                if (bitmapImage != NULL)
                {
                    painter.setWorldMatrixEnabled(true);

                    // previous frame (onion skin)
                    BitmapImage *previousImage = layerBitmap->getLastBitmapImageAtFrame(frame, -1);
                    if (previousImage != NULL && onionPrev)
                    {
                        painter.setOpacity(opacity * m_pEditor->getOnionLayer1Opacity() / 100.0);
                        previousImage->paintImage(painter);
                    }
                    BitmapImage *previousImage2 = layerBitmap->getLastBitmapImageAtFrame(frame, -2);
                    if (previousImage2 != NULL && onionPrev)
                    {
                        painter.setOpacity(opacity * m_pEditor->getOnionLayer2Opacity() / 100.0);
                        previousImage2->paintImage(painter);
                    }
                    BitmapImage *previousImage3 = layerBitmap->getLastBitmapImageAtFrame(frame, -3);
                    if (previousImage3 != NULL && onionPrev)
                    {
                        painter.setOpacity(opacity * m_pEditor->getOnionLayer3Opacity() / 100.0);
                        previousImage3->paintImage(painter);
                    }

                    // next frame (onion skin)
                    BitmapImage *nextImage = layerBitmap->getLastBitmapImageAtFrame(frame, 1);
                    if (nextImage != NULL && onionNext)
                    {
                        painter.setOpacity(opacity * m_pEditor->getOnionLayer1Opacity() / 100.0);
                        nextImage->paintImage(painter);
                    }
                    BitmapImage *nextImage2 = layerBitmap->getLastBitmapImageAtFrame(frame, 2);
                    if (nextImage2 != NULL && onionNext)
                    {
                        painter.setOpacity(opacity * m_pEditor->getOnionLayer2Opacity() / 100.0);
                        nextImage2->paintImage(painter);
                    }
                    BitmapImage *nextImage3 = layerBitmap->getLastBitmapImageAtFrame(frame, 3);
                    if (nextImage3 != NULL && onionNext)
                    {
                        painter.setOpacity(opacity * m_pEditor->getOnionLayer3Opacity() / 100.0);
                        nextImage3->paintImage(painter);
                    }

                    // current frame
                    painter.setOpacity(opacity);
                    if (i == m_pEditor->m_nCurrentLayerIndex && somethingSelected && (myTempTransformedSelection != mySelection))
                    {
                        // hole in the original selection -- might support arbitrary shapes in the future
                        painter.setClipping(true);
                        QRegion clip = QRegion(mySelection.toRect());
                        QRegion totalImage = QRegion(myTempView.inverted().mapRect(QRect(-2, -2, width() + 3, height() + 3)));
                        QRegion ImageWithHole = totalImage -= clip;
                        painter.setClipRegion(ImageWithHole, Qt::ReplaceClip);
                        //painter.drawImage(bitmapImage->topLeft(), *(bitmapImage->image) );
                        bitmapImage->paintImage(painter);
                        painter.setClipping(false);
                        // transforms the bitmap selection
                        bool smoothTransform = false;
                        if (myTempTransformedSelection.width() != mySelection.width() || myTempTransformedSelection.height() != mySelection.height()) { smoothTransform = true; }
                        BitmapImage selectionClip = bitmapImage->copy(mySelection.toRect());
                        selectionClip.transform(myTempTransformedSelection, smoothTransform);
                        selectionClip.paintImage(painter);
                        //painter.drawImage(selectionClip.topLeft(), *(selectionClip.image));
                    }
                    else
                    {
                        //painter.drawImage(bitmapImage->topLeft(), *(bitmapImage->image) );
                        bitmapImage->paintImage(painter);
                    }
                    //painter.setPen(Qt::red);
                    //painter.setBrush(Qt::NoBrush);
                    //painter.drawRect(bitmapImage->boundaries);
                }
            }
            // paints the vector images
            if (layer->type == Layer::VECTOR)
            {
                LayerVector *layerVector = (LayerVector *)layer;
                VectorImage *vectorImage = layerVector->getLastVectorImageAtFrame(frame, 0);
                if (somethingSelected)
                {
                    // transforms the vector selection
                    //calculateSelectionTransformation();
                    vectorImage->setSelectionTransformation(selectionTransformation);
                    //vectorImage->setTransformedSelection(myTempTransformedSelection);
                }
                QImage *image = layerVector->getLastImageAtFrame(frame, 0, size(), simplified, showThinLines, curveOpacity, antialiasing, gradients);
                if (image != NULL)
                {
                    painter.setWorldMatrixEnabled(false);

                    // previous frame (onion skin)
                    QImage *previousImage = layerVector->getLastImageAtFrame(frame, -1, size(), simplified, showThinLines, curveOpacity, antialiasing, gradients);
                    if (previousImage != NULL && onionPrev)
                    {
                        painter.setOpacity(opacity * m_pEditor->getOnionLayer1Opacity() / 100.0);
                        painter.drawImage(QPoint(0, 0), *previousImage);
                    }
                    QImage *previousImage2 = layerVector->getLastImageAtFrame(frame, -2, size(), simplified, showThinLines, curveOpacity, antialiasing, gradients);
                    if (previousImage2 != NULL && onionPrev)
                    {
                        painter.setOpacity(opacity * m_pEditor->getOnionLayer2Opacity() / 100.0);
                        painter.drawImage(QPoint(0, 0), *previousImage2);
                    }
                    QImage *previousImage3 = layerVector->getLastImageAtFrame(frame, -3, size(), simplified, showThinLines, curveOpacity, antialiasing, gradients);
                    if (previousImage3 != NULL && onionPrev)
                    {
                        painter.setOpacity(opacity * m_pEditor->getOnionLayer3Opacity() / 100.0);
                        painter.drawImage(QPoint(0, 0), *previousImage3);
                    }

                    // next frame (onion skin)
                    QImage *nextImage = layerVector->getLastImageAtFrame(frame, 1, size(), simplified, showThinLines, curveOpacity, antialiasing, gradients);
                    if (nextImage != NULL && onionNext)
                    {
                        painter.setOpacity(opacity * m_pEditor->getOnionLayer1Opacity() / 100.0);
                        painter.drawImage(QPoint(0, 0), *nextImage);
                    }
                    QImage *nextImage2 = layerVector->getLastImageAtFrame(frame, 2, size(), simplified, showThinLines, curveOpacity, antialiasing, gradients);
                    if (nextImage2 != NULL && onionNext)
                    {
                        painter.setOpacity(opacity * m_pEditor->getOnionLayer2Opacity() / 100.0);
                        painter.drawImage(QPoint(0, 0), *nextImage2);
                    }
                    QImage *nextImage3 = layerVector->getLastImageAtFrame(frame, 3, size(), simplified, showThinLines, curveOpacity, antialiasing, gradients);
                    if (nextImage3 != NULL && onionNext)
                    {
                        painter.setOpacity(opacity * m_pEditor->getOnionLayer3Opacity() / 100.0);
                        painter.drawImage(QPoint(0, 0), *nextImage3);
                    }

                    // current frame
                    painter.setOpacity(opacity);
                    painter.drawImage(QPoint(0, 0), *image);
                }
            }
        }
    }
    painter.end();
}

void ScribbleArea::setGaussianGradient(QGradient &gradient, QColor colour, qreal opacity, qreal offset)
{
    int r = colour.red();
    int g = colour.green();
    int b = colour.blue();
    qreal a = colour.alphaF();
    gradient.setColorAt(0.0, QColor(r, g, b, qRound(a * 255 * opacity)));
    gradient.setColorAt(offset + 0.0 * (1.0 - offset), QColor(r, g, b, qRound(a * 255 * opacity)));
    gradient.setColorAt(offset + 0.1 * (1.0 - offset), QColor(r, g, b, qRound(a * 245 * opacity)));
    gradient.setColorAt(offset + 0.2 * (1.0 - offset), QColor(r, g, b, qRound(a * 217 * opacity)));
    gradient.setColorAt(offset + 0.3 * (1.0 - offset), QColor(r, g, b, qRound(a * 178 * opacity)));
    gradient.setColorAt(offset + 0.4 * (1.0 - offset), QColor(r, g, b, qRound(a * 134 * opacity)));
    gradient.setColorAt(offset + 0.5 * (1.0 - offset), QColor(r, g, b, qRound(a * 94 * opacity)));
    gradient.setColorAt(offset + 0.6 * (1.0 - offset), QColor(r, g, b, qRound(a * 60 * opacity)));
    gradient.setColorAt(offset + 0.7 * (1.0 - offset), QColor(r, g, b, qRound(a * 36 * opacity)));
    gradient.setColorAt(offset + 0.8 * (1.0 - offset), QColor(r, g, b, qRound(a * 20 * opacity)));
    gradient.setColorAt(offset + 0.9 * (1.0 - offset), QColor(r, g, b, qRound(a * 10 * opacity)));
    gradient.setColorAt(offset + 1.0 * (1.0 - offset), QColor(r, g, b, 0));
}

void ScribbleArea::drawBrush(QPointF thePoint, qreal brushWidth, qreal offset, QColor fillColour, qreal opacity)
{
    QRadialGradient radialGrad(thePoint, 0.5 * brushWidth);
    setGaussianGradient(radialGrad, fillColour, opacity, offset);

    //radialGrad.setCenter( thePoint );
    //radialGrad.setFocalPoint( thePoint );

    QRectF rectangle(thePoint.x() - 0.5 * brushWidth, thePoint.y() - 0.5 * brushWidth, brushWidth, brushWidth);

    BitmapImage *tempBitmapImage = new BitmapImage(NULL);
    if (followContour)
    {
        tempBitmapImage = new BitmapImage(NULL, rectangle.toRect(), QColor(0, 0, 0, 0));
        //tempBitmapImage->drawRect( rectangle, Qt::NoPen, QColor(0,0,0,0), QPainter::CompositionMode_Source, antialiasing);
        Layer *layer = m_pEditor->getCurrentLayer();
        if (layer == NULL) { return; }
        int index = ((LayerImage *)layer)->getLastIndexAtFrame(m_pEditor->m_nCurrentFrameIndex);
        if (index == -1) { return; }
        BitmapImage *bitmapImage = ((LayerBitmap *)layer)->getLastBitmapImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
        if (bitmapImage == NULL) { qDebug() << "NULL image pointer!" << m_pEditor->m_nCurrentLayerIndex << m_pEditor->m_nCurrentFrameIndex;  return; }
        BitmapImage::floodFill(bitmapImage, tempBitmapImage, thePoint.toPoint(), qRgba(255, 255, 255, 0), fillColour.rgb(), 20 * 20, false);
        tempBitmapImage->drawRect(rectangle.toRect(), Qt::NoPen, radialGrad, QPainter::CompositionMode_SourceIn, antialiasing);
    }
    else
    {
        tempBitmapImage = new BitmapImage(NULL);
        tempBitmapImage->drawRect(rectangle, Qt::NoPen, radialGrad, QPainter::CompositionMode_Source, antialiasing);
    }

    bufferImg->paste(tempBitmapImage);
    delete tempBitmapImage;
}

void ScribbleArea::drawLineTo(const QPointF &endPixel, const QPointF &endPoint)
{
    Layer *layer = m_pEditor->getCurrentLayer();
    if (layer == NULL) { return; }

    if (layer->type == Layer::BITMAP)
    {
        //int index = ((LayerImage*)layer)->getLastIndexAtFrame(editor->currentFrame);
        //if (index == -1) return;
        //BitmapImage* bitmapImage = ((LayerBitmap*)layer)->getLastBitmapImageAtFrame(editor->currentFrame, 0);
        //if (bitmapImage == NULL) { qDebug() << "NULL image pointer!" << editor->currentLayer << editor->currentFrame;  return; }

        if (currentTool()->type() == ERASER)
        {
            QPen pen2 = QPen(QBrush(QColor(255, 255, 255, 255)), currentWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            bufferImg->drawLine(lastPoint, endPoint, pen2, QPainter::CompositionMode_SourceOver, antialiasing);
            int rad = qRound(currentWidth / 2) + 2;
            update(myTempView.mapRect(QRect(lastPoint.toPoint(), endPoint.toPoint()).normalized().adjusted(-rad, -rad, +rad, +rad)));
        }
        if (currentTool()->type() == PENCIL)
        {
            QPen pen2 = QPen(QBrush(m_pEditor->currentColor), currentWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            bufferImg->drawLine(lastPoint, endPoint, pen2, QPainter::CompositionMode_Source, antialiasing);
            int rad = qRound(currentWidth / 2) + 3;
            update(myTempView.mapRect(QRect(lastPoint.toPoint(), endPoint.toPoint()).normalized().adjusted(-rad, -rad, +rad, +rad)));
        }
        if (currentTool()->type() == PEN)
        {
            QPen pen2 = QPen(m_pEditor->currentColor , currentWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            bufferImg->drawLine(lastPoint, endPoint, pen2, QPainter::CompositionMode_SourceOver, antialiasing);
            int rad = qRound(currentWidth / 2) + 3;
            update(myTempView.mapRect(QRect(lastPoint.toPoint(), endPoint.toPoint()).normalized().adjusted(-rad, -rad, +rad, +rad)));
        }
        if (currentTool()->type() == BRUSH)
        {
            qreal opacity = 1.0;
            qreal brushWidth = getTool(BRUSH)->properties.width +  0.5 * getTool(BRUSH)->properties.feather;
            qreal offset = qMax(0.0, getTool(BRUSH)->properties.width - 0.5 * getTool(BRUSH)->properties.feather) / brushWidth;
            if (tabletInUse) { opacity = tabletPressure; }
            if (usePressure) { brushWidth = brushWidth * tabletPressure; }

            qreal distance = 4 * QLineF(endPoint, lastBrushPoint).length();
            qreal brushStep = 0.5 * getTool(BRUSH)->properties.width + 0.5 * getTool(BRUSH)->properties.feather;
            if (usePressure) { brushStep = brushStep * tabletPressure; }
            brushStep = qMax(1.0, brushStep);
            int steps = qRound(distance) / brushStep ;

            for (int i = 0; i < steps; i++)
            {
                QPointF thePoint = lastBrushPoint + (i + 1) * (brushStep) * (endPoint - lastBrushPoint) / distance;
                drawBrush(thePoint, brushWidth, offset, m_pEditor->currentColor, opacity);

                if (i == (steps - 1))
                {
                    lastBrushPoint = thePoint;
                }
            }

            int rad = qRound(brushWidth / 2) + 3;
            update(myTempView.mapRect(QRect(lastPoint.toPoint(), endPoint.toPoint()).normalized().adjusted(-rad, -rad, +rad, +rad)));
        }
    }
    if (layer->type == Layer::VECTOR)
    {
        if (currentTool()->type() == ERASER)
        {
            bufferImg->drawLine(lastPixel, currentPixel, QPen(Qt::white, currentWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin), QPainter::CompositionMode_SourceOver, antialiasing);
            int rad = qRound((currentWidth / 2 + 2) * qAbs(myTempView.m11()));
            update(QRect(lastPixel.toPoint(), endPixel.toPoint()).normalized().adjusted(-rad, -rad, +rad, +rad));
        }
        if (currentTool()->type() == PENCIL)
        {
            bufferImg->drawLine(lastPixel, currentPixel, QPen(m_pEditor->currentColor, 1, Qt::DotLine, Qt::RoundCap, Qt::RoundJoin), QPainter::CompositionMode_SourceOver, antialiasing);
            int rad = qRound((currentWidth / 2 + 2) * qAbs(myTempView.m11()));
            update(QRect(lastPixel.toPoint(), endPixel.toPoint()).normalized().adjusted(-rad, -rad, +rad, +rad));
        }
        if (currentTool()->type() == PEN)
        {
            bufferImg->drawLine(lastPixel, currentPixel, QPen(m_pEditor->currentColor, currentWidth * myTempView.m11(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin), QPainter::CompositionMode_SourceOver, antialiasing);
            int rad = qRound((currentWidth / 2 + 2) * (qAbs(myTempView.m11()) + qAbs(myTempView.m22())));
            update(QRect(lastPixel.toPoint(), endPixel.toPoint()).normalized().adjusted(-rad, -rad, +rad, +rad));
        }
        if (currentTool()->type() == BRUSH)
        {
            bufferImg->drawLine(lastPixel, currentPixel, QPen(Qt::gray, 1, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin), QPainter::CompositionMode_SourceOver, antialiasing);
            int rad = qRound((currentWidth / 2 + 2) * qAbs(myTempView.m11()));
            update(QRect(lastPixel.toPoint(), endPixel.toPoint()).normalized().adjusted(-rad, -rad, +rad, +rad));
        }
    }

    //emit modification();

    lastPixel = endPixel;
    lastPoint = endPoint;
}

void ScribbleArea::drawEyedropperPreview(const QColor colour)
{
    QPixmap cursorPixmap;
    QPainter painter(&cursorPixmap);
    painter.setPen(QPen(Qt::black, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(colour);
    painter.drawRect(10, 10, 20, 20);
    painter.end();
    setCursor(QCursor(cursorPixmap, 5, 5));
    update();
}

void ScribbleArea::drawPolyline()
{
    if (currentTool()->type() == POLYLINE)
    {
        if (mousePoints.size() > 0)
        {
            QPen pen2(m_pEditor->currentColor,
                      getTool( PEN )->properties.width,
                      Qt::SolidLine,
                      Qt::RoundCap,
                      Qt::RoundJoin);
            QPainterPath tempPath = BezierCurve(mousePoints).getSimplePath();
            tempPath.lineTo(currentPoint);
            QRect updateRect = myTempView.mapRect(tempPath.boundingRect().toRect()).adjusted(-10, -10, 10, 10);
            if (m_pEditor->getCurrentLayer()->type == Layer::VECTOR)
            {
                tempPath = myTempView.map(tempPath);
                if (makeInvisible) { pen2.setWidth(0); pen2.setStyle(Qt::DotLine);}
                else { pen2.setWidth(getTool( PEN )->properties.width * myTempView.m11()); }
            }
            bufferImg->clear();
            bufferImg->drawPath(tempPath, pen2, Qt::NoBrush, QPainter::CompositionMode_SourceOver, antialiasing);
            update(updateRect);
            //update( QRect(lastPixel.toPoint(), currentPixel.toPoint()).normalized() );
            //bufferImg->drawRect(tempPath.boundingRect().toRect());
            //update( QRect(lastPixel.toPoint()-QPoint(10,10), lastPixel.toPoint()+QPoint(10,10)) );
            //update();
        }
    }
}

void ScribbleArea::endPolyline()
{
    Layer *layer = m_pEditor->getCurrentLayer();
    if (layer == NULL) { return; }
    if (layer->type == Layer::VECTOR)
    {
        BezierCurve curve = BezierCurve(mousePoints);
        if (makeInvisible) { curve.setWidth(0); }
        else { curve.setWidth(getTool( PEN )->properties.width); }
        curve.setColourNumber(getTool( PEN )->properties.colourNumber);
        curve.setVariableWidth(false);
        curve.setInvisibility(makeInvisible);
        //curve.setSelected(true);
        ((LayerVector *)layer)->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0)->addCurve(curve, qAbs(myTempView.m11()));
    }
    if (layer->type == Layer::BITMAP)
    {
        drawPolyline();
        BitmapImage *bitmapImage = ((LayerBitmap *)layer)->getLastBitmapImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
        bitmapImage->paste(bufferImg);
    }
    bufferImg->clear();
    while (!mousePoints.isEmpty()) { mousePoints.removeAt(0); } // empty the mousePoints
    setModified(m_pEditor->m_nCurrentLayerIndex, m_pEditor->m_nCurrentFrameIndex);
}

void ScribbleArea::resizeEvent(QResizeEvent *event)
{
    //resize( size() );
    QWidget::resizeEvent(event);
    canvas = QPixmap(size());
    recentre();
    updateAllFrames();
}

void ScribbleArea::zoom()
{
    centralView.scale(1.2, 1.2);
    setView();
    updateAllFrames();
}

void ScribbleArea::zoom1()
{
    centralView.scale(0.8, 0.8);
    setView();
    updateAllFrames();
}

void ScribbleArea::rotatecw()
{
    centralView.rotate(20);
    setView();
    updateAllFrames();
}

void ScribbleArea::rotateacw()
{
    centralView.rotate(-20);
    setView();
    updateAllFrames();
}


void ScribbleArea::recentre()
{
    centralView = QMatrix(1, 0, 0, 1, 0.5 * width(), 0.5 * height());
    setView();
    QPixmapCache::clear();
    update();
}

void ScribbleArea::setMyView(QMatrix view)
{
    myView = view;
}

QMatrix ScribbleArea::getMyView()
{
    return myView;
}

void ScribbleArea::setView()
{
    setView(getView());
}

void ScribbleArea::setView(QMatrix view)
{
    for (int i = 0; i < m_pEditor->object->getLayerCount() ; i++)
    {
        Layer *layer = m_pEditor->object->getLayer(i);
        if (layer->type == Layer::VECTOR)
        {
            ((LayerVector *)layer)->setView(view * centralView);
        }
    }
    myTempView = view * centralView;
}

void ScribbleArea::resetView()
{
    m_pEditor->resetMirror();
    myView.reset();
    myTempView = myView * centralView;
    recentre();
}

QMatrix ScribbleArea::getView()
{
    Layer *layer = m_pEditor->getCurrentLayer();
    if (layer == NULL)
    {
        return QMatrix(); // TODO: error
    }
    if (layer->type == Layer::CAMERA)
    {
        return ((LayerCamera *)layer)->getViewAtFrame(m_pEditor->m_nCurrentFrameIndex);
        qDebug() << "viewCamera" << ((LayerCamera *)layer)->getViewAtFrame(m_pEditor->m_nCurrentFrameIndex);
    }
    else
    {
        return myView;
    }
}

QRectF ScribbleArea::getViewRect()
{
    QRectF rect =  QRectF(-width() / 2, -height() / 2, width(), height());
    Layer *layer = m_pEditor->getCurrentLayer();
    if (layer == NULL) { return rect; }
    if (layer->type == Layer::CAMERA)
    {
        return ((LayerCamera *)layer)->getViewRect();
    }
    else
    {
        return rect;
    }
}

QPointF ScribbleArea::getCentralPoint()
{
    return myTempView.inverted().map(QPoint(width() / 2, height() / 2));
}

void ScribbleArea::calculateSelectionRect()
{
    selectionTransformation.reset();
    Layer *layer = m_pEditor->getCurrentLayer();
    if (layer == NULL) { return; }
    if (layer->type == Layer::VECTOR)
    {
        VectorImage *vectorImage = ((LayerVector *)layer)->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
        vectorImage->calculateSelectionRect();
        setSelection(vectorImage->getSelectionRect(), true);
    }
}

void ScribbleArea::calculateSelectionTransformation()
{
    qreal c1x, c1y , c2x, c2y, scaleX, scaleY;
    c1x = 0.5 * (myTempTransformedSelection.left() + myTempTransformedSelection.right());
    c1y = 0.5 * (myTempTransformedSelection.top() + myTempTransformedSelection.bottom());
    c2x = 0.5 * (mySelection.left() + mySelection.right());
    c2y = 0.5 * (mySelection.top() + mySelection.bottom());
    if (mySelection.width() == 0) { scaleX = 1.0; }
    else { scaleX = myTempTransformedSelection.width() / mySelection.width(); }
    if (mySelection.height() == 0) { scaleY = 1.0; }
    else { scaleY = myTempTransformedSelection.height() / mySelection.height(); }
    selectionTransformation.reset();
    selectionTransformation.translate(c1x, c1y);
    selectionTransformation.scale(scaleX, scaleY);
    selectionTransformation.translate(-c2x, -c2y);
    //modification();
}

void ScribbleArea::paintTransformedSelection()
{
    Layer *layer = m_pEditor->getCurrentLayer();
    if (layer == NULL)
    {
        return;
    }

    if (somethingSelected)    // there is something selected
    {
        if (layer->type == Layer::BITMAP && (myTransformedSelection != mySelection))
        {
            //backup();
            BitmapImage *bitmapImage = ((LayerBitmap *)layer)->getLastBitmapImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
            if (bitmapImage == NULL)
            {
                qDebug() << "NULL image pointer!"
                         << m_pEditor->m_nCurrentLayerIndex
                         << m_pEditor->m_nCurrentFrameIndex;
                return;
            }

            bool smoothTransform = false;
            if (myTransformedSelection.width() != mySelection.width() || myTransformedSelection.height() != mySelection.height()) { smoothTransform = true; }
            BitmapImage selectionClip = bitmapImage->copy(mySelection.toRect());
            selectionClip.transform(myTransformedSelection, smoothTransform);
            bitmapImage->clear(mySelection.toRect());
            bitmapImage->paste(&selectionClip);
        }
        if (layer->type == Layer::VECTOR)
        {
            // vector transformation
            LayerVector *layerVector = (LayerVector *)layer;
            VectorImage *vectorImage = layerVector->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
            vectorImage->applySelectionTransformation();
            selectionTransformation.reset();
        }
        setModified(m_pEditor->m_nCurrentLayerIndex, m_pEditor->m_nCurrentFrameIndex);
    }
}

void ScribbleArea::setSelection(QRectF rect, bool trueOrFalse)
{
    mySelection = rect;
    myTransformedSelection = rect;
    myTempTransformedSelection = rect;
    somethingSelected = trueOrFalse;
    displaySelectionProperties();
}

void ScribbleArea::displaySelectionProperties()
{
    Layer *layer = m_pEditor->getCurrentLayer();
    if (layer == NULL) { return; }
    if (layer->type == Layer::VECTOR)
    {
        LayerVector *layerVector = (LayerVector *)layer;
        VectorImage *vectorImage = layerVector->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
        //vectorImage->applySelectionTransformation();
        if (currentTool()->type() == MOVE)
        {
            int selectedCurve = vectorImage->getFirstSelectedCurve();
            if (selectedCurve != -1)
            {
                m_pEditor->setWidth(vectorImage->curve[selectedCurve].getWidth());
                m_pEditor->setFeather(vectorImage->curve[selectedCurve].getFeather());
                m_pEditor->setInvisibility(vectorImage->curve[selectedCurve].isInvisible());
                m_pEditor->setPressure(vectorImage->curve[selectedCurve].getVariableWidth());
                m_pEditor->selectVectorColourNumber(vectorImage->curve[selectedCurve].getColourNumber());
            }

            int selectedArea = vectorImage->getFirstSelectedArea();
            if (selectedArea != -1)
            {
                m_pEditor->selectVectorColourNumber(vectorImage->area[selectedArea].colourNumber);
                //editor->setFeather(vectorImage->area[selectedArea].getFeather());
            }
        }
    }
}

void ScribbleArea::selectAll()
{
    offset.setX(0);
    offset.setY(0);
    Layer *layer = m_pEditor->getCurrentLayer();
    if (layer == NULL) { return; }
    if (layer->type == Layer::BITMAP)
    {
        setSelection(myTempView.inverted().mapRect(QRect(-2, -2, width() + 3, height() + 3)), true); // TO BE IMPROVED
    }
    if (layer->type == Layer::VECTOR)
    {
        VectorImage *vectorImage = ((LayerVector *)layer)->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0);
        vectorImage->selectAll();
        setSelection(vectorImage->getSelectionRect(), true);
    }
    updateFrame();
}

void ScribbleArea::deselectAll()
{
    offset.setX(0);
    offset.setY(0);
    selectionTransformation.reset();
    mySelection.setRect(10, 10, 20, 20);
    myTransformedSelection.setRect(10, 10, 20, 20);
    myTempTransformedSelection.setRect(10, 10, 20, 20);

    Layer *layer = m_pEditor->getCurrentLayer();
    if (layer == NULL) { return; }
    if (layer->type == Layer::VECTOR)
    {
        ((LayerVector *)layer)->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0)->deselectAll();
        if (currentTool()->type() == MOVE)
        {
            m_pEditor->setWidth(-1);
            m_pEditor->setInvisibility(-1);
            m_pEditor->setPressure(-1);
        }
    }
    somethingSelected = false;
    bufferImg->clear();
    vectorSelection.clear();
    while (!mousePoints.isEmpty()) { mousePoints.removeAt(0); } // empty the mousePoints
    updateFrame();
}

void ScribbleArea::toggleOnionNext(bool checked)
{
    onionNext = checked;
    updateAllFrames();
    emit onionNextChanged(onionNext);
}

void ScribbleArea::toggleOnionPrev(bool checked)
{
    onionPrev = checked;
    updateAllFrames();
    emit onionPrevChanged(onionPrev);
}

void ScribbleArea::floodFill(VectorImage *vectorImage, QPoint point, QRgb targetColour, QRgb replacementColour, int tolerance)
{
    bool invertible;

    QPointF initialPoint = myTempView.inverted(&invertible).map(QPointF(point));

    // Step 1: peforms a standard (pixel-based) flood fill, and finds the vertices on the contour of the filled area
    qreal tol = 8.0 / qAbs(myTempView.m11()); // tolerance for finding vertices along the contour of the flood-filled area
    qreal tol2 = 1.5 / qAbs(myTempView.m11()); // tolerance for connecting contour vertices from different curves // should be small as close points of different curves are supposed to coincide
    QList<QPoint> queue; // queue all the pixels of the filled area (as they are found)
    QList<QPoint> contourPixels; // list of the pixels near the contour of the filled area
    int j, k;
    bool condition;
    //vectorImage->update(true, showThinLines); // update the vector image with simplified curves (all width=1)
    QImage *targetImage = new QImage(size(), QImage::Format_ARGB32_Premultiplied);
    vectorImage->outputImage(targetImage, size(), myTempView, true, showThinLines, 1.0, true, false); // the target image is the vector image with simplified curves (all width=1)
    //QImage* replaceImage = &bufferImg;
    QImage *replaceImage = new QImage(size(), QImage::Format_ARGB32_Premultiplied);
    QList<VertexRef> points = vectorImage->getAllVertices(); // refs of all the points
    QList<VertexRef> boxPoints; // refs of points inside the bounding box
    QList<VertexRef> contourPoints; // refs of points near the contour pixels
    QList<VertexRef> vertices;
    if (BitmapImage::rgbDistance(targetImage->pixel(point.x(), point.y()), targetColour) > tolerance) { return; }
    queue.append(point);
    int boxLeft = point.x();
    int boxRight = point.x();
    int boxTop = point.y();
    int boxBottom = point.y();
    // ----- flood fill and remember the contour pixels -> contourPixels
    // ----- from the standard flood fill algorithm
    // ----- http://en.wikipedia.org/wiki/Flood_fill
    j = -1;
    k = 1;
    for (int i = 0; i < queue.size(); i++)
    {
        point = queue.at(i);
        if (replaceImage->pixel(point.x(), point.y()) != replacementColour  && BitmapImage::rgbDistance(targetImage->pixel(point.x(), point.y()), targetColour) < tolerance)
        {
            //image.setPixel( point.x(), point.y(), replacementColour);
            j = -1;
            condition = (point.x() + j > 0);
            while (replaceImage->pixel(point.x() + j, point.y()) != replacementColour  && BitmapImage::rgbDistance(targetImage->pixel(point.x() + j, point.y()), targetColour) < tolerance && condition)
            {
                j = j - 1;
                condition = (point.x() + j > 0);
            }
            if (!condition) { floodFillError(1); return; }
            if (BitmapImage::rgbDistance(targetImage->pixel(point.x() + j, point.y()), targetColour) >= tolerance)      // bumps into the contour
            {
                contourPixels.append(point + QPoint(j, 0));
            }

            k = 1;
            condition = (point.x() + k < targetImage->width() - 1);
            while (replaceImage->pixel(point.x() + k, point.y()) != replacementColour  && BitmapImage::rgbDistance(targetImage->pixel(point.x() + k, point.y()), targetColour) < tolerance && condition)
            {
                k = k + 1;
                condition = (point.x() + k < targetImage->width() - 1);
            }
            if (!condition) { floodFillError(1); return; }
            if (BitmapImage::rgbDistance(targetImage->pixel(point.x() + k, point.y()), targetColour) >= tolerance)      // bumps into the contour
            {
                contourPixels.append(point + QPoint(k, 0));
            }
            if (point.x() + k > boxRight) { boxRight = point.x() + k; }
            if (point.x() + j < boxLeft) { boxLeft = point.x() + j; }
            for (int x = j + 1; x < k; x++)
            {
                replaceImage->setPixel(point.x() + x, point.y(), replacementColour);
                if (point.y() - 1 > 0 && queue.size() < targetImage->height() * targetImage->width())
                {
                    if (replaceImage->pixel(point.x() + x, point.y() - 1) != replacementColour)
                    {
                        if (BitmapImage::rgbDistance(targetImage->pixel(point.x() + x, point.y() - 1), targetColour) < tolerance)
                        {
                            queue.append(point + QPoint(x, -1));
                            if (point.y() - 1 < boxBottom) { boxBottom = point.y() - 1; }
                        }
                        else   // bumps into the contour
                        {
                            contourPixels.append(point + QPoint(x, -1));
                        }
                    }
                }
                else { floodFillError(1); return; }
                if (point.y() + 1 < targetImage->height() && queue.size() < targetImage->height() * targetImage->width())
                {
                    if (replaceImage->pixel(point.x() + x, point.y() + 1) != replacementColour)
                    {
                        if (BitmapImage::rgbDistance(targetImage->pixel(point.x() + x, point.y() + 1), targetColour) < tolerance)
                        {
                            queue.append(point + QPoint(x, 1));
                            if (point.y() + 1 > boxTop) { boxTop = point.y() + 1; }
                        }
                        else   // bumps into the contour
                        {
                            contourPixels.append(point + QPoint(x, 1));
                        }
                    }
                }
                else { floodFillError(1); return; }
            }
        }
    }
    // --- finds the bounding box of the filled area, and all the points contained inside (+ 1*tol)  -> boxPoints
    QPointF mBoxTopRight = myTempView.inverted(&invertible).map(QPointF(qMax(boxLeft, boxRight) + 1 * tol, qMax(boxTop, boxBottom) + 1 * tol));
    QPointF mBoxBottomLeft = myTempView.inverted(&invertible).map(QPointF(qMin(boxLeft, boxRight) - 1 * tol, qMin(boxTop, boxBottom) - 1 * tol));
    QRectF boundingBox = QRectF(mBoxBottomLeft.x() - 1, mBoxBottomLeft.y() - 1, qAbs(mBoxBottomLeft.x() - mBoxTopRight.x()) + 2, qAbs(mBoxBottomLeft.y() - mBoxTopRight.y()) + 2);
    debugRect = QRectF(0, 0, 0, 0);
    debugRect = boundingBox;
    for (int l = 0; l < points.size(); l++)
    {
        QPointF mPoint = vectorImage->getVertex(points.at(l));
        if (boundingBox.contains(mPoint))
        {
            // -----
            //vectorImage->setSelected(points.at(l), true);
            boxPoints.append(points.at(l));
        }
    }
    // ---- finds the points near the contourPixels -> contourPoints
    for (int i = 0; i < contourPixels.size(); i++)
    {
        QPointF mPoint = myTempView.inverted(&invertible).map(QPointF(contourPixels.at(i)));
        vertices = vectorImage->getAndRemoveVerticesCloseTo(mPoint, tol, &boxPoints);
        //contourPoints << vertices;
        for (int m = 0; m < vertices.size(); m++) // for each ?
        {
            contourPoints.append(vertices.at(m));
        }
    }
    // ---- points of sharp peaks may be missing in contourPoints ---> we correct for that
    for (int i = 0; i < contourPoints.size(); i++)
    {
        VertexRef theNextVertex = contourPoints[i].nextVertex();
        if (! contourPoints.contains(theNextVertex))     // if the next vertex is not in the list of contour points
        {
            if (contourPoints.contains(theNextVertex.nextVertex()))       // but the next-next vertex is...
            {
                contourPoints.append(theNextVertex);
                //qDebug() << "----- found SHARP point (type 1a) ------";
            }
            QList<VertexRef> closePoints = vectorImage->getVerticesCloseTo(theNextVertex, tol2);
            for (int j = 0; j < closePoints.size(); j++)
            {
                if (closePoints[j] != theNextVertex)     // ...or a point connected to the next vertex is
                {
                    if (contourPoints.contains(closePoints[j].nextVertex()) || contourPoints.contains(closePoints[j].prevVertex()))
                    {
                        contourPoints.append(theNextVertex);
                        contourPoints.append(closePoints[j]);
                        //qDebug() << "----- found SHARP point (type 2a) ------";
                    }
                }
            }
        }
        VertexRef thePreviousVertex = contourPoints[i].prevVertex();
        if (! contourPoints.contains(thePreviousVertex))     // if the previous vertex is not in the list of contour points
        {
            if (contourPoints.contains(thePreviousVertex.prevVertex()))       // but the prev-prev vertex is...
            {
                contourPoints.append(thePreviousVertex);
                //qDebug() << "----- found SHARP point (type 1b) ------";
            }
            QList<VertexRef> closePoints = vectorImage->getVerticesCloseTo(thePreviousVertex, tol2);
            for (int j = 0; j < closePoints.size(); j++)
            {
                if (closePoints[j] != thePreviousVertex)     // ...or a point connected to the previous vertex is
                {
                    if (contourPoints.contains(closePoints[j].nextVertex()) || contourPoints.contains(closePoints[j].prevVertex()))
                    {
                        contourPoints.append(thePreviousVertex);
                        contourPoints.append(closePoints[j]);
                        //qDebug() << "----- found SHARP point (type 2b) ------";
                    }
                }
            }
        }
    }

    // 1 --- stop here (for debugging purpose)
    /*qDebug() << "CONTOUR POINTS:";
    for(int i=0; i < contourPoints.size(); i++) {
        qDebug() << "(" << contourPoints.at(i).curveNumber << "," << contourPoints.at(i).vertexNumber << ")";
    }*/
    // -----
    vectorImage->setSelected(contourPoints, true);
    update();

    // 2 --- or continue

    // Step 2: finds closed paths among the selected vertices: we start from a vertex and build a tree of connected vertices
    //while (contourPoints.size() > 0) {
    QList<VertexRef> tree;
    QList<int> fatherNode; // given the index in tree (of a vertex), return the index (in tree) of its father vertex; this will define the tree structure
    QList<int> leaves; // list of indices in tree which correspond to end of branches (leaves)

    // Step 2.1: build tree
    int rootIndex = -1;
    bool rootIndexFound = false;
    while (!rootIndexFound && rootIndex < contourPoints.size() - 1)
    {
        rootIndex++;
        if (vectorImage->getVerticesCloseTo(vectorImage->getVertex(contourPoints.at(rootIndex)), tol2, &contourPoints).size() > 1)
        {
            // this point is connected!
            rootIndexFound = true;
        }
    }
    if (!rootIndexFound) { floodFillError(3); return; }
    tree << contourPoints.at(rootIndex);
    fatherNode.append(-1);
    //leaves << 0;
    contourPoints.removeAt(rootIndex);
    VertexRef vertex0 = tree.at(0);
    //qDebug() << "ROOT = " <<  vertex0.curveNumber << "," << vertex0.vertexNumber;
    j = 0;
    bool success = false;
    int counter = 0;
    while (!success && j > -1 && counter < 1000)
    {
        counter++;
        //qDebug() << "------";
        VertexRef vertex  = tree.at(j);
        //qDebug() << j << "/" << tree.size() << "   " << vertex.curveNumber << "," << vertex.vertexNumber << "->" << fatherNode.at(j);
        int index1 = contourPoints.indexOf(vertex.nextVertex());
        if (index1 != -1)
        {
            //qDebug() << "next vertex";
            tree.append(vertex.nextVertex());
            fatherNode.append(j);
            contourPoints.removeAt(index1);
            j = tree.size() - 1;
        }
        else
        {
            int index2 = contourPoints.indexOf(vertex.prevVertex());
            if (index2 != -1)
            {
                // qDebug() << "previous vertex";
                tree.append(vertex.prevVertex());
                fatherNode.append(j);
                contourPoints.removeAt(index2);
                j = tree.size() - 1;
            }
            else
            {
                QList<VertexRef> pointsNearby = vectorImage->getVerticesCloseTo(vectorImage->getVertex(vertex), tol2, &contourPoints);
                if (pointsNearby.size() > 0)
                {
                    //qDebug() << "close vertex";
                    tree << pointsNearby.at(0);
                    fatherNode.append(j);
                    contourPoints.removeAt(contourPoints.indexOf(pointsNearby.at(0)));
                    j = tree.size() - 1;
                }
                else
                {
                    qreal dist = vectorImage->getDistance(vertex, vertex0);
                    //qDebug() << "is it a leave ? " << j << "dist = " << dist << "-" << tol2;
                    if (((vertex.curveNumber == vertex0.curveNumber) && (qAbs(vertex.vertexNumber - vertex0.vertexNumber) == 1))  || (dist < tol2))
                    {
                        // we found a leaf close to the root of the tree - does the closed path contain the initial point?
                        QList<VertexRef> closedPath;
                        int pathIndex = j;
                        if (dist > 0) { closedPath.prepend(vertex0); }
                        closedPath.prepend(tree.at(pathIndex));
                        while ((pathIndex = fatherNode.at(pathIndex)) != -1)
                        {
                            closedPath.prepend(tree.at(pathIndex));
                        }
                        BezierArea newArea = BezierArea(closedPath, getTool(BRUSH)->properties.colourNumber);
                        vectorImage->updateArea(newArea);
                        if (newArea.path.contains(initialPoint))
                        {
                            vectorImage->addArea(newArea);
                            //qDebug() << "Yes!";
                            success = true;
                        }
                        else
                        {
                            //qDebug() << "No! almost";
                            j = fatherNode.at(j);
                        }
                    }
                    else
                    {
                        //qDebug() << "No!";
                        leaves << j;
                        j = fatherNode.at(j);
                    }
                }
            }
        }

        //
    }

    if (!success) { floodFillError(2); return; }
    //qDebug() << "failure!" << contourPoints.size();
    replaceImage->fill(qRgba(0, 0, 0, 0));
    deselectAll();

    // -- debug --- (display tree)
    /*for(int indexm=0; indexm < tree.size(); indexm++) {
        qDebug() << indexm  << ") " << tree.at(indexm).curveNumber << "," << tree.at(indexm).vertexNumber << " -> " << fatherNode.at(indexm);
        //if (leaves.contains(indexm)) vectorImage->setSelected( tree.at(indexm), true);
        vectorImage->setSelected( tree.at(indexm), true);
        update();
        //sleep( 1 );
        QMessageBox::warning(this, tr("My Application"), tr("all the tree points"), QMessageBox::Ok, QMessageBox::Ok);
    }*/
    delete targetImage;
    update();
}

void ScribbleArea::floodFillError(int errorType)
{
    QString message, error;
    if (errorType == 1) { message = "There is a gap in your drawing (or maybe you have zoomed too much)."; }
    if (errorType == 2 || errorType == 3) message = "Sorry! This doesn't always work."
                "Please try again (zoom a bit, click at another location... )<br>"
                "if it doesn't work, zoom a bit and check that your paths are connected by pressing F1.).";

    if (errorType == 1) { error = "Out of bound."; }
    if (errorType == 2) { error = "Could not find a closed path."; }
    if (errorType == 3) { error = "Could not find the root index."; }
    QMessageBox::warning(this, tr("Flood fill error"), message + "<br><br>Error: " + error, QMessageBox::Ok, QMessageBox::Ok);
    bufferImg->clear();
    deselectAll();
}

BaseTool *ScribbleArea::currentTool()
{
    return m_currentTool;
}

BaseTool *ScribbleArea::getTool(ToolType eToolMode)
{
    return m_toolSetHash.value(eToolMode);
}

void ScribbleArea::setCurrentTool(ToolType eToolMode)
{
    if (eToolMode != currentTool()->type())
    {
        qDebug() << "Set Current Tool" << BaseTool::TypeName(eToolMode);
        // XXX tool->setActive()
        if (currentTool()->type() == MOVE) {
            paintTransformedSelection();
            deselectAll();
        }
        if (currentTool()->type() == POLYLINE)
        {
            escape();
        }
        m_currentTool = getTool(eToolMode);
    }
    // --- change cursor ---
    setCursor(currentTool()->cursor());
}

void ScribbleArea::switchTool(ToolType type)
{
    switch (type)
    {
    case PENCIL:
        emit pencilOn();
        break;
    case ERASER:
        emit eraserOn();
        break;
    case SELECT:
        emit selectOn();
        break;
    case MOVE:
        emit moveOn();
        break;
    case HAND:
        emit handOn();
        break;
    case SMUDGE:
        emit smudgeOn();
        break;
    case PEN:
        emit penOn();
        break;
    case POLYLINE:
        emit polylineOn();
        break;
    case BUCKET:
        emit bucketOn();
        break;
    case EYEDROPPER:
        emit eyedropperOn();
        break;
    case BRUSH:
        emit brushOn();
        break;
    default:
        break;
    }
}

QList<BaseTool *> ScribbleArea::getTools()
{
    return m_toolSetHash.values();
}

void ScribbleArea::deleteSelection()
{
    if (somethingSelected)      // there is something selected
    {
        m_pEditor->backup(tr("DeleteSel"));
        Layer *layer = m_pEditor->getCurrentLayer();
        if (layer == NULL) { return; }
        closestCurves.clear();
        if (layer->type == Layer::VECTOR) { ((LayerVector *)layer)->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0)->deleteSelection(); }
        if (layer->type == Layer::BITMAP) { ((LayerBitmap *)layer)->getLastBitmapImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0)->clear(mySelection); }
        updateAllFrames();
    }
}

void ScribbleArea::clearImage()
{
    m_pEditor->backup(tr("ClearImg"));
    Layer *layer = m_pEditor->getCurrentLayer();
    if (layer == NULL) { return; }
    if (layer->type == Layer::VECTOR) { ((LayerVector *)layer)->getLastVectorImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0)->clear(); }
    if (layer->type == Layer::BITMAP) { ((LayerBitmap *)layer)->getLastBitmapImageAtFrame(m_pEditor->m_nCurrentFrameIndex, 0)->clear(); }
    //emit modification();
    //update();
    setModified(m_pEditor->m_nCurrentLayerIndex, m_pEditor->m_nCurrentFrameIndex);
}

void ScribbleArea::toggleThinLines()
{
    showThinLines = !showThinLines;
    emit thinLinesChanged(showThinLines);
    setView(myView);
    updateAllFrames();
}

void ScribbleArea::toggleOutlines()
{
    simplified = !simplified;
    emit outlinesChanged(simplified);
    setView(myView);
    updateAllFrames();
}

void ScribbleArea::toggleMirror()
{
    myView =  myView * QMatrix(-1, 0, 0, 1, 0, 0);
    myTempView = myView * centralView;
    setView(myView);
    updateAllFrames();
}

void ScribbleArea::toggleMirrorV()
{
    myView =  myView * QMatrix(1, 0, 0, -1, 0, 0);
    myTempView = myView * centralView;
    setView(myView);
    updateAllFrames();
}

void ScribbleArea::toggleShowAllLayers()
{
    showAllLayers++;
    if (showAllLayers == 3)
    {
        showAllLayers = 0;
    }
    //emit showAllLayersChanged(showAllLayers);
    setView(myView);
    updateAllFrames();
}

void ScribbleArea::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        if (event->delta() > 0) //+ve for wheel up
        {
            zoom();
        }
        else
        {
            zoom1();
        }
    }
}

void ScribbleArea::setPrevMode()
{
    setCurrentTool(prevMode);
    switchTool(prevMode);
}
