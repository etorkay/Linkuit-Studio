#ifndef LOGICBUTTON_H
#define LOGICBUTTON_H

#include "../IBaseComponent.h"
#include "HelperStructures.h"
#include "LogicCells/LogicButtonCell.h"

///
/// \brief The LogicButton class represents a logic button input
///
class LogicButton : public IBaseComponent
{
    Q_OBJECT
public:
    /// \brief Constructor for LogicButton
    /// \param pCoreLogic: Pointer to the core logic
    LogicButton(const CoreLogic* pCoreLogic);

    /// \brief Copy constructor for LogicButton
    /// \param pObj: The object to be copied
    /// \param pCoreLogic: Pointer to the core logic
    LogicButton(const LogicButton& pObj, const CoreLogic* pCoreLogic);

    /// \brief Constructor for loading from JSON
    /// \param pCoreLogic: Pointer to the core logic, used to connect the component's signals and slots
    /// \param pJson: The JSON object to load the component's data from
    LogicButton(const CoreLogic* pCoreLogic, const QJsonObject& pJson);

    /// \brief Clone function for the button component
    /// \param pCoreLogic: Pointer to the core logic, used to connect the component's signals and slots
    /// \return A pointer to the new component
    virtual IBaseComponent* CloneBaseComponent(const CoreLogic* pCoreLogic) const override;

    /// \brief Triggers the button to toggle if in simulation mode
    /// \param pEvent: Pointer to the mouse press event
    void mousePressEvent(QGraphicsSceneMouseEvent *pEvent) override;

    /// \brief Defines the bounding rect of this component
    /// \return A rectangle describing the bounding rect
    QRectF boundingRect(void) const override;

    /// \brief Sets the Z-value to its defined value, to reset it after components have been copied
    void ResetZValue(void) override;

    /// \brief Saves the dats of this component to the given JSON object
    /// \return The JSON object with the component data
    virtual QJsonObject GetJson(void) const override;

    /// \brief Gets the minimum version compatible with this component
    /// \return the minimum version
    virtual SwVersion GetMinVersion(void) const override;

protected:
    /// \brief Paints the button component
    /// \param pPainter: The painter to use
    /// \param pItem: Contains drawing parameters
    /// \param pWidget: Unused, the widget that is been painted on
    void paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pItem, QWidget *pWidget) override;
};

#endif // LOGICBUTTON_H
