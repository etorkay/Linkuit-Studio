#include "CoreLogic.h"

#include "Components/Gates/AndGate.h"
#include "Components/Gates/OrGate.h"
#include "Components/Gates/XorGate.h"
#include "Components/Gates/NotGate.h"
#include "Components/Gates/BufferGate.h"
#include "Components/Inputs/LogicInput.h"
#include "Components/Inputs/LogicConstant.h"
#include "Components/Inputs/LogicButton.h"
#include "Components/Inputs/LogicClock.h"
#include "Components/Outputs/LogicOutput.h"
#include "Components/TextLabel.h"
#include "Components/ComplexLogic/HalfAdder.h"
#include "Components/ComplexLogic/FullAdder.h"
#include "Components/ComplexLogic/RsFlipFlop.h"
#include "Components/ComplexLogic/RsMsFlipFlop.h"
#include "Components/ComplexLogic/RsClockedFlipFlop.h"
#include "Components/ComplexLogic/DFlipFlop.h"
#include "Components/ComplexLogic/DMsFlipFlop.h"
#include "Components/ComplexLogic/TFlipFlop.h"
#include "Components/ComplexLogic/JKFlipFlop.h"
#include "Components/ComplexLogic/JkMsFlipFlop.h"
#include "Components/ComplexLogic/Multiplexer.h"
#include "Components/ComplexLogic/Demultiplexer.h"
#include "Components/ComplexLogic/Decoder.h"
#include "Components/ComplexLogic/Encoder.h"
#include "Components/ComplexLogic/ShiftRegister.h"
#include "Components/ComplexLogic/Counter.h"

#include "Undo/UndoAddType.h"
#include "Undo/UndoDeleteType.h"
#include "Undo/UndoMoveType.h"
#include "Undo/UndoConfigureType.h"

#include "HelperFunctions.h"

#include <QCoreApplication>

CoreLogic::CoreLogic(View &pView):
    mView(pView),
    mHorizontalPreviewWire(this, WireDirection::HORIZONTAL, 0),
    mVerticalPreviewWire(this, WireDirection::VERTICAL, 0),
    mPropagationTimer(this),
    mProcessingTimer(this),
    mCircuitFileParser(mRuntimeConfigParser)
{
    mView.Init();

    mProcessingTimer.setSingleShot(true);

    QObject::connect(&mPropagationTimer, &QTimer::timeout, this, &CoreLogic::OnPropagationTimeout);
    QObject::connect(&mProcessingTimer, &QTimer::timeout, this, &CoreLogic::OnProcessingTimeout);

    QObject::connect(&mCircuitFileParser, &CircuitFileParser::LoadCircuitFileSuccessSignal, this, &CoreLogic::ReadJson);

    if (!mRuntimeConfigParser.LoadRuntimeConfig(GetRuntimeConfigAbsolutePath()))
    {
        qDebug() << "Could not open runtime config file, using defaults";
    }
}

RuntimeConfigParser& CoreLogic::GetRuntimeConfigParser()
{
    return mRuntimeConfigParser;
}

CircuitFileParser& CoreLogic::GetCircuitFileParser()
{
    return mCircuitFileParser;
}

void CoreLogic::SetShowWelcomeDialogOnStartup(bool pShowOnStartup)
{
    mRuntimeConfigParser.IsWelcomeDialogEnabledOnStartup(pShowOnStartup);
}

void CoreLogic::SelectAll()
{
    if (mControlMode == ControlMode::COPY || mControlMode == ControlMode::SIMULATION)
    {
        return;
    }

    EnterControlMode(ControlMode::EDIT);

    QPainterPath path;
    path.addRect(mView.Scene()->sceneRect());
    mView.Scene()->setSelectionArea(path);
}

void CoreLogic::EnterControlMode(ControlMode pNewMode)
{
    mView.Scene()->clearFocus();
    if (pNewMode == ControlMode::SIMULATION)
    {
        mView.GetPieMenu()->hide();
    }
    else
    {
        mView.GetPieMenu()->HideIfNotPinned();
    }

    emit HideClockConfiguratorSignal();

    if (pNewMode == mControlMode)
    {
        return;
    }

    if (mControlMode == ControlMode::SIMULATION)
    {
        mControlMode = pNewMode;
        LeaveSimulation();
    }

    if (mControlMode == ControlMode::COPY)
    {
        FinishPaste(); // Finish copy because copy mode is left (either accept or abort the copy)
    }

    mControlMode = pNewMode;
    emit ControlModeChangedSignal(pNewMode);

    if (pNewMode == ControlMode::ADD)
    {
        emit ComponentTypeChangedSignal(mComponentType);
    }

    if (pNewMode == ControlMode::SIMULATION)
    {
        EnterSimulation();
        RunSimulation();
    }

    mView.UpdatePieMenuIcons();

    Q_ASSERT(mControlMode == pNewMode);
}

void CoreLogic::SetSimulationMode(SimulationMode pNewMode)
{
    if (mSimulationMode != pNewMode)
    {
        mSimulationMode = pNewMode;
        emit SimulationModeChangedSignal(mSimulationMode);
    }
}

void CoreLogic::EnterSimulation()
{
    StartProcessing();
    ParseWireGroups();
    CreateWireLogicCells();
    ConnectLogicCells();
    EndProcessing();
    SetSimulationMode(SimulationMode::STOPPED);
    emit SimulationStartSignal();
    StepSimulation();
}

void CoreLogic::RunSimulation()
{
    if (mControlMode == ControlMode::SIMULATION && mSimulationMode == SimulationMode::STOPPED)
    {
        mPropagationTimer.start(simulation::PROPAGATION_DELAY);
        SetSimulationMode(SimulationMode::RUNNING);
    }
}

void CoreLogic::StepSimulation()
{
    if (mControlMode == ControlMode::SIMULATION)
    {
        OnPropagationTimeout();
    }
}

void CoreLogic::ResetSimulation()
{
    if (mControlMode == ControlMode::SIMULATION && ! IsProcessing())
    {
        LeaveSimulation();
        EnterSimulation();
    }
}

void CoreLogic::PauseSimulation()
{
    if (mControlMode == ControlMode::SIMULATION && mSimulationMode == SimulationMode::RUNNING)
    {
        mPropagationTimer.stop();
        SetSimulationMode(SimulationMode::STOPPED);
    }
}

void CoreLogic::LeaveSimulation()
{
    mPropagationTimer.stop();
    SetSimulationMode(SimulationMode::STOPPED);
    emit SimulationStopSignal();
}

void CoreLogic::OnMasterSlaveToggled(bool pChecked)
{
    switch (mComponentType)
    {
        case ComponentType::D_FLIPFLOP:
        {
            mIsDFlipFlopMasterSlave = pChecked;
            break;
        }
        case ComponentType::JK_FLIPFLOP:
        {
            mIsJkFlipFlopMasterSlave = pChecked;
            break;
        }
        default:
        {
            throw std::logic_error("Master-slave button toggled for unapplicable component type");
            break;
        }
    }
}

void CoreLogic::OnToggleValueChanged(uint32_t pValue)
{
    if (mView.Scene()->selectedItems().size() == 1 && nullptr != dynamic_cast<LogicClock*>(mView.Scene()->selectedItems()[0]))
    {
        auto clock = static_cast<LogicClock*>(mView.Scene()->selectedItems()[0]);
        auto clockCell = std::dynamic_pointer_cast<LogicClockCell>(clock->GetLogicCell());
        if (nullptr != clockCell && pValue != clockCell->GetToggleTicks())
        {
            clockCell->SetToggleTicks(pValue);
            mCircuitFileParser.MarkAsModified();
        }
    }
}

void CoreLogic::OnPulseValueChanged(uint32_t pValue)
{
    if (mView.Scene()->selectedItems().size() == 1 && nullptr != dynamic_cast<LogicClock*>(mView.Scene()->selectedItems()[0]))
    {
        auto clock = static_cast<LogicClock*>(mView.Scene()->selectedItems()[0]);
        auto clockCell = std::dynamic_pointer_cast<LogicClockCell>(clock->GetLogicCell());
        if (nullptr != clockCell && pValue != clockCell->GetPulseTicks())
        {
            clockCell->SetPulseTicks(pValue);
            mCircuitFileParser.MarkAsModified();
        }
    }
}

void CoreLogic::OnClockModeChanged(ClockMode pMode)
{
    if (mView.Scene()->selectedItems().size() == 1 && nullptr != dynamic_cast<LogicClock*>(mView.Scene()->selectedItems()[0]))
    {
        auto clock = static_cast<LogicClock*>(mView.Scene()->selectedItems()[0]);
        auto clockCell = std::dynamic_pointer_cast<LogicClockCell>(clock->GetLogicCell());
        if (nullptr != clockCell && pMode != clockCell->GetClockMode())
        {
            clockCell->SetClockMode(pMode);
            mCircuitFileParser.MarkAsModified();
        }
    }
}

void CoreLogic::EnterAddControlMode(ComponentType pComponentType)
{
    EnterControlMode(ControlMode::ADD);
    SelectComponentType(pComponentType);
}

ComponentType CoreLogic::GetSelectedComponentType() const
{
    return mComponentType;
}

bool CoreLogic::IsSimulationRunning() const
{
    return (mControlMode == ControlMode::SIMULATION);
}

void CoreLogic::OnPropagationTimeout()
{
    emit SimulationAdvanceSignal();
}

bool CoreLogic::IsUndoQueueEmpty() const
{
    return mUndoQueue.empty();
}

bool CoreLogic::IsRedoQueueEmpty() const
{
    return mRedoQueue.empty();
}

void CoreLogic::SelectComponentType(ComponentType pComponentType)
{
    Q_ASSERT(mControlMode == ControlMode::ADD);
    mComponentType = pComponentType;
    emit ComponentTypeChangedSignal(mComponentType);
}

std::optional<IBaseComponent*> CoreLogic::GetItem() const
{
    IBaseComponent* item = nullptr;

    switch(mComponentType)
    {
        case ComponentType::AND_GATE:
        {
            item = new AndGate(this, mGateInputCount, mComponentDirection);
            break;
        }
        case ComponentType::OR_GATE:
        {
            item = new OrGate(this, mGateInputCount, mComponentDirection);
            break;
        }
        case ComponentType::XOR_GATE:
        {
            item = new XorGate(this, mGateInputCount, mComponentDirection);
            break;
        }
        case ComponentType::NOT_GATE:
        {
            item = new NotGate(this, mComponentDirection);
            break;
        }
        case ComponentType::BUFFER_GATE:
        {
            item = new BufferGate(this, mComponentDirection);
            break;
        }
        case ComponentType::INPUT:
        {
            item = new LogicInput(this);
            break;
        }
        case ComponentType::CONSTANT:
        {
            item = new LogicConstant(this, mConstantState);
            break;
        }
        case ComponentType::BUTTON:
        {
            item = new LogicButton(this);
            break;
        }
        case ComponentType::CLOCK:
        {
            item = new LogicClock(this, mComponentDirection);
            break;
        }
        case ComponentType::OUTPUT:
        {
            item = new LogicOutput(this);
            break;
        }
        case ComponentType::TEXT_LABEL:
        {
            item = new TextLabel(this);
            break;
        }
        case ComponentType::HALF_ADDER:
        {
            item = new HalfAdder(this, mComponentDirection);
            break;
        }
        case ComponentType::FULL_ADDER:
        {
            item = new FullAdder(this, mComponentDirection);
            break;
        }
        case ComponentType::RS_FLIPFLOP:
        {
            switch (mFlipFlopStyle)
            {
                case FlipFlopStyle::LATCH:
                {
                    item = new RsFlipFlop(this, mComponentDirection);
                    break;
                }
                case FlipFlopStyle::CLOCKED:
                {
                    item = new RsClockedFlipFlop(this, mComponentDirection);
                    break;
                }
                case FlipFlopStyle::MASTER_SLAVE:
                {
                    item = new RsMasterSlaveFlipFlop(this, mComponentDirection);
                    break;
                }
                default:
                {
                    throw std::logic_error("Unknown flip-flop style");
                }
            }
            break;
        }
        case ComponentType::D_FLIPFLOP:
        {
            if (mIsDFlipFlopMasterSlave)
            {
                item = new DMasterSlaveFlipFlop(this, mComponentDirection);
            }
            else
            {
                item = new DFlipFlop(this, mComponentDirection);
            }
            break;
        }
        case ComponentType::T_FLIPFLOP:
        {
            item = new TFlipFlop(this, mComponentDirection);
            break;
        }
        case ComponentType::JK_FLIPFLOP:
        {
            if (mIsJkFlipFlopMasterSlave)
            {
                item = new JkMasterSlaveFlipFlop(this, mComponentDirection);
            }
            else
            {
                item = new JKFlipFlop(this, mComponentDirection);
            }
            break;
        }
        case ComponentType::MULTIPLEXER:
        {
            item = new Multiplexer(this, mComponentDirection, mMultiplexerBitWidth);
            break;
        }
        case ComponentType::DEMULTIPLEXER:
        {
            item = new Demultiplexer(this, mComponentDirection, mMultiplexerBitWidth);
            break;
        }
        case ComponentType::DECODER:
        {
            item = new Decoder(this, mComponentDirection, mEncoderDecoderInputCount);
            break;
        }
        case ComponentType::ENCODER:
        {
            item = new Encoder(this, mComponentDirection, mEncoderDecoderInputCount);
            break;
        }
        case ComponentType::SHIFTREGISTER:
        {
            item = new ShiftRegister(this, mComponentDirection, mShiftRegisterBitWidth);
            break;
        }
        case ComponentType::COUNTER:
        {
            item = new Counter(this, mComponentDirection, mCounterBitWidth);
            break;
        }
        default:
        {
            return std::nullopt;
        }
    }

    Q_ASSERT(item);
    return item;
}

ControlMode CoreLogic::GetControlMode() const
{
    return mControlMode;
}

SimulationMode CoreLogic::GetSimulationMode() const
{
    return mSimulationMode;
}

bool CoreLogic::AddCurrentTypeComponent(QPointF pPosition)
{
    if (mView.Scene()->selectedItems().size() > 0)
    {
        return false;
    }

    auto item = GetItem();
    Q_ASSERT(item.has_value());

    item.value()->setPos(SnapToGrid(pPosition));

    if (!GetCollidingComponents(item.value(), false).empty())
    {
        delete item.value();
        return false;
    }

    mView.Scene()->clearFocus(); // Remove focus from components like labels that can be edited while in ADD mode
    mView.Scene()->addItem(item.value());

    auto addedComponents = std::vector<IBaseComponent*>{static_cast<IBaseComponent*>(item.value())};
    AppendUndo(new UndoAddType(addedComponents));

    return true;
}

void CoreLogic::SetGateInputCount(uint8_t pCount)
{
    Q_ASSERT(pCount >= components::gates::MIN_INPUT_COUNT && pCount <= components::gates::MAX_INPUT_COUNT);
    mGateInputCount = pCount;
}

void CoreLogic::SetEncoderDecoderInputCount(uint8_t pCount)
{
    Q_ASSERT(pCount >= components::encoder_decoder::MIN_INPUT_COUNT && pCount <= components::encoder_decoder::MAX_INPUT_COUNT);
    mEncoderDecoderInputCount = pCount;
}

void CoreLogic::SetComponentDirection(Direction pDirection)
{
    mComponentDirection = pDirection;
}

void CoreLogic::SetMultiplexerBitWidth(uint8_t pBitWidth)
{
    Q_ASSERT(pBitWidth >= components::multiplexer::MIN_BIT_WIDTH && pBitWidth <= components::multiplexer::MAX_BIT_WIDTH);
    mMultiplexerBitWidth = pBitWidth;
}

void CoreLogic::SetShiftRegisterBitWidth(uint8_t pBitWidth)
{
    Q_ASSERT(pBitWidth >= components::shift_register::MIN_BIT_WIDTH && pBitWidth <= components::shift_register::MAX_BIT_WIDTH);
    mShiftRegisterBitWidth = pBitWidth;
}

void CoreLogic::SetCounterBitWidth(uint8_t pBitWidth)
{
    Q_ASSERT(pBitWidth >= components::counter::MIN_BIT_WIDTH && pBitWidth <= components::counter::MAX_BIT_WIDTH);
    mCounterBitWidth = pBitWidth;
}

void CoreLogic::SetFlipFlopStyle(FlipFlopStyle pStyle)
{
    mFlipFlopStyle = pStyle;
}

void CoreLogic::SetConstantState(LogicState pState)
{
    mConstantState = pState;
}

void CoreLogic::SetPreviewWireStart(QPointF pPoint)
{
    mPreviewWireStart = SnapToGrid(pPoint);

    mHorizontalPreviewWire.SetLength(0);
    mVerticalPreviewWire.SetLength(0);

    mView.Scene()->addItem(&mHorizontalPreviewWire);
    mView.Scene()->addItem(&mVerticalPreviewWire);
}

void CoreLogic::ShowPreviewWires(QPointF pCurrentPoint)
{
    QPointF snappedCurrentPoint = SnapToGrid(pCurrentPoint);

    // Set the start direction (which wire is drawn starting at the start position)
    if (mWireStartDirection == WireDirection::UNSET)
    {
        if (snappedCurrentPoint.x() != mPreviewWireStart.x())
        {
            mWireStartDirection = WireDirection::HORIZONTAL;
        }
        else if (snappedCurrentPoint.y() != mPreviewWireStart.y())
        {
            mWireStartDirection = WireDirection::VERTICAL;
        }
    }

    // Trigger a redraw of the area where the wires were before
    mHorizontalPreviewWire.setVisible(false);
    mVerticalPreviewWire.setVisible(false);

    mHorizontalPreviewWire.SetLength(std::abs(mPreviewWireStart.x() - snappedCurrentPoint.x()));
    mVerticalPreviewWire.SetLength(std::abs(mPreviewWireStart.y() - snappedCurrentPoint.y()));

    if (mWireStartDirection == WireDirection::HORIZONTAL)
    {
        mHorizontalPreviewWire.setPos(std::min(mPreviewWireStart.x(), snappedCurrentPoint.x()), mPreviewWireStart.y());
        mVerticalPreviewWire.setPos(snappedCurrentPoint.x(), std::min(mPreviewWireStart.y(), snappedCurrentPoint.y()));
    }
    else
    {
        mVerticalPreviewWire.setPos(mPreviewWireStart.x(), std::min(mPreviewWireStart.y(), snappedCurrentPoint.y()));
        mHorizontalPreviewWire.setPos(std::min(mPreviewWireStart.x(), snappedCurrentPoint.x()), snappedCurrentPoint.y());
    }

    mHorizontalPreviewWire.setVisible(true);
    mVerticalPreviewWire.setVisible(true);
}

void CoreLogic::AddWires(QPointF pEndPoint)
{
    mView.Scene()->removeItem(&mHorizontalPreviewWire);
    mView.Scene()->removeItem(&mVerticalPreviewWire);

    if (mWireStartDirection == WireDirection::UNSET)
    {
        return; // Return if no wire must be drawn anyways
    }

    QPointF snappedEndPoint = SnapToGrid(pEndPoint);
    std::vector<IBaseComponent*> addedComponents;
    std::vector<IBaseComponent*> deletedComponents;

    // Add horizontal wire
    if (mPreviewWireStart.x() != snappedEndPoint.x())
    {
        auto item = new LogicWire(this, WireDirection::HORIZONTAL, std::abs(mPreviewWireStart.x() - snappedEndPoint.x()));

        if (mWireStartDirection == WireDirection::HORIZONTAL)
        {
            item->setPos(std::min(mPreviewWireStart.x(), snappedEndPoint.x()), mPreviewWireStart.y());
        }
        else
        {
            item->setPos(std::min(mPreviewWireStart.x(), snappedEndPoint.x()), snappedEndPoint.y());
        }

        // Delete wires that are completely behind the new wire
        const auto containedWires = DeleteContainedWires(item);
        deletedComponents.insert(deletedComponents.end(), containedWires.begin(), containedWires.end());

        // Find wires left or right of the new wire (those may be partly behind the new wire)
        auto startAdjacent = GetAdjacentWire(QPointF(item->x() - 2, item->y()), WireDirection::HORIZONTAL);
        auto endAdjacent = GetAdjacentWire(QPointF(item->x() + item->GetLength() + 2, item->y()), WireDirection::HORIZONTAL);

        auto horizontalWire = MergeWires(item, startAdjacent, endAdjacent);

        delete item;

        if (startAdjacent == endAdjacent)
        {
            endAdjacent = std::nullopt;
        }

        if (startAdjacent.has_value())
        {
            deletedComponents.push_back(static_cast<IBaseComponent*>(startAdjacent.value()));
            Q_ASSERT(startAdjacent.value());
            mView.Scene()->removeItem(startAdjacent.value());
        }

        if (endAdjacent.has_value())
        {
            deletedComponents.push_back(static_cast<IBaseComponent*>(endAdjacent.value()));
            Q_ASSERT(endAdjacent.value());
            mView.Scene()->removeItem(endAdjacent.value());
        }

        mView.Scene()->addItem(horizontalWire);
        addedComponents.push_back(static_cast<IBaseComponent*>(horizontalWire));
    }

    // Add vertical wire
    if (mPreviewWireStart.y() != snappedEndPoint.y())
    {
        auto item = new LogicWire(this, WireDirection::VERTICAL, std::abs(mPreviewWireStart.y() - snappedEndPoint.y()));

        if (mWireStartDirection == WireDirection::VERTICAL)
        {
            item->setPos(mPreviewWireStart.x(), std::min(mPreviewWireStart.y(), snappedEndPoint.y()));
        }
        else
        {
            item->setPos(snappedEndPoint.x(), std::min(mPreviewWireStart.y(), snappedEndPoint.y()));
        }

        // Delete wires that are completely behind the new wire
        const auto containedWires = DeleteContainedWires(item);
        deletedComponents.insert(deletedComponents.end(), containedWires.begin(), containedWires.end());

        // Find wires above or below of the new wire (those may be partly behind the new wire)
        auto startAdjacent = GetAdjacentWire(QPointF(item->x(), item->y() - 2), WireDirection::VERTICAL);
        auto endAdjacent = GetAdjacentWire(QPointF(item->x(), item->y() + item->GetLength() + 2), WireDirection::VERTICAL);

        auto verticalWire = MergeWires(item, startAdjacent, endAdjacent);

        delete item;

        if (startAdjacent == endAdjacent)
        {
            endAdjacent = std::nullopt;
        }

        if (startAdjacent.has_value())
        {
            deletedComponents.push_back(static_cast<IBaseComponent*>(startAdjacent.value()));
            mView.Scene()->removeItem(startAdjacent.value());
        }

        if (endAdjacent.has_value())
        {
            deletedComponents.push_back(static_cast<IBaseComponent*>(endAdjacent.value()));
            mView.Scene()->removeItem(endAdjacent.value());
        }

        mView.Scene()->addItem(verticalWire);
        addedComponents.push_back(static_cast<IBaseComponent*>(verticalWire));
    }

    std::vector<IBaseComponent*> addedConPoints;

    for (const auto& wire : addedComponents)
    {
        for (const auto& collidingComp : mView.Scene()->collidingItems(wire, Qt::IntersectsItemShape))
        {
            if (dynamic_cast<LogicWire*>(collidingComp) != nullptr && IsTCrossing(static_cast<LogicWire*>(wire), static_cast<LogicWire*>(collidingComp)))
            {
                QPointF conPointPos;
                if (static_cast<LogicWire*>(wire)->GetDirection() == WireDirection::HORIZONTAL)
                {
                    conPointPos.setX(collidingComp->x());
                    conPointPos.setY(wire->y());
                }
                else
                {
                    conPointPos.setX(wire->x());
                    conPointPos.setY(collidingComp->y());
                }

                if (!IsComponentAtPosition<ConPoint>(conPointPos))
                {
                    auto item = new ConPoint(this);
                    item->setPos(conPointPos);
                    addedConPoints.push_back(item);
                    mView.Scene()->addItem(item);
                }
            }
        }
    }

    addedComponents.insert(addedComponents.end(), addedConPoints.begin(), addedConPoints.end());
    AppendUndo(new UndoAddType(addedComponents, deletedComponents));
    mWireStartDirection = WireDirection::UNSET;
}

template<typename T>
bool CoreLogic::IsComponentAtPosition(QPointF pPos)
{
    for (const auto& comp : mView.Scene()->items(pPos, Qt::IntersectsItemShape))
    {
        if (dynamic_cast<T*>(comp) != nullptr)
        {
            return true;
        }
    }
    return false;
}

bool CoreLogic::TwoConPointsAtPosition(QPointF pPos)
{
    uint8_t conPoints = 0;
    for (const auto& comp : mView.Scene()->items(pPos, Qt::IntersectsItemShape))
    {
        if (dynamic_cast<ConPoint*>(comp) != nullptr)
        {
            conPoints++;
        }
    }
    return (conPoints == 2);
}

void CoreLogic::MergeWiresAfterMove(const std::vector<LogicWire*> &pWires, std::vector<IBaseComponent*> &pAddedComponents, std::vector<IBaseComponent*> &pDeletedComponents)
{
    for (const auto& w : pWires)
    {
        ProcessingHeartbeat();

        const auto&& containedWires = DeleteContainedWires(w);
        pDeletedComponents.insert(pDeletedComponents.end(), containedWires.begin(), containedWires.end());

        std::optional<LogicWire*> startAdjacent = std::nullopt;
        std::optional<LogicWire*> endAdjacent = std::nullopt;

        if (w->GetDirection() == WireDirection::HORIZONTAL)
        {
            startAdjacent = GetAdjacentWire(QPointF(w->x() - 4, w->y()), WireDirection::HORIZONTAL);
            endAdjacent = GetAdjacentWire(QPointF(w->x() + w->GetLength() + 4, w->y()), WireDirection::HORIZONTAL);

            auto horizontalWire = MergeWires(w, startAdjacent, endAdjacent);
            horizontalWire->setSelected(w->isSelected());

            if (startAdjacent == endAdjacent)
            {
                endAdjacent = std::nullopt;
            }

            mView.Scene()->addItem(horizontalWire);
            pAddedComponents.push_back(static_cast<LogicWire*>(horizontalWire));
        }
        else
        {
            startAdjacent = GetAdjacentWire(QPointF(w->x(), w->y() - 4), WireDirection::VERTICAL);
            endAdjacent = GetAdjacentWire(QPointF(w->x(), w->y() + w->GetLength() + 4), WireDirection::VERTICAL);

            auto verticalWire = MergeWires(w, startAdjacent, endAdjacent);
            verticalWire->setSelected(w->isSelected());

            if (startAdjacent == endAdjacent)
            {
                endAdjacent = std::nullopt;
            }

            mView.Scene()->addItem(verticalWire);
            pAddedComponents.push_back(static_cast<LogicWire*>(verticalWire));
        }

        Q_ASSERT(w->scene() == mView.Scene());
        pDeletedComponents.push_back(w);
        mView.Scene()->removeItem(w);

        if (startAdjacent.has_value() && std::find(pAddedComponents.begin(), pAddedComponents.end(), startAdjacent) == pAddedComponents.end())
        {
            pDeletedComponents.push_back(static_cast<LogicWire*>(startAdjacent.value()));
            mView.Scene()->removeItem(startAdjacent.value());
        }

        if (endAdjacent.has_value() && std::find(pAddedComponents.begin(), pAddedComponents.end(), endAdjacent) == pAddedComponents.end())
        {
            pDeletedComponents.push_back(static_cast<LogicWire*>(endAdjacent.value()));
            mView.Scene()->removeItem(endAdjacent.value());
        }
    }
}

std::vector<LogicWire*> CoreLogic::DeleteContainedWires(const LogicWire* pWire)
{
    std::vector<LogicWire*> deletedComponents;

    QRectF collisionRect;
    if (pWire->GetDirection() == WireDirection::HORIZONTAL)
    {
        collisionRect = QRectF(pWire->x() - 2, pWire->y() - components::wires::BOUNDING_RECT_SIZE / 2.0f - 2,
                               pWire->GetLength() + 4, components::wires::BOUNDING_RECT_SIZE + 4);
    }
    else
    {
        collisionRect = QRectF(pWire->x() - components::wires::BOUNDING_RECT_SIZE / 2.0f - 2, pWire->y() - 2,
                               components::wires::BOUNDING_RECT_SIZE + 4, pWire->GetLength() + 4);
    }

    const auto&& containedComponents = mView.Scene()->items(collisionRect, Qt::ContainsItemShape, Qt::DescendingOrder);

    for (const auto &wire : containedComponents)
    {
        if (dynamic_cast<LogicWire*>(wire) != nullptr && static_cast<LogicWire*>(wire)->GetDirection() == pWire->GetDirection() && wire != pWire)
        {
            deletedComponents.push_back(static_cast<LogicWire*>(wire));
            mView.Scene()->removeItem(wire);
        }
    }

    return deletedComponents;
}

std::optional<LogicWire*> CoreLogic::GetAdjacentWire(QPointF pCheckPosition, WireDirection pDirection) const
{
    const auto&& componentsAtPosition = mView.Scene()->items(pCheckPosition.x(), pCheckPosition.y(), 1, 1, Qt::IntersectsItemShape, Qt::DescendingOrder);
    const auto&& wiresAtPosition = FilterForWires(componentsAtPosition, pDirection);

    if (wiresAtPosition.size() > 0)
    {
        Q_ASSERT(static_cast<LogicWire*>(wiresAtPosition.at(0)));
        return std::optional(static_cast<LogicWire*>(wiresAtPosition.at(0)));
    }

    return std::nullopt;
}

// Remember that using (dynamic_cast<LogicWire*>(comp) != nullptr) directly is more efficient than iterating over filtered components
std::vector<IBaseComponent*> CoreLogic::FilterForWires(const QList<QGraphicsItem*> &pComponents, WireDirection pDirection) const
{
    std::vector<IBaseComponent*> wires;

    for (auto &comp : pComponents)
    {
        if (dynamic_cast<LogicWire*>(comp) != nullptr
                && (static_cast<LogicWire*>(comp)->GetDirection() == pDirection || pDirection == WireDirection::UNSET))
        {
            wires.push_back(static_cast<IBaseComponent*>(comp));
        }
    }

    return wires;
}

std::vector<IBaseComponent*> CoreLogic::GetCollidingComponents(IBaseComponent* pComponent, bool pOnlyUnselected) const
{
    std::vector<IBaseComponent*> collidingComponents;

    for (auto &comp : mView.Scene()->collidingItems(pComponent))
    {
        Q_ASSERT(comp);

        if (IsCollidingComponent(comp) && (!pOnlyUnselected || !comp->isSelected()))
        {
            // comp must be IBaseComponent at this point
            collidingComponents.push_back(static_cast<IBaseComponent*>(comp));
        }
    }

    return collidingComponents;
}

bool CoreLogic::IsCollidingComponent(QGraphicsItem* pComponent) const
{
    return (dynamic_cast<IBaseComponent*>(pComponent) != nullptr
            && dynamic_cast<LogicWire*>(pComponent) == nullptr
            && dynamic_cast<ConPoint*>(pComponent) == nullptr);
}

bool CoreLogic::IsTCrossing(const LogicWire* pWire1, const LogicWire* pWire2) const
{
    const LogicWire* a = nullptr;
    const LogicWire* b = nullptr;

    if (pWire1->GetDirection() == WireDirection::VERTICAL && pWire2->GetDirection() == WireDirection::HORIZONTAL)
    {
        a = pWire1;
        b = pWire2;
    }
    else if (pWire1->GetDirection() != pWire2->GetDirection()) // pWire1 horizontal, pWire2 vertical
    {
        a = pWire2;
        b = pWire1;
    }
    else
    {
        return false;
    }

    return ((a->y() < b->y() && a->x() == b->x() && a->y() + a->GetLength() > b->y())
        || (a->y() < b->y() && a->x() == b->x() + b->GetLength() && a->y() + a->GetLength() > b->y())
        || (a->x() > b->x() && a->y() + a->GetLength() == b->y() && a->x() < b->x() + b->GetLength())
        || (a->x() > b->x() && a->y() == b->y() && a->x() < b->x() + b->GetLength()));
}

bool CoreLogic::IsNoCrossingPoint(const ConPoint* pConPoint) const
{
    const auto&& components = mView.Scene()->items(pConPoint->pos(), Qt::IntersectsItemBoundingRect);

    if (components.size() <= 2)
    {
        // Including the ConPoint at the position, there can be at max the ConPoint and one wire
        return true;
    }

    bool foundOne = false;
    bool firstGoesTrough = false;
    for (const auto& comp : components)
    {
        if (dynamic_cast<LogicWire*>(comp) != nullptr)
        {
            if (!foundOne)
            {
                foundOne = true; // Found a crossing wire (either ends in pConPoint or doesn't)
                firstGoesTrough = !static_cast<LogicWire*>(comp)->StartsOrEndsIn(pConPoint->pos()); // True, if this wire doesn't end in pConPoint
            }
            else if ((foundOne && firstGoesTrough) || (foundOne && !static_cast<LogicWire*>(comp)->StartsOrEndsIn(pConPoint->pos())))
            {
                // T-Crossing wire found (first or second one) and two wires total, this is no L or I crossing
                return false;
            }
        }
    }
    return true;
}

bool CoreLogic::IsXCrossingPoint(QPointF pPoint) const
{
    const auto& wires = FilterForWires(mView.Scene()->items(pPoint, Qt::IntersectsItemBoundingRect));

    if (wires.size() <= 1)
    {
        return false;
    }

    for (const auto& wire : wires)
    {
        if (static_cast<LogicWire*>(wire)->StartsOrEndsIn(pPoint))
        {
            return false; // L-Crossing type wire found, this is no X crossing
        }
    }
    return true;
}

LogicWire* CoreLogic::MergeWires(LogicWire* pNewWire, std::optional<LogicWire*> pLeftTopAdjacent, std::optional<LogicWire*> pRightBottomAdjacent) const
{
    Q_ASSERT(pNewWire);
    Q_ASSERT(!pLeftTopAdjacent.has_value() || pLeftTopAdjacent.value());
    Q_ASSERT(!pRightBottomAdjacent.has_value() || pRightBottomAdjacent.value());

    QPointF newStart(pNewWire->pos());

    if (pNewWire->GetDirection() == WireDirection::HORIZONTAL)
    {
        QPointF newEnd(pNewWire->x() + pNewWire->GetLength(), pNewWire->y());

        if (pLeftTopAdjacent.has_value() && pLeftTopAdjacent.value()->GetDirection() == pNewWire->GetDirection())
        {
            Q_ASSERT(pNewWire->y() == pLeftTopAdjacent.value()->y());
            newStart = QPointF(pLeftTopAdjacent.value()->x(), pNewWire->y());
        }
        if (pRightBottomAdjacent.has_value()  && pRightBottomAdjacent.value()->GetDirection() == pNewWire->GetDirection())
        {
            Q_ASSERT(pNewWire->y() == pRightBottomAdjacent.value()->y());
            newEnd = QPoint(pRightBottomAdjacent.value()->x() + pRightBottomAdjacent.value()->GetLength(), pNewWire->y());
        }

        auto newWire = new LogicWire(this, WireDirection::HORIZONTAL, newEnd.x() - newStart.x());
        newWire->setPos(newStart);

        Q_ASSERT(newWire);
        return newWire;
    }
    else
    {
        QPointF newEnd(pNewWire->x(), pNewWire->y() + pNewWire->GetLength());

        if (pLeftTopAdjacent.has_value()  && pLeftTopAdjacent.value()->GetDirection() == pNewWire->GetDirection())
        {
            Q_ASSERT(pNewWire->x() == pLeftTopAdjacent.value()->x());
            newStart = QPointF(pNewWire->x(), pLeftTopAdjacent.value()->y());
        }
        if (pRightBottomAdjacent.has_value()  && pRightBottomAdjacent.value()->GetDirection() == pNewWire->GetDirection())
        {
            Q_ASSERT(pNewWire->x() == pRightBottomAdjacent.value()->x());
            newEnd = QPoint(pNewWire->x(), pRightBottomAdjacent.value()->y() + pRightBottomAdjacent.value()->GetLength());
        }

        auto newWire = new LogicWire(this, WireDirection::VERTICAL, newEnd.y() - newStart.y());
        newWire->setPos(newStart);
        return newWire;
    }
}

void CoreLogic::ParseWireGroups(void)
{
    mWireGroups.clear();
    mWireMap.clear();

    for (auto& comp : mView.Scene()->items())
    {
        // If the component is a wire that is not yet part of a group
        if (dynamic_cast<LogicWire*>(comp) && mWireMap.find(static_cast<LogicWire*>(comp)) == mWireMap.end())
        {
            mWireGroups.push_back(std::vector<IBaseComponent*>());
            ExploreGroup(static_cast<LogicWire*>(comp), mWireGroups.size() - 1);
        }
        ProcessingHeartbeat();
    }

    // Push ConPoints into groups of the wires below; done here because ExploreGroup doesn't catch all ConPoints
    for (auto& comp : mView.Scene()->items())
    {
        if (nullptr != dynamic_cast<ConPoint*>(comp) && static_cast<ConPoint*>(comp)->GetConnectionType() == ConnectionType::FULL)
        {
            auto collidingItems = mView.Scene()->collidingItems(comp, Qt::IntersectsItemShape);
            if (collidingItems.size() > 0) // Sanity check that ConPoint is above wires
            {
                auto wireBelow = dynamic_cast<LogicWire*>(collidingItems[0]);
                if (nullptr != wireBelow)
                {
                    // We trust that all wires have been inserted into mWireMap because checking would be costly
                    mWireGroups[mWireMap.at(wireBelow)].push_back(static_cast<IBaseComponent*>(comp));
                }
            }
        }
        ProcessingHeartbeat();
    }
}

void CoreLogic::ExploreGroup(LogicWire* pWire, int32_t pGroupIndex)
{
    Q_ASSERT(pWire);
    Q_ASSERT(pGroupIndex >= 0);

    mWireMap[pWire] = pGroupIndex;
    mWireGroups[pGroupIndex].push_back(pWire); // Note: pWire must not be part of group pGroupIndex before the call to ExploreGroup

    for (auto& coll : mView.Scene()->collidingItems(pWire, Qt::IntersectsItemShape)) // Item shape is sufficient for wire collision
    {
        if (dynamic_cast<LogicWire*>(coll) != nullptr && mWireMap.find(static_cast<LogicWire*>(coll)) == mWireMap.end())
        {
            auto collisionPoint = GetWireCollisionPoint(pWire, static_cast<LogicWire*>(coll));
            if (collisionPoint.has_value())
            {
                // Get ConPoints to recognize connected wires and traverse them recursively
                auto conPoint = GetConPointAtPosition(collisionPoint.value(), ConnectionType::FULL);
                if (conPoint.has_value() || IsLCrossing(pWire, static_cast<LogicWire*>(coll)))
                {
                    ExploreGroup(static_cast<LogicWire*>(coll), pGroupIndex);
                }
            }
        }
        ProcessingHeartbeat();
    }
}

std::optional<QPointF> CoreLogic::GetWireCollisionPoint(const LogicWire* pWireA, const LogicWire* pWireB) const
{
    Q_ASSERT(pWireA);
    Q_ASSERT(pWireB);

    if (pWireA->GetDirection() == WireDirection::HORIZONTAL && pWireB->GetDirection() == WireDirection::VERTICAL)
    {
        return QPointF(pWireB->x(), pWireA->y());
    }
    else if (pWireA->GetDirection() == WireDirection::VERTICAL && pWireB->GetDirection() == WireDirection::HORIZONTAL)
    {
        return QPointF(pWireA->x(), pWireB->y());
    }

    return std::nullopt;
}

bool CoreLogic::IsLCrossing(LogicWire* pWireA, LogicWire* pWireB) const
{
    Q_ASSERT(pWireA && pWireB);

    const LogicWire* a = nullptr;
    const LogicWire* b = nullptr;

    if (pWireA->GetDirection() == pWireB->GetDirection())
    {
        return false;
    }

    if (pWireA->GetDirection() == WireDirection::VERTICAL && pWireB->GetDirection() == WireDirection::HORIZONTAL)
    {
        a = pWireB;
        b = pWireA;
    }
    else // must be pWireA horizontal, pWireB vertical
    {
        a = pWireA;
        b = pWireB;
    }

    return ((a->y() == b->y() && a->x() == b->x()) || (a->y() == b->y() && a->x() + a->GetLength() == b->x())
        || (a->x() == b->x() && b->y() + b->GetLength() == a->y()) || (a->x() + a->GetLength() == b->x() && a->y() == b->y() + b->GetLength()));
}

std::optional<ConPoint*> CoreLogic::GetConPointAtPosition(QPointF pPos, ConnectionType pType) const
{
    for (const auto& comp : mView.Scene()->items(pPos, Qt::IntersectsItemShape))
    {
        if ((nullptr != dynamic_cast<ConPoint*>(comp)) && (pType == static_cast<ConPoint*>(comp)->GetConnectionType()))
        {
            return static_cast<ConPoint*>(comp);
        }
    }

    return std::nullopt;
}

void CoreLogic::CreateWireLogicCells()
{
    mLogicWireCells.clear();

    for (const auto& group : mWireGroups)
    {
        auto logicCell = std::make_shared<LogicWireCell>(this);
        mLogicWireCells.emplace_back(logicCell);
        for (auto& comp : group)
        {
            if (dynamic_cast<LogicWire*>(comp) != nullptr)
            {
                static_cast<LogicWire*>(comp)->SetLogicCell(logicCell);
            }
            else if (dynamic_cast<ConPoint*>(comp) != nullptr) // Full crossing because sorted into group
            {
                static_cast<ConPoint*>(comp)->SetLogicCell(logicCell);
            }
            ProcessingHeartbeat();
        }
    }
}

void CoreLogic::ConnectLogicCells()
{
    for (auto& comp : mView.Scene()->items())
    {
        ProcessingHeartbeat();

        if (nullptr == dynamic_cast<IBaseComponent*>(comp) || nullptr != dynamic_cast<LogicWire*>(comp))
        {
            continue; //  Skip if no non-wire component
        }

        auto compBase = static_cast<IBaseComponent*>(comp);

        for (auto& coll : mView.Scene()->collidingItems(comp, Qt::IntersectsItemBoundingRect))
        {
            ProcessingHeartbeat();

            if (nullptr == dynamic_cast<LogicWire*>(coll))
            {
                continue; // Skip if no wire
            }

            // Component <-> Wire connection
            auto wire = static_cast<LogicWire*>(coll);

            if (nullptr != dynamic_cast<ConPoint*>(comp))
            {
                const auto& conPoint = static_cast<ConPoint*>(comp);
                if (conPoint->GetConnectionType() != ConnectionType::FULL) // Diode <-> Wire connection
                {
                    Q_ASSERT(compBase->GetLogicCell());
                    auto outputDirection = (conPoint->GetConnectionType() == ConnectionType::DIODE_X ? WireDirection::HORIZONTAL : WireDirection::VERTICAL);
                    auto inputDirection = (conPoint->GetConnectionType() == ConnectionType::DIODE_X ? WireDirection::VERTICAL : WireDirection::HORIZONTAL);

                    if ((wire->GetDirection() == outputDirection)
                            && wire->contains(wire->mapFromScene(compBase->pos() + compBase->GetOutConnectors()[0].pos)))
                    {
                        std::static_pointer_cast<LogicWireCell>(wire->GetLogicCell())->AddInputSlot();
                        compBase->GetLogicCell()->ConnectOutput(wire->GetLogicCell(), std::static_pointer_cast<LogicWireCell>(wire->GetLogicCell())->GetInputSize() - 1, 0);
                    }
                    else if ((wire->GetDirection() == inputDirection)
                            && wire->contains(wire->mapFromScene(compBase->pos() + compBase->GetInConnectors()[0].pos)))
                    {
                        std::static_pointer_cast<LogicWireCell>(wire->GetLogicCell())->AppendOutput(compBase->GetLogicCell(), 0);
                    }
                }
            }
            else // Other component <-> Wire connection
            {
                for (size_t out = 0; out < compBase->GetOutConnectorCount(); out++)
                {
                    if (wire->contains(wire->mapFromScene(compBase->pos() + compBase->GetOutConnectors()[out].pos)))
                    {
                        std::static_pointer_cast<LogicWireCell>(wire->GetLogicCell())->AddInputSlot();
                        compBase->GetLogicCell()->ConnectOutput(wire->GetLogicCell(), std::static_pointer_cast<LogicWireCell>(wire->GetLogicCell())->GetInputSize() - 1, out);
                    }
                }

                for (size_t in = 0; in < compBase->GetInConnectorCount(); in++)
                {
                    if (wire->contains(wire->mapFromScene(compBase->pos() + compBase->GetInConnectors()[in].pos)))
                    {
                        std::static_pointer_cast<LogicWireCell>(wire->GetLogicCell())->AppendOutput(compBase->GetLogicCell(), in);
                    }
                }
            }
        }
    }
}

void CoreLogic::StartProcessing()
{
    mProcessingTimer.start(gui::PROCESSING_OVERLAY_TIMEOUT);
    mIsProcessing = true;
}

void CoreLogic::ProcessingHeartbeat()
{
    QCoreApplication::processEvents(); // User input during processing will be handled but ignored
}

void CoreLogic::OnProcessingTimeout()
{
    mView.FadeInProcessingOverlay();
    emit ProcessingStartedSignal();
}

void CoreLogic::EndProcessing()
{
    mProcessingTimer.stop();
    mView.FadeOutProcessingOverlay();
    mIsProcessing = false;
    emit ProcessingEndedSignal();
}

bool CoreLogic::IsProcessing() const
{
    return mIsProcessing;
}

bool CoreLogic::IsDFlipFlopMasterSlave() const
{
    return mIsDFlipFlopMasterSlave;
}

bool CoreLogic::IsJkFlipFlopMasterSlave() const
{
    return mIsJkFlipFlopMasterSlave;
}

void CoreLogic::ClearSelection()
{
    mView.Scene()->clearSelection();
    emit HideClockConfiguratorSignal();
}

void CoreLogic::OnSelectedComponentsMovedOrPasted(QPointF pOffset)
{   
    StartProcessing();

    if (pOffset.manhattanLength() <= 0 && mControlMode != ControlMode::COPY) // No effective movement
    {
        EndProcessing();
        return;
    }

    if (mControlMode == ControlMode::COPY && mCurrentCopyUndoType.has_value() && mCurrentCopyUndoType.value()->IsCompleted()) // Ignore calls when copy action is already completed
    // OnSelectedComponentsMovedOrPasted is invoked once when pasting and deselecting immediately and twice if moving the pasted components
    {
        EndProcessing();
        return;
    }

    std::vector<LogicWire*> affectedWires;
    std::vector<IBaseComponent*> affectedComponents;

    if (mControlMode == ControlMode::COPY && mCurrentCopyUndoType.has_value())
    {
        for (const auto& comp : mCurrentPaste)
        {
            affectedComponents.push_back(static_cast<IBaseComponent*>(comp));

            if (nullptr != dynamic_cast<LogicWire*>(comp))
            {
                affectedWires.push_back(static_cast<LogicWire*>(comp));
            }

            ProcessingHeartbeat();
        }
    }
    else
    {
        for (const auto& comp : mView.Scene()->selectedItems())
        {
            ProcessingHeartbeat();

            if (nullptr == dynamic_cast<IBaseComponent*>(comp))
            {
                continue;
            }

            affectedComponents.push_back(static_cast<IBaseComponent*>(comp));

            if (nullptr != dynamic_cast<LogicWire*>(comp))
            {
                affectedWires.push_back(static_cast<LogicWire*>(comp));
            }
        }
    }

    std::vector<IBaseComponent*> movedComponents;
    std::vector<IBaseComponent*> addedComponents;
    std::vector<IBaseComponent*> deletedComponents;

    MergeWiresAfterMove(affectedWires, addedComponents, deletedComponents); // Ca. 25% of total cost

    // Insert merged wires to recognize T-crossings
    affectedComponents.insert(affectedComponents.end(), addedComponents.begin(), addedComponents.end());
    // In theory, we should remove deletedComponents from movedComponents here, but that would be costly and
    // should not make any difference because old wires behind the merged ones cannot generate new ConPoints

    for (const auto& comp : affectedComponents) // Ca. 75% of total cost
    {
        ProcessingHeartbeat();

        if (ManageConPointsOneStep(comp, pOffset, movedComponents, addedComponents, deletedComponents))
        {
            continue;
        }

        // Collision, abort
        for (const auto& comp : affectedComponents) // Revert moving
        {
            comp->moveBy(-pOffset.x(), -pOffset.y());
        }
        for (const auto& comp : addedComponents) // Revert adding
        {
            delete comp;
        }
        for (const auto& comp : deletedComponents) // Revert deleting
        {
            mView.Scene()->addItem(comp);
        }

        AbortPastingIfInCopy();

        ClearSelection();
        EndProcessing();
        return;
    }

    ClearSelection();


    if (movedComponents.size() > 0 || mControlMode == ControlMode::COPY) // Create undo copy actions also when no components were moved
    {
        if (mControlMode != ControlMode::COPY)
        {
            AppendUndo(new UndoMoveType(movedComponents, addedComponents, deletedComponents, pOffset));
        }
        else
        {
            if (mCurrentCopyUndoType.has_value())
            {
                mCurrentCopyUndoType.value()->AppendAddedComponents(addedComponents);
                mCurrentCopyUndoType.value()->AppendDeletedComponents(deletedComponents);
                mCurrentCopyUndoType.value()->AppendMovedComponents(movedComponents);
                mCurrentCopyUndoType.value()->SetOffset(pOffset);
                // Mark as completed so that the pointer will not be deleted during the next copy action
                mCurrentCopyUndoType.value()->MarkCompleted();
                AppendUndo(mCurrentCopyUndoType.value());
            }
        }
    }

    EndProcessing();
}

bool CoreLogic::ManageConPointsOneStep(IBaseComponent* pComponent, QPointF& pOffset, std::vector<IBaseComponent*>& pMovedComponents,
                                       std::vector<IBaseComponent*>& pAddedComponents, std::vector<IBaseComponent*>& pDeletedComponents)
{
    if (IsCollidingComponent(pComponent) && !GetCollidingComponents(pComponent, true).empty()) // Abort if collision with unselected component
    {
        return false;
    }

    // Delete all invalid ConPoints at the original position colliding with the selection
    QRectF oldCollisionRect(pComponent->pos() + pComponent->boundingRect().topLeft() - pOffset, pComponent->pos() + pComponent->boundingRect().bottomRight() - pOffset);

    const auto&& abandonedComponents = mView.Scene()->items(oldCollisionRect, Qt::IntersectsItemShape);

    for (const auto& collidingComp : abandonedComponents)
    {
        if ((nullptr != dynamic_cast<ConPoint*>(collidingComp)) && !collidingComp->isSelected() && IsNoCrossingPoint(static_cast<ConPoint*>(collidingComp)))
        {
            Q_ASSERT(collidingComp->scene() == mView.Scene());
            mView.Scene()->removeItem(collidingComp);
            pDeletedComponents.push_back(static_cast<IBaseComponent*>(collidingComp));
        }
        ProcessingHeartbeat();
    }

    // Delete all ConPoints of the moved components that are not valid anymore (plus ConPoints that already exist the position; needed when copying)
    if ((nullptr != dynamic_cast<ConPoint*>(pComponent)) && (IsNoCrossingPoint(static_cast<ConPoint*>(pComponent)) || TwoConPointsAtPosition(pComponent->pos())))
    {
        Q_ASSERT(pComponent->scene() == mView.Scene());
        mView.Scene()->removeItem(pComponent);
        pDeletedComponents.push_back(pComponent);
    }

    // Add ConPoints to all T Crossings
    if (nullptr != dynamic_cast<LogicWire*>(pComponent))
    {
        AddConPointsToTCrossings(static_cast<LogicWire*>(pComponent), pAddedComponents);
    }

    pMovedComponents.push_back(pComponent);
    return true;
}

void CoreLogic::AddConPointsToTCrossings(LogicWire* pWire, std::vector<IBaseComponent*>& addedComponents)
{
    const auto&& collidingComponents = mView.Scene()->collidingItems(pWire, Qt::IntersectsItemShape);

    for (const auto& collidingComp : collidingComponents)
    {
        ProcessingHeartbeat();

        if (dynamic_cast<LogicWire*>(collidingComp) == nullptr)
        {
            continue;
        }

        if (!IsTCrossing(pWire, static_cast<LogicWire*>(collidingComp)))
        {
            continue;
        }

        QPointF conPointPos;
        if (pWire->GetDirection() == WireDirection::HORIZONTAL)
        {
            conPointPos = QPointF(collidingComp->x(), pWire->y());
        }
        else
        {
            conPointPos = QPointF(pWire->x(), collidingComp->y());
        }

        if (!IsComponentAtPosition<ConPoint>(conPointPos))
        {
            auto item = new ConPoint(this);
            item->setPos(conPointPos);
            addedComponents.push_back(item);
            mView.Scene()->addItem(item);
        }
    }
}

void CoreLogic::OnShowClockConfiguratorRequest(ClockMode pMode, uint32_t pToggle, uint32_t pPulse)
{
    emit ShowClockConfiguratorSignal(pMode, pToggle, pPulse);
}

void CoreLogic::OnLeftMouseButtonPressedWithoutCtrl(QPointF pMappedPos, QMouseEvent &pEvent)
{
    auto snappedPos = SnapToGrid(pMappedPos);

    emit HideClockConfiguratorSignal();

    // Add ConPoint on X crossing
    if (mControlMode == ControlMode::EDIT
            && mView.Scene()->selectedItems().empty()                                               // Scene must be empty (select of clicked item did not yet happen)
            && dynamic_cast<LogicWire*>(mView.Scene()->itemAt(pMappedPos, QTransform())) != nullptr // Wire is clicked (not crossing below other component)
            && IsXCrossingPoint(snappedPos)                                                         // There is an X-crossing at that position
            && !IsComponentAtPosition<ConPoint>(snappedPos))                                        // There is no ConPoint at that position yet
    {
        auto item = new ConPoint(this); // Create a new ConPoint (removing will be handled by OnConnectionTypeChanged)
        item->setPos(snappedPos);
        std::vector<IBaseComponent*> addedComponents{item};
        mView.Scene()->addItem(item);
        AppendUndo(new UndoAddType(addedComponents));
        return;
    }

    if (mControlMode == ControlMode::EDIT
            && mView.Scene()->selectedItems().empty()) // Invert in/output connectors
    {
        for (const auto& item : mView.Scene()->items(pMappedPos, Qt::IntersectsItemBoundingRect))
        {
            if ((nullptr != dynamic_cast<AbstractGate*>(item)) || (nullptr != dynamic_cast<AbstractComplexLogic*>(item)) || (nullptr != dynamic_cast<LogicClock*>(item)))
            {
                const auto& connector = static_cast<IBaseComponent*>(item)->InvertConnectorByPoint(pMappedPos);
                if (connector.has_value())
                {
                    auto data = std::make_shared<undo::ConnectorInversionChangedData>(static_cast<IBaseComponent*>(item), connector.value());
                    AppendUndo(new UndoConfigureType(data));
                    return;
                }
            }
        }
    }

    if (mControlMode == ControlMode::ADD) // Add component at the current position
    {
        const auto&& success = AddCurrentTypeComponent(snappedPos);
        if (success)
        {
            // A new component has been added => clear selection if it wasn't a text label
            if (mView.Scene()->selectedItems().size() != 1 || dynamic_cast<TextLabel*>(mView.Scene()->selectedItems()[0]) == nullptr)
            {
                ClearSelection();
            }
            return;
        }
    }

    if (mControlMode == ControlMode::WIRE) // Start the preview wire at the current position
    {
        SetPreviewWireStart(snappedPos);
        return;
    }

    emit MousePressedEventDefaultSignal(pEvent);
}

void CoreLogic::AbortPastingIfInCopy()
{
    if (mControlMode != ControlMode::COPY)
    {
        return;
    }

    RemoveCurrentPaste();

    if (mCurrentCopyUndoType.has_value()) // delete current copy undo action if existing
    {
        delete mCurrentCopyUndoType.value();
        mCurrentCopyUndoType.reset();
    }

    EnterControlMode(ControlMode::EDIT);
}

void CoreLogic::FinishPaste()
{
    for (auto& comp : mCurrentPaste)
    {
        if ((nullptr != dynamic_cast<IBaseComponent*>(comp))
                && IsCollidingComponent(static_cast<IBaseComponent*>(comp))
                && !GetCollidingComponents(static_cast<IBaseComponent*>(comp), false).empty())
        {
            AbortPastingIfInCopy();
            return;
        }
    }

    if (!mCurrentCopyUndoType.has_value() || !mCurrentCopyUndoType.value()->IsCompleted())
    {
        OnSelectedComponentsMovedOrPasted(QPointF(0, 0));
    }

    mCurrentPaste.clear();
}

void CoreLogic::RemoveCurrentPaste()
{
    for (auto& comp : mCurrentPaste)
    {
        mView.Scene()->removeItem(comp);
        delete comp;
    }

    mCurrentPaste.clear();
}

void CoreLogic::OnConnectionTypeChanged(ConPoint* pConPoint, ConnectionType pPreviousType, ConnectionType pCurrentType)
{
    Q_ASSERT(pConPoint);

    if (IsXCrossingPoint(pConPoint->pos()) && pPreviousType == ConnectionType::DIODE_X)
    {
        pConPoint->setSelected(false);
        pConPoint->SetConnectionType(pPreviousType); // Restore old connection type in case delete is undone
        mView.Scene()->removeItem(pConPoint);

        auto deleted = std::vector<IBaseComponent*>{pConPoint};
        AppendUndo(new UndoDeleteType(deleted));
    }
    else
    {
        auto data = std::make_shared<undo::ConnectionTypeChangedData>(pConPoint, pPreviousType, pCurrentType);
        AppendUndo(new UndoConfigureType(data));
    }
}

void CoreLogic::OnTextLabelContentChanged(TextLabel* pTextLabel, const QString& pPreviousText, const QString& pCurrentText)
{
    Q_ASSERT(pTextLabel);

    auto data = std::make_shared<undo::TextLabelContentChangedData>(pTextLabel, pPreviousText, pCurrentText);
    AppendUndo(new UndoConfigureType(data));
}

void CoreLogic::CopySelectedComponents()
{
    QList<QGraphicsItem*> componentsToCopy = mView.Scene()->selectedItems();

    if (componentsToCopy.empty() || mControlMode == ControlMode::COPY)
    {
        return;
    }

    // Remove previous copy components
    for (auto& comp : mCopiedComponents)
    {
        delete comp;
    }

    mCopiedComponents.clear();

    for (const auto& orig : componentsToCopy)
    {
        // Create a copy of the original component
        IBaseComponent* copy = static_cast<IBaseComponent*>(orig)->CloneBaseComponent(this);
        Q_ASSERT(copy);

        copy->setPos(SnapToGrid(orig->pos() + QPointF(canvas::GRID_SIZE, canvas::GRID_SIZE)));

        mCopiedComponents.push_back(copy);
    }
}

void CoreLogic::CutSelectedComponents()
{
    AbortPastingIfInCopy();

    if (mControlMode == ControlMode::EDIT)
    {
        CopySelectedComponents();
        DeleteSelectedComponents();
    }
}

void CoreLogic::PasteCopiedComponents()
{
    if (mCopiedComponents.empty() || mControlMode == ControlMode::COPY)
    {
        return;
    }

    EnterControlMode(ControlMode::COPY);

    ClearSelection();

    mCurrentPaste.clear();

    for (const auto& comp : mCopiedComponents)
    {
        // Create a copy of the copy component
        IBaseComponent* copy = static_cast<IBaseComponent*>(comp)->CloneBaseComponent(this);
        Q_ASSERT(copy);

        copy->setPos(comp->pos());
        copy->setSelected(true);
        copy->ResetZValue();
        copy->setZValue(copy->zValue() + 100); // Bring copied components to front
        mView.Scene()->addItem(copy);
        mCurrentPaste.push_back(copy);
    }

    // Delete previous copy action if aborted to prevent memory leak
    if (mCurrentCopyUndoType.has_value() && !mCurrentCopyUndoType.value()->IsCompleted())
    {
        delete mCurrentCopyUndoType.value();
    }
    mCurrentCopyUndoType = new UndoCopyType(mCurrentPaste);
}

void CoreLogic::DeleteSelectedComponents()
{
    QList<QGraphicsItem*> componentsToDelete = mView.Scene()->selectedItems();
    std::vector<IBaseComponent*> deletedComponents{};
    for (auto& comp : componentsToDelete)
    {
        // Do not allow deleting of ConPoints on T crossings
        if (dynamic_cast<ConPoint*>(comp) == nullptr || IsXCrossingPoint(comp->pos()))
        {
            mView.Scene()->removeItem(comp);
            deletedComponents.push_back(static_cast<IBaseComponent*>(comp));
        }
    }

    // Delete all colliding ConPoints that are not over a crossing anymore
    for (auto& comp : FilterForWires(componentsToDelete))
    {
        for (const auto& collidingComp : mView.Scene()->collidingItems(comp))
        {
            if (dynamic_cast<ConPoint*>(collidingComp) != nullptr && IsNoCrossingPoint(static_cast<ConPoint*>(collidingComp)))
            {
                mView.Scene()->removeItem(collidingComp);
                deletedComponents.push_back(static_cast<IBaseComponent*>(collidingComp));
            }
        }
    }

    if (deletedComponents.size() > 0)
    {
        AppendUndo(new UndoDeleteType(deletedComponents));
    }
    ClearSelection();
}

QJsonObject CoreLogic::GetJson() const
{
    QJsonObject json;
    QJsonArray components;
    SwVersion minVersion(0, 0, 0);

    for (const auto& item : mView.Scene()->items())
    {
        if (nullptr != dynamic_cast<IBaseComponent*>(item))
        {
            components.append(static_cast<IBaseComponent*>(item)->GetJson());
            auto version = static_cast<IBaseComponent*>(item)->GetMinVersion();
            minVersion = GetNewerVersion(minVersion, version);
        }
    }

    json[file::JSON_COMPONENTS_IDENTIFIER] = components;

    json[file::JSON_MAJOR_VERSION_IDENTIFIER] = MAJOR_VERSION;
    json[file::JSON_MINOR_VERSION_IDENTIFIER] = MINOR_VERSION;
    json[file::JSON_PATCH_VERSION_IDENTIFIER] = PATCH_VERSION;

    json[file::JSON_COMPATIBLE_MAJOR_VERSION_IDENTIFIER] = minVersion.major;
    json[file::JSON_COMPATIBLE_MINOR_VERSION_IDENTIFIER] = minVersion.minor;
    json[file::JSON_COMPATIBLE_PATCH_VERSION_IDENTIFIER] = minVersion.patch;

    return json;
}

void CoreLogic::NewCircuit()
{
    EnterControlMode(ControlMode::EDIT); // Always start in edit mode after loading

    // Delete all components
    for (const auto& item : mView.Scene()->items())
    {
        mView.Scene()->removeItem(item);
    }

    mView.ResetViewport();

    // Clear undo and redo stacks
    mUndoQueue.clear();
    mRedoQueue.clear();

    emit UpdateUndoRedoEnabledSignal();

    mCircuitFileParser.ResetCurrentFileInfo();
}

void CoreLogic::ReadJson(const QFileInfo& pFileInfo, const QJsonObject& pJson)
{
    EnterControlMode(ControlMode::EDIT); // Always start in edit mode after loading

    if (pJson.contains(file::JSON_COMPATIBLE_MAJOR_VERSION_IDENTIFIER) && pJson[file::JSON_COMPATIBLE_MAJOR_VERSION_IDENTIFIER].isDouble() &&
        pJson.contains(file::JSON_COMPATIBLE_MINOR_VERSION_IDENTIFIER) && pJson[file::JSON_COMPATIBLE_MINOR_VERSION_IDENTIFIER].isDouble() &&
        pJson.contains(file::JSON_COMPATIBLE_PATCH_VERSION_IDENTIFIER) && pJson[file::JSON_COMPATIBLE_PATCH_VERSION_IDENTIFIER].isDouble())
    {
        const auto major = pJson[file::JSON_COMPATIBLE_MAJOR_VERSION_IDENTIFIER].toInt();
        const auto minor = pJson[file::JSON_COMPATIBLE_MINOR_VERSION_IDENTIFIER].toInt();
        const auto patch = pJson[file::JSON_COMPATIBLE_PATCH_VERSION_IDENTIFIER].toInt();

        if (CompareWithCurrentVersion(SwVersion(major, minor, patch)) > 0) // version is newer
        {
            emit FileHasNewerIncompatibleVersionSignal(QString("%0.%1.%2").arg(major).arg(minor).arg(patch));
            return;
        }
    }

    if (pJson.contains(file::JSON_MAJOR_VERSION_IDENTIFIER) && pJson[file::JSON_MAJOR_VERSION_IDENTIFIER].isDouble() &&
        pJson.contains(file::JSON_MINOR_VERSION_IDENTIFIER) && pJson[file::JSON_MINOR_VERSION_IDENTIFIER].isDouble() &&
        pJson.contains(file::JSON_PATCH_VERSION_IDENTIFIER) && pJson[file::JSON_PATCH_VERSION_IDENTIFIER].isDouble())
    {
        const auto major = pJson[file::JSON_MAJOR_VERSION_IDENTIFIER].toInt();
        const auto minor = pJson[file::JSON_MINOR_VERSION_IDENTIFIER].toInt();
        const auto patch = pJson[file::JSON_PATCH_VERSION_IDENTIFIER].toInt();

        if (CompareWithCurrentVersion(SwVersion(major, minor, patch)) > 0) // version is newer
        {
            emit FileHasNewerCompatibleVersionSignal(QString("%0.%1.%2").arg(major).arg(minor).arg(patch));
        }
    }

    // Delete all components
    for (const auto& item : mView.Scene()->items())
    {
        mView.Scene()->removeItem(item);
    }

    mView.ResetViewport();

    // Create components
    if (pJson.contains(file::JSON_COMPONENTS_IDENTIFIER) && pJson[file::JSON_COMPONENTS_IDENTIFIER].isArray())
    {
        auto components = pJson[file::JSON_COMPONENTS_IDENTIFIER].toArray();

        for (uint32_t compIndex = 0; compIndex < components.size(); compIndex++)
        {
            auto component = components[compIndex].toObject();

            if (!CreateComponent(component))
            {
                qDebug() << "Component unknown";
            }
        }
    }

    // Clear undo and redo stacks
    mUndoQueue.clear();
    mRedoQueue.clear();

    emit UpdateUndoRedoEnabledSignal();
    emit OpeningFileSuccessfulSignal(pFileInfo);
}

bool CoreLogic::CreateComponent(const QJsonObject &pJson)
{
    if (pJson.contains(file::JSON_TYPE_IDENTIFIER) && pJson[file::JSON_TYPE_IDENTIFIER].isDouble())
    {
        IBaseComponent* item = nullptr;
        switch (pJson[file::JSON_TYPE_IDENTIFIER].toInt())
        {
            case file::ComponentId::AND_GATE:
            {
                item = new AndGate(this, pJson);
                break;
            }
            case file::ComponentId::OR_GATE:
            {
                item = new OrGate(this, pJson);
                break;
            }
            case file::ComponentId::XOR_GATE:
            {
                item = new XorGate(this, pJson);
                break;
            }
            case file::ComponentId::NOT_GATE:
            {
                item = new NotGate(this, pJson);
                break;
            }
            case file::ComponentId::BUFFER_GATE:
            {
                item = new BufferGate(this, pJson);
                break;
            }
            case file::ComponentId::WIRE:
            {
                item = new LogicWire(this, pJson);
                break;
            }
            case file::ComponentId::CONPOINT:
            {
                item = new ConPoint(this, pJson);
                break;
            }
            case file::ComponentId::TEXT_LABEL:
            {
                item = new TextLabel(this, pJson);
                break;
            }
            case file::ComponentId::INPUT:
            {
                item = new LogicInput(this, pJson);
                break;
            }
            case file::ComponentId::CONSTANT:
            {
                item = new LogicConstant(this, pJson);
                break;
            }
            case file::ComponentId::BUTTON:
            {
                item = new LogicButton(this, pJson);
                break;
            }
            case file::ComponentId::CLOCK:
            {
                item = new LogicClock(this, pJson);
                break;
            }
            case file::ComponentId::OUTPUT:
            {
                item = new LogicOutput(this, pJson);
                break;
            }
            case file::ComponentId::HALF_ADDER:
            {
                item = new HalfAdder(this, pJson);
                break;
            }
            case file::ComponentId::FULL_ADDER:
            {
                item = new FullAdder(this, pJson);
                break;
            }
            case file::ComponentId::RS_FLIPFLOP:
            {
                item = new RsFlipFlop(this, pJson);
                break;
            }
            case file::ComponentId::D_FLIPFLOP:
            {
                item = new DFlipFlop(this, pJson);
                break;
            }
            case file::ComponentId::D_MS_FLIPFLOP:
            {
                item = new DMasterSlaveFlipFlop(this, pJson);
                break;
            }
            case file::ComponentId::T_FLIPFLOP:
            {
                item = new TFlipFlop(this, pJson);
                break;
            }
            case file::ComponentId::JK_FLIPFLOP:
            {
                item = new JKFlipFlop(this, pJson);
                break;
            }
            case file::ComponentId::JK_MS_FLIPFLOP:
            {
                item = new JkMasterSlaveFlipFlop(this, pJson);
                break;
            }
            case file::ComponentId::RS_MS_FLIPFLOP:
            {
                item = new RsMasterSlaveFlipFlop(this, pJson);
                break;
            }
            case file::ComponentId::RS_CLOCKED_FLIPFLOP:
            {
                item = new RsClockedFlipFlop(this, pJson);
                break;
            }
            case file::ComponentId::MULTIPLEXER:
            {
                item = new Multiplexer(this, pJson);
                break;
            }
            case file::ComponentId::DEMULTIPLEXER:
            {
                item = new Demultiplexer(this, pJson);
                break;
            }
            case file::ComponentId::DECODER:
            {
                item = new Decoder(this, pJson);
                break;
            }
            case file::ComponentId::ENCODER:
            {
                item = new Encoder(this, pJson);
                break;
            }
            case file::ComponentId::SHIFTREGISTER:
            {
                item = new ShiftRegister(this, pJson);
                break;
            }
            case file::ComponentId::COUNTER:
            {
                item = new Counter(this, pJson);
                break;
            }
            default:
            {
                // component unknown by this SW version
                return false;
                break;
            }
        }

        if (nullptr != item)
        {
            mView.Scene()->addItem(item);
            return true;
        }
    }

    // JSON array does not contain a type
    return false;
}

void CoreLogic::AppendUndo(UndoBaseType* pUndoObject)
{
    Q_ASSERT(pUndoObject);

    mCircuitFileParser.MarkAsModified();
    AppendToUndoQueue(pUndoObject, mUndoQueue);
    mRedoQueue.clear();

    emit UpdateUndoRedoEnabledSignal();
}

void CoreLogic::AppendToUndoQueue(UndoBaseType* pUndoObject, std::deque<UndoBaseType*> &pQueue)
{
    Q_ASSERT(pUndoObject);

    pQueue.push_back(pUndoObject);
    if (pQueue.size() > MAX_UNDO_STACK_SIZE)
    {
        delete pQueue.front();
        pQueue.pop_front();
    }
}

void CoreLogic::Undo()
{
    AbortPastingIfInCopy();

    if (mUndoQueue.size() > 0)
    {
        UndoBaseType* undoObject = mUndoQueue.back();
        mUndoQueue.pop_back();
        Q_ASSERT(undoObject);

        switch (undoObject->Type())
        {
            case undo::Type::ADD:
            {
                for (const auto& comp : static_cast<UndoAddType*>(undoObject)->AddedComponents())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->removeItem(comp);
                }
                for (const auto& comp : static_cast<UndoAddType*>(undoObject)->DeletedComponents())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->addItem(comp);
                }
                AppendToUndoQueue(undoObject, mRedoQueue);
                break;
            }
            case undo::Type::DEL:
            {
                for (const auto& comp : static_cast<UndoDeleteType*>(undoObject)->Components())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->addItem(comp);
                }
                AppendToUndoQueue(undoObject, mRedoQueue);
                break;
            }
            case undo::Type::MOVE:
            {
                const auto undoMoveObject = static_cast<UndoMoveType*>(undoObject);
                for (const auto& comp : static_cast<UndoMoveType*>(undoObject)->DeletedComponents())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->addItem(comp);
                }
                for (const auto& comp : static_cast<UndoMoveType*>(undoObject)->AddedComponents())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->removeItem(comp);
                }
                for (const auto& comp : undoMoveObject->MovedComponents())
                {
                    Q_ASSERT(comp);
                    comp->moveBy(-undoMoveObject->Offset().x(), -undoMoveObject->Offset().y());
                }
                AppendToUndoQueue(undoObject, mRedoQueue);
                break;
            }
            case undo::Type::CONFIGURE:
            {
                const auto undoConfigureObject = static_cast<UndoConfigureType*>(undoObject);
                switch (undoConfigureObject->Data()->Type())
                {
                    case undo::ConfigType::CONNECTION_TYPE:
                    {
                        auto data = std::static_pointer_cast<undo::ConnectionTypeChangedData>(undoConfigureObject->Data());
                        Q_ASSERT(data->conPoint);
                        data->conPoint->SetConnectionType(data->previousType);
                        AppendToUndoQueue(undoObject, mRedoQueue);
                        break;
                    }
                case undo::ConfigType::TEXTLABEL_CONTENT:
                {
                    auto data = std::static_pointer_cast<undo::TextLabelContentChangedData>(undoConfigureObject->Data());
                    Q_ASSERT(data->textLabel);
                    data->textLabel->SetTextContent(data->previousText);
                    AppendToUndoQueue(undoObject, mRedoQueue);
                    break;
                }
                case undo::ConfigType::CONNECTOR_INVERSION:
                {
                    auto data = std::static_pointer_cast<undo::ConnectorInversionChangedData>(undoConfigureObject->Data());
                    Q_ASSERT(data->component);
                    Q_ASSERT(data->logicConnector);
                    data->component->InvertConnectorByPoint(data->component->pos() + data->logicConnector->pos);
                    AppendToUndoQueue(undoObject, mRedoQueue);
                    break;
                }
                }
                break;
            }
            case undo::Type::COPY:
            {
                const auto undoCopyObject = static_cast<UndoCopyType*>(undoObject);
                for (const auto& comp : static_cast<UndoCopyType*>(undoObject)->DeletedComponents())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->addItem(comp);
                }
                for (const auto& comp : static_cast<UndoCopyType*>(undoObject)->AddedComponents())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->removeItem(comp);
                }
                for (const auto& comp : undoCopyObject->MovedComponents())
                {
                    Q_ASSERT(comp);
                    comp->moveBy(-undoCopyObject->Offset().x(), -undoCopyObject->Offset().y());
                }
                AppendToUndoQueue(undoObject, mRedoQueue);
                break;
            }
            default:
            {
                break;
            }
        }
        mCircuitFileParser.MarkAsModified();
    }
    ClearSelection();
}

void CoreLogic::Redo()
{
    AbortPastingIfInCopy();

    if (mRedoQueue.size() > 0)
    {
        UndoBaseType* redoObject = mRedoQueue.back();
        mRedoQueue.pop_back();
        Q_ASSERT(redoObject);

        switch (redoObject->Type())
        {
            case undo::Type::ADD:
            {
                for (const auto& comp : static_cast<UndoAddType*>(redoObject)->AddedComponents())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->addItem(comp);
                }
                for (const auto& comp : static_cast<UndoAddType*>(redoObject)->DeletedComponents())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->removeItem(comp);
                }
                AppendToUndoQueue(redoObject, mUndoQueue);
                break;
            }
            case undo::Type::DEL:
            {
                for (const auto& comp : static_cast<UndoDeleteType*>(redoObject)->Components())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->removeItem(comp);
                }
                AppendToUndoQueue(redoObject, mUndoQueue);
                break;
            }
            case undo::Type::MOVE:
            {
                const auto redoMoveObject = static_cast<UndoMoveType*>(redoObject);
                for (const auto& comp : redoMoveObject->MovedComponents())
                {
                    Q_ASSERT(comp);
                    comp->moveBy(redoMoveObject->Offset().x(), redoMoveObject->Offset().y());
                }
                for (const auto& comp : static_cast<UndoMoveType*>(redoObject)->AddedComponents())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->addItem(comp);
                }
                for (const auto& comp : static_cast<UndoMoveType*>(redoObject)->DeletedComponents())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->removeItem(comp);
                }
                AppendToUndoQueue(redoObject, mUndoQueue);
                break;
            }
            case undo::Type::CONFIGURE:
            {
                const auto undoConfigureObject = static_cast<UndoConfigureType*>(redoObject);
                switch (undoConfigureObject->Data()->Type())
                {
                    case undo::ConfigType::CONNECTION_TYPE:
                    {
                        auto data = std::static_pointer_cast<undo::ConnectionTypeChangedData>(undoConfigureObject->Data());
                        Q_ASSERT(data->conPoint);
                        data->conPoint->SetConnectionType(data->currentType);
                        AppendToUndoQueue(redoObject, mUndoQueue);
                        break;
                    }
                    case undo::ConfigType::TEXTLABEL_CONTENT:
                    {
                        auto data = std::static_pointer_cast<undo::TextLabelContentChangedData>(undoConfigureObject->Data());
                        Q_ASSERT(data->textLabel);
                        data->textLabel->SetTextContent(data->currentText);
                        AppendToUndoQueue(redoObject, mUndoQueue);
                        break;
                    }
                    case undo::ConfigType::CONNECTOR_INVERSION:
                    {
                        auto data = std::static_pointer_cast<undo::ConnectorInversionChangedData>(undoConfigureObject->Data());
                        Q_ASSERT(data->component);
                        Q_ASSERT(data->logicConnector);
                        data->component->InvertConnectorByPoint(data->component->pos() + data->logicConnector->pos);
                        AppendToUndoQueue(redoObject, mUndoQueue);
                        break;
                    }
                }
                break;
            }
            case undo::Type::COPY:
            {
                const auto redoCopyObject = static_cast<UndoCopyType*>(redoObject);
                for (const auto& comp : redoCopyObject->MovedComponents())
                {
                    Q_ASSERT(comp);
                    comp->moveBy(redoCopyObject->Offset().x(), redoCopyObject->Offset().y());
                }
                for (const auto& comp : static_cast<UndoCopyType*>(redoObject)->AddedComponents())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->addItem(comp);
                }
                for (const auto& comp : static_cast<UndoCopyType*>(redoObject)->DeletedComponents())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->removeItem(comp);
                }
                AppendToUndoQueue(redoObject, mUndoQueue);
                break;
            }
            default:
            {
                break;
            }
        }
        mCircuitFileParser.MarkAsModified();
    }
    ClearSelection();
}
