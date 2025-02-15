#ifndef ABSTRACTCOMPLEXLOGIC_H
#define ABSTRACTCOMPLEXLOGIC_H

#include "../IBaseComponent.h"

///
/// \brief The AbstractComplexLogic class is the super class for all built-in complex components (excluding gates)
///
class AbstractComplexLogic : public IBaseComponent
{
    Q_OBJECT
public:
    /// \brief Constructor for the abstract complex logic component
    /// \param pCoreLogic: Pointer to the core logic, used to connect the component's signals and slots
    /// \param pLogicCell: Pointer to the associated logic cell
    /// \param pInputCount: The amount of inputs to this component
    /// \param pOutputCount: The amount of outputs of this component
    /// \param pDirection: The direction of the component
    /// \param pTopInputCount: The input number up to which the inputs should be drawn on top of the component (exclusive)
    /// \param pStretchTwoPins: If enabled, pin spacing will be increased for components with two inputs or outputs
    /// \param pTrapezoidShape: If true, the component will have a trapezoid shape instead of the rectangular
    AbstractComplexLogic(const CoreLogic* pCoreLogic, const std::shared_ptr<LogicBaseCell>& pLogicCell, uint8_t pInputCount, uint8_t pOutputCount,
                         Direction pDirection, uint8_t pTopInputCount = 0, bool pStretchTwoPins = true, bool pTrapezoidShape = false);

    /// \brief Clone function for the component
    /// \param pCoreLogic: Pointer to the core logic, used to connect the component's signals and slots
    /// \return A pointer to the new component
    virtual IBaseComponent* CloneBaseComponent(const CoreLogic* pCoreLogic) const override = 0;

    /// \brief Defines the bounding rect of this component
    /// \return A rectangle describing the bounding rect
    QRectF boundingRect(void) const override;

    /// \brief Sets the Z-value to its defined value, to reset it after components have been copied
    void ResetZValue(void) override;

    /// \brief Saves the dats of this component to the given JSON object
    /// \return The JSON object with the component data
    virtual QJsonObject GetJson(void) const override = 0;

    /// \brief Gets the minimum version compatible with this component
    /// \return the minimum version
    virtual SwVersion GetMinVersion(void) const override = 0;

protected:
    /// \brief Paints the abstract complex logic component
    /// \param pPainter: The painter to use
    /// \param pItem: Contains drawing parameters
    /// \param pWidget: Unused, the widget that is been painted on
    void paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pItem, QWidget *pWidget) override;

    /// \brief Draws the component in- and output pins and inversion circles for a right-facing component
    /// \param pPainter: The painter to use
    /// \param pItem: Contains drawing parameters
    void DrawComponentDetailsRight(QPainter *pPainter, const QStyleOptionGraphicsItem *pItem);

    /// \brief Draws the component in- and output pins and inversion circles for a down-facing component
    /// \param pPainter: The painter to use
    /// \param pItem: Contains drawing parameters
    void DrawComponentDetailsDown(QPainter *pPainter, const QStyleOptionGraphicsItem *pItem);

    /// \brief Draws the component in- and output pins and inversion circles for a left-facing component
    /// \param pPainter: The painter to use
    /// \param pItem: Contains drawing parameters
    void DrawComponentDetailsLeft(QPainter *pPainter, const QStyleOptionGraphicsItem *pItem);

    /// \brief Draws the component in- and output pins and inversion circles for an up-facing component
    /// \param pPainter: The painter to use
    /// \param pItem: Contains drawing parameters
    void DrawComponentDetailsUp(QPainter *pPainter, const QStyleOptionGraphicsItem *pItem);

    /// \brief Draws the connector description strings or clock symbol for inputs labeled ">" (right facing component)
    /// \param pPainter: The painter to use
    /// \param pItem: Contains drawing parameters
    void DrawConnectorDescriptionsRight(QPainter *pPainter, const QStyleOptionGraphicsItem *pItem);

    /// \brief Draws the connector description strings or clock symbol for inputs labeled ">" (down facing component)
    /// \param pPainter: The painter to use
    /// \param pItem: Contains drawing parameters
    void DrawConnectorDescriptionsDown(QPainter *pPainter, const QStyleOptionGraphicsItem *pItem);

    /// \brief Draws the connector description strings or clock symbol for inputs labeled ">" (left facing component)
    /// \param pPainter: The painter to use
    /// \param pItem: Contains drawing parameters
    void DrawConnectorDescriptionsLeft(QPainter *pPainter, const QStyleOptionGraphicsItem *pItem);

    /// \brief Draws the connector description strings or clock symbol for inputs labeled ">" (up facing component)
    /// \param pPainter: The painter to use
    /// \param pItem: Contains drawing parameters
    void DrawConnectorDescriptionsUp(QPainter *pPainter, const QStyleOptionGraphicsItem *pItem);

    /// \brief Creates logic connectors at all in- and output pin points
    virtual void SetLogicConnectors(void);

    /// \brief Sets the current pen to the pen to use for connector pins
    /// \param pPainter: The painter to use
    /// \param pState: The logic state of the pin
    /// \param pSelected: Whether the component is selected or not
    void SetConnectorPen(QPainter *pPainter, LogicState pState, bool pSelected);

    /// \brief Sets the current pen and brush to the pen to use for inversion circles
    /// \param pPainter: The painter to use
    /// \param pState: The logic state of the inversion circle
    /// \param pSelected: Whether the component is selected or not
    void SetInversionPen(QPainter *pPainter, LogicState pState, bool pSelected);

    /// \brief Sets the current pen and brush to the pen to use for clock input triangles
    /// \param pPainter: The painter to use
    /// \param pState: The logic state of the clock input
    /// \param pSelected: Whether the component is selected or not
    void SetClockInputPen(QPainter *pPainter, LogicState pState, bool pSelected);

protected:
    QString mComponentText;

    uint8_t mInputCount;
    uint8_t mOutputCount;
    Direction mDirection;

    uint8_t mInputsSpacing;
    uint8_t mOutputsSpacing;

    uint8_t mTopInputCount;

    std::vector<QString> mInputLabels;
    std::vector<QString> mOutputLabels;

    uint8_t mDescriptionFontSize;

    QPolygon mTrapezoid;
    bool mTrapezoidShape;
    uint8_t mInputsTrapezoidOffset;
    uint8_t mOutputsTrapezoidOffset;
};

#endif // ABSTRACTCOMPLEXLOGIC_H
