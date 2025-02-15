#ifndef VIEW_H
#define VIEW_H

#include "CoreLogic.h"
#include "Configuration.h"

#include "Gui/PieMenu.h"
#include "QtAwesome.h"

#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QPropertyAnimation>
#include <QTabWidget>
#include <QButtonGroup>
#include <QGroupBox>
#include <QGridLayout>

// forward declarations
QT_BEGIN_NAMESPACE
class QLabel;
class QToolButton;
QT_END_NAMESPACE

class View;
class CoreLogic;

///
/// \brief The GraphicsView class extends QGraphicsView, which contains the main scene
///
class GraphicsView : public QGraphicsView
{
    Q_OBJECT
public:
    /// \brief Constructor for the GraphicsView class
    /// \param pView: The frame to contain the GraphicsView
    /// \param pCoreLogic: Reference to the core logic
    GraphicsView(View &pView, CoreLogic &pCoreLogic);

signals:
    /// \brief Emitted when the left mouse button is pressed without the CTRL key being down
    /// \param pMappedPos: The mouse position mapped onto the scene
    /// \param pEvent: The mouse event object of the event
    void LeftMouseButtonPressedWithoutCtrlEvent(QPointF pMappedPos, QMouseEvent &pEvent);

public slots:
    /// \brief This slot can be invoked to execute the default behavior for a QGraphicsView::mousePressEvent
    /// \param pEvent: The mouse event to handle
    void OnMousePressedEventDefault(QMouseEvent &pEvent);

protected:
    /// \brief Handles mouse wheel events for zooming
    /// \param pEvent: A mouse wheel event pointer
    void wheelEvent(QWheelEvent *pEvent) override;

    /// \brief Handles mouse press events for panning and editing
    /// \param pEvent: A mouse press event pointer
    void mousePressEvent(QMouseEvent *pEvent) override;

    /// \brief Handles mouse move event for panning and wire drawing
    /// \param pEvent: A mouse move event pointer
    void mouseMoveEvent(QMouseEvent *pEvent) override;

    /// \brief Handles mouse release event for moving components etc.
    /// \param pEvent: A mouse release event pointer
    void mouseReleaseEvent(QMouseEvent *pEvent) override;

    /// \brief Prevents all double click interaction
    /// \param pEvent: The associated double click event
    void mouseDoubleClickEvent(QMouseEvent *pEvent) override;

protected:
    View &mView;
    CoreLogic &mCoreLogic;

    QPoint mPanStart;
    bool mIsLeftMousePressed;
    bool mIsAddingWire;
    bool mIsDragging;
};

///
/// \brief The View class contains the main canvas
///
class View : public QFrame
{
    Q_OBJECT
public:
    /// \brief Creator for the View class
    /// \param pAwesome: Reference to the QtAwesome object
    /// \param pCoreLogic: Reference to the core logic
    View(QtAwesome &pAwesome, CoreLogic &pCoreLogic);

    /// \brief Initializes the GraphicsView and sets up all signal/slot connections
    void Init(void);

    /// \brief Sets the displayed scene to the given scene
    /// \param pScene: Reference to a QGraphicsScene
    void SetScene(QGraphicsScene &pScene);

    /// \brief Gets the main scene
    /// \return Pointer to a QGraphicsScene
    QGraphicsScene* Scene(void) const;

    /// \brief Returns a list of pointers to all scene items
    /// \return A QList of all items in mScene
    QList<QGraphicsItem*> Components(void) const;

    /// \brief Fades out the overlay that indicates that the SW is loading
    void FadeOutProcessingOverlay(void);

    /// \brief Fades in the overlay that indicates that the SW is loading
    void FadeInProcessingOverlay(void);

    /// \brief Resets zoom and position
    void ResetViewport(void);

    /// \brief Getter for the current zoom level
    int32_t GetZoomLevel(void);

    void ShowPieMenu(const QPoint &pPos);

    void UpdatePieMenuIcons(void);

    PieMenu* GetPieMenu(void);

signals:
    /// \brief Emitted when the scene is zoomed in or out
    /// \param pPercentage: The new zoom percentage
    /// \param pValue: The new zoom value
    void ZoomLevelChangedSignal(uint8_t pPercentage, uint32_t pValue);

    void UndoFromPieMenuSignal(void);
    void RedoFromPieMenuSignal(void);

public slots:
    /// \brief Sets the zoom level and updates the displayed percentage
    /// \param pZoomLevel: The new zoom level
    void SetZoom(int32_t pZoomLevel);

    /// \brief Increases the zoom level and updates the displayed percentage
    /// \param pAmount: The amount to increase the zoom by
    void ZoomIn(int32_t pAmount);

    /// \brief Decreases the zoom level and updates the displayed percentage
    /// \param pAmount: The amount to decrease the zoom by
    void ZoomOut(int32_t pAmount);

    /// \brief Disables buttons not available in simulation and disables component selection
    void OnSimulationStart(void);

    /// \brief Enables buttons available for editing and enables component selection
    void OnSimulationStop(void);

protected slots:
    /// \brief Applies the current transform to the GraphicsView and sets the scene background
    void SetupMatrix(void);

protected:
    /// \brief Creates a grid pattern for the canvas background
    /// \param pZoomLevel: The zoom level decides whether to draw the grid or not
    /// \return A pixel map containing a background grid tile
    QPixmap DrawGridPattern(int32_t pZoomLevel);

    void OnPieMenuButtonClicked(int8_t pButtonIndex);

protected:
    GraphicsView mGraphicsView;
    QGraphicsScene *mScene;
    CoreLogic &mCoreLogic;

    QtAwesome &mAwesome;
    QVariantMap mStandardPieMenuIconVariant;
    QVariantMap mDisabledPieMenuIconVariant;

    QGridLayout *mMainLayout;

    // Over-canvas elements

    QWidget *mProcessingOverlay;
    QLabel *mProcessingImage;
    QGridLayout *mProcessingLayout;

    PieMenu *mPieMenu;

    int32_t mZoomLevel = canvas::DEFAULT_ZOOM_LEVEL;
};

#endif // VIEW_H
