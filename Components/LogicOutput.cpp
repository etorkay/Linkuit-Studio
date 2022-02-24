#include "LogicOutput.h"
#include "CoreLogic.h"
#include "Configuration.h"

LogicOutput::LogicOutput(const CoreLogic* pCoreLogic):
    BaseComponent(pCoreLogic, std::make_shared<LogicOutputCell>())
{
    setZValue(components::zvalues::OUTPUT);

    mWidth = canvas::GRID_SIZE;
    mHeight = canvas::GRID_SIZE;

    QObject::connect(pCoreLogic, &CoreLogic::SimulationStopSignal, this, [&](){
        std::static_pointer_cast<LogicOutputCell>(mLogicCell)->Shutdown();
    });
}

LogicOutput::LogicOutput(const LogicOutput& pObj, const CoreLogic* pCoreLogic):
    LogicOutput(pCoreLogic)
{
    mWidth = pObj.mWidth;
    mHeight = pObj.mHeight;
    mLogicCell = std::make_shared<LogicOutputCell>();
};

BaseComponent* LogicOutput::CloneBaseComponent(const CoreLogic* pCoreLogic) const
{
    return new LogicOutput(*this, pCoreLogic);
}

void LogicOutput::ResetZValue()
{
    setZValue(components::zvalues::OUTPUT);
}

void LogicOutput::paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOption, QWidget *pWidget)
{
    Q_UNUSED(pWidget);

    if (std::static_pointer_cast<LogicOutputCell>(mLogicCell)->GetState() == LogicState::LOW)
    {
        QPen pen(pOption->state & QStyle::State_Selected ? components::SELECTED_BORDER_COLOR : components::BORDER_COLOR,
                 components::BORDER_WIDTH, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);

        pPainter->setPen(pen);
        pPainter->setBrush(QBrush(components::FILL_COLOR));
    }
    else if (std::static_pointer_cast<LogicOutputCell>(mLogicCell)->GetState() == LogicState::HIGH)
    {
        pPainter->setPen(QPen(Qt::white));
        pPainter->setBrush(QBrush(Qt::white));
    }

    pPainter->drawEllipse(mWidth * -0.5f, mHeight * -0.5f, mWidth, mHeight);
}

QRectF LogicOutput::boundingRect() const
{
    return QRectF(mWidth * -0.5f, mHeight * -0.5f, mWidth, mHeight);
}

QPainterPath LogicOutput::shape() const
{
    QPainterPath path;
    path.addRect(mWidth * -0.5f, mHeight * -0.5f, mWidth, mHeight);
    return path;
}
