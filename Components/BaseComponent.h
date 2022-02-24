#ifndef BASECOMPONENT_H
#define BASECOMPONENT_H

#include "LogicCells/LogicBaseCell.h"

#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

class CoreLogic;

class BaseComponent : public QObject, public QGraphicsItem
{
    Q_OBJECT
    Q_INTERFACES(QGraphicsItem)
public:
    BaseComponent(const CoreLogic* pCoreLogic, std::shared_ptr<LogicBaseCell> pLogicCell);

    virtual BaseComponent* CloneBaseComponent(const CoreLogic* pCoreLogic) const = 0;

    void mousePressEvent(QGraphicsSceneMouseEvent *pEvent) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *pEvent) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *pEvent) override;

    virtual void ResetZValue(void) = 0;

signals:
    void SelectedComponentMovedSignal(QPointF pOffset);

protected:
    uint32_t mWidth;
    uint32_t mHeight;

    QPointF mMoveStartPoint;

    bool mSimulationRunning;

    std::shared_ptr<LogicBaseCell> mLogicCell;
};

#endif // BASECOMPONENT_H
