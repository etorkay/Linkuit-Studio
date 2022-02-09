#include "CoreLogic.h"

#include "Components/Gates/AndGate.h"
#include "Components/Gates/OrGate.h"
#include "Components/Gates/XorGate.h"
#include "Components/Gates/NotGate.h"
#include "Components/LogicInput.h"
#include "Components/LogicOutput.h"

#include "Undo/UndoAddType.h"
#include "Undo/UndoDeleteType.h"
#include "Undo/UndoMoveType.h"
#include "Undo/UndoConfigureType.h"

#include "HelperFunctions.h"

CoreLogic::CoreLogic(View &pView):
    mView(pView),
    mHorizontalPreviewWire(WireDirection::HORIZONTAL, 0),
    mVerticalPreviewWire(WireDirection::VERTICAL, 0)
{
    ConnectToView();
    mView.Init();
}

void CoreLogic::ConnectToView()
{
    QObject::connect(this, &CoreLogic::ControlModeChangedSignal, &mView, &View::OnControlModeChanged);
    QObject::connect(this, &CoreLogic::ComponentTypeChangedSignal, &mView, &View::OnComponentTypeChanged);
}

void CoreLogic::EnterControlMode(ControlMode pMode)
{
    mControlMode = pMode;
    emit ControlModeChangedSignal(pMode);
    if (mControlMode == ControlMode::ADD)
    {
        emit ComponentTypeChangedSignal(mComponentType);
    }
}

void CoreLogic::EnterAddControlMode(ComponentType pComponentType)
{
    EnterControlMode(ControlMode::ADD);
    SetComponentType(pComponentType);
}

ComponentType CoreLogic::GetComponentType()
{
    return mComponentType;
}

void CoreLogic::SetComponentType(ComponentType pComponentType)
{
    mComponentType = pComponentType;
    emit ComponentTypeChangedSignal(mComponentType);
}

QGraphicsItem* CoreLogic::GetItem()
{
    QGraphicsItem* item;

    switch(mComponentType)
    {
        case ComponentType::AND_GATE:
        {
            item = new AndGate(mComponentInputCount, mComponentDirection);
            break;
        }
        case ComponentType::OR_GATE:
        {
            item = new OrGate(mComponentInputCount, mComponentDirection);
            break;
        }
        case ComponentType::XOR_GATE:
        {
            item = new XorGate(mComponentInputCount, mComponentDirection);
            break;
        }
        case ComponentType::NOT_GATE:
        {
            item = new NotGate(mComponentDirection);
            break;
        }
        case ComponentType::INPUT:
        {
            item = new LogicInput();
            break;
        }
        case ComponentType::OUTPUT:
        {
            item = new LogicOutput();
            break;
        }
        default:
        {
            item = nullptr;
            break;
        }
    }

    return item;
}

ControlMode CoreLogic::GetControlMode()
{
    return mControlMode;
}

void CoreLogic::AddCurrentTypeComponent(QPointF pPosition)
{
    auto item = GetItem();
    Q_ASSERT(item);

    item->setPos(SnapToGrid(pPosition));

    if (FilterOutCollidingComponents(mView.Scene()->collidingItems(item)).empty())
    {
        mView.Scene()->addItem(item);

        auto addedComponents = std::vector<BaseComponent*>{static_cast<BaseComponent*>(item)};
        AppendUndo(new UndoAddType(addedComponents));
    }
    else
    {
        delete item;
    }
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
    std::vector<BaseComponent*> addedComponents;
    std::vector<BaseComponent*> deletedComponents;

    // Add horizontal wire
    if (mPreviewWireStart.x() != snappedEndPoint.x())
    {
        auto item = new LogicWire(WireDirection::HORIZONTAL, std::abs(mPreviewWireStart.x() - snappedEndPoint.x()));

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

        if (startAdjacent)
        {
            deletedComponents.push_back(static_cast<BaseComponent*>(startAdjacent));
            mView.Scene()->removeItem(startAdjacent);
        }

        if (endAdjacent)
        {
            deletedComponents.push_back(static_cast<BaseComponent*>(endAdjacent));
            mView.Scene()->removeItem(endAdjacent);
        }

        mView.Scene()->addItem(horizontalWire);
        addedComponents.push_back(static_cast<BaseComponent*>(horizontalWire));
    }

    // Add vertical wire
    if (mPreviewWireStart.y() != snappedEndPoint.y())
    {
        auto item = new LogicWire(WireDirection::VERTICAL, std::abs(mPreviewWireStart.y() - snappedEndPoint.y()));

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

        if (startAdjacent)
        {
            deletedComponents.push_back(static_cast<BaseComponent*>(startAdjacent));
            mView.Scene()->removeItem(startAdjacent);
        }

        if (endAdjacent)
        {
            deletedComponents.push_back(static_cast<BaseComponent*>(endAdjacent));
            mView.Scene()->removeItem(endAdjacent);
        }

        mView.Scene()->addItem(verticalWire);
        addedComponents.push_back(static_cast<BaseComponent*>(verticalWire));
    }

    AppendUndo(new UndoAddType(addedComponents, deletedComponents));
    mWireStartDirection = WireDirection::UNSET;
}

void CoreLogic::MergeWiresAfterMove(QList<QGraphicsItem*> &pComponents, std::vector<BaseComponent*> &pAddedWires, std::vector<BaseComponent*> &pDeletedWires)
{
    auto wires = FilterForWires(pComponents);

    for (auto &w : wires)
    {
        const auto containedWires = DeleteContainedWires(static_cast<LogicWire*>(w));
        pDeletedWires.insert(pDeletedWires.end(), containedWires.begin(), containedWires.end());

        LogicWire* startAdjacent = nullptr;
        LogicWire* endAdjacent = nullptr;

        if (static_cast<LogicWire*>(w)->GetDirection() == WireDirection::HORIZONTAL)
        {
            startAdjacent = GetAdjacentWire(QPointF(w->x() - 2, w->y()), WireDirection::HORIZONTAL);
            endAdjacent = GetAdjacentWire(QPointF(w->x() + static_cast<LogicWire*>(w)->GetLength() + 2, w->y()), WireDirection::HORIZONTAL);

            auto horizontalWire = MergeWires(static_cast<LogicWire*>(w), startAdjacent, endAdjacent);
            horizontalWire->setSelected(w->isSelected());

            mView.Scene()->addItem(horizontalWire);
            pAddedWires.push_back(static_cast<BaseComponent*>(horizontalWire));
        }
        else
        {
            startAdjacent = GetAdjacentWire(QPointF(w->x(), w->y() - 2), WireDirection::VERTICAL);
            endAdjacent = GetAdjacentWire(QPointF(w->x(), w->y() + static_cast<LogicWire*>(w)->GetLength() + 2), WireDirection::VERTICAL);

            auto verticalWire = MergeWires(static_cast<LogicWire*>(w), startAdjacent, endAdjacent);
            verticalWire->setSelected(w->isSelected());

            mView.Scene()->addItem(verticalWire);
            pAddedWires.push_back(static_cast<BaseComponent*>(verticalWire));
        }

        pDeletedWires.push_back(w);
        mView.Scene()->removeItem(w);

        if (startAdjacent)
        {
            pDeletedWires.push_back(static_cast<BaseComponent*>(startAdjacent));
            mView.Scene()->removeItem(startAdjacent);
        }

        if (endAdjacent)
        {
            pDeletedWires.push_back(static_cast<BaseComponent*>(endAdjacent));
            mView.Scene()->removeItem(endAdjacent);
        }
    }
}

std::vector<BaseComponent*> CoreLogic::DeleteContainedWires(LogicWire* pWire)
{
    std::vector<BaseComponent*> deletedComponents;

    QList<QGraphicsItem*> containedComponents;

    if (pWire->GetDirection() == WireDirection::HORIZONTAL)
    {
        containedComponents = mView.Scene()->items(pWire->x() - 2, pWire->y() - components::wires::BOUNDING_RECT_SIZE / 2 - 2,
                                                   pWire->GetLength() + 4, components::wires::BOUNDING_RECT_SIZE + 4, Qt::ContainsItemShape, Qt::DescendingOrder);
    }
    else
    {
        containedComponents = mView.Scene()->items(pWire->x() - components::wires::BOUNDING_RECT_SIZE / 2 - 2, pWire->y() - 2,
                                                   components::wires::BOUNDING_RECT_SIZE + 4, pWire->GetLength() + 4, Qt::ContainsItemShape, Qt::DescendingOrder);
    }
    const auto containedWires = FilterForWires(containedComponents, pWire->GetDirection());

    for (auto &wire : containedWires)
    {
        if (wire != pWire)
        {
            deletedComponents.push_back(wire);
            mView.Scene()->removeItem(wire);
        }
    }

    return deletedComponents;
}

LogicWire* CoreLogic::GetAdjacentWire(QPointF pCheckPosition, WireDirection pDirection) const
{
    const auto startAdjacentComponents = mView.Scene()->items(pCheckPosition.x(), pCheckPosition.y(), 1, 1, Qt::IntersectsItemShape, Qt::DescendingOrder);
    const auto startAdjacentWires = FilterForWires(startAdjacentComponents, pDirection);

    if (startAdjacentWires.size() > 0)
    {
        return static_cast<LogicWire*>(startAdjacentWires.at(0));
    }

    return nullptr;
}

std::vector<BaseComponent*> CoreLogic::FilterForWires(const QList<QGraphicsItem*> &pComponents, WireDirection pDirection) const
{
    std::vector<BaseComponent*> wires;

    for (auto &comp : pComponents)
    {
        if (dynamic_cast<LogicWire*>(comp) != nullptr
                && (dynamic_cast<LogicWire*>(comp)->GetDirection() == pDirection || pDirection == WireDirection::UNSET))
        {
            wires.push_back(static_cast<BaseComponent*>(comp));
        }
    }

    return wires;
}

std::vector<BaseComponent*> CoreLogic::FilterOutCollidingComponents(const QList<QGraphicsItem*> &pComponents) const
{
    std::vector<BaseComponent*> collidingComponents;

    for (auto &comp : pComponents)
    {
        if (dynamic_cast<LogicWire*>(comp) == nullptr)
        {
            collidingComponents.push_back(static_cast<BaseComponent*>(comp));
        }
    }

    return collidingComponents;
}

bool CoreLogic::IsCollidingComponent(QGraphicsItem* pComponent) const
{
    return (dynamic_cast<LogicWire*>(pComponent) == nullptr);
}

LogicWire* CoreLogic::MergeWires(LogicWire* pNewWire, LogicWire* pStartAdjacent, LogicWire* pEndAdjacent) const
{
    QPointF newStart(pNewWire->pos());

    if (pNewWire->GetDirection() == WireDirection::HORIZONTAL)
    {
        QPointF newEnd(pNewWire->x() + pNewWire->GetLength(), pNewWire->y());

        if (pStartAdjacent && pStartAdjacent->GetDirection() == pNewWire->GetDirection())
        {
            Q_ASSERT(pNewWire->y() == pStartAdjacent->y());
            newStart = QPointF(pStartAdjacent->x(), pNewWire->y());
        }
        if (pEndAdjacent && pEndAdjacent->GetDirection() == pNewWire->GetDirection())
        {
            Q_ASSERT(pNewWire->y() == pEndAdjacent->y());
            newEnd = QPoint(pEndAdjacent->x() + pEndAdjacent->GetLength(), pNewWire->y());
        }

        auto newWire = new LogicWire(WireDirection::HORIZONTAL, newEnd.x() - newStart.x());
        newWire->setPos(newStart);
        return newWire;
    }
    else
    {
        QPointF newEnd(pNewWire->x(), pNewWire->y() + pNewWire->GetLength());

        if (pStartAdjacent && pStartAdjacent->GetDirection() == pNewWire->GetDirection())
        {
            Q_ASSERT(pNewWire->x() == pStartAdjacent->x());
            newStart = QPointF(pNewWire->x(), pStartAdjacent->y());
        }
        if (pEndAdjacent && pEndAdjacent->GetDirection() == pNewWire->GetDirection())
        {
            Q_ASSERT(pNewWire->x() == pEndAdjacent->x());
            newEnd = QPoint(pNewWire->x(), pEndAdjacent->y() + pEndAdjacent->GetLength());
        }

        auto newWire = new LogicWire(WireDirection::VERTICAL, newEnd.y() - newStart.y());
        newWire->setPos(newStart);
        return newWire;
    }
}

void CoreLogic::OnSelectedComponentsMoved(QPointF pOffset)
{
    if (pOffset.manhattanLength() <= 0)
    {
        return;
    }

    QList<QGraphicsItem*> movedGraphicsItems = mView.Scene()->selectedItems();

    std::vector<BaseComponent*> movedComponents{};
    for (const auto& comp : movedGraphicsItems)
    {
        if (IsCollidingComponent(comp) && !FilterOutCollidingComponents(mView.Scene()->collidingItems(comp, Qt::IntersectsItemShape)).empty())
        {
            // Collision, revert moving
            for (const auto& comp : movedGraphicsItems)
            {
                comp->moveBy(-pOffset.x(), -pOffset.y());
            }
            return;
        }
        movedComponents.push_back(static_cast<BaseComponent*>(comp));
    }

    if (movedComponents.size() > 0)
    {
        std::vector<BaseComponent*> addedWires;
        std::vector<BaseComponent*> deletedWires;

        MergeWiresAfterMove(movedGraphicsItems, addedWires, deletedWires);
        AppendUndo(new UndoMoveType(movedComponents, addedWires, deletedWires, pOffset));
    }
}

void CoreLogic::CopySelectedComponents()
{
    QList<QGraphicsItem*> componentsToCopy = mView.Scene()->selectedItems();
    std::vector<BaseComponent*> addedComponents{};

    mView.Scene()->clearSelection();

    for (auto& orig : componentsToCopy)
    {
        // Create a copy of the original component
        BaseComponent* copy = static_cast<BaseComponent*>(orig)->CloneBaseComponent();
        Q_ASSERT(copy);

        // Paste the copied component one grid cell below and to the right
        copy->setPos(SnapToGrid(orig->pos() + QPointF(canvas::GRID_SIZE, canvas::GRID_SIZE)));
        copy->setSelected(true);
        copy->ResetZValue();
        mView.Scene()->addItem(copy);
        addedComponents.push_back(copy);
    }
    if (addedComponents.size() > 0)
    {
        AppendUndo(new UndoAddType(addedComponents));
    }
}

void CoreLogic::DeleteSelectedComponents()
{
    QList<QGraphicsItem*> componentsToDelete = mView.Scene()->selectedItems();
    std::vector<BaseComponent*> deletedComponents{};
    for (auto& comp : componentsToDelete)
    {
        mView.Scene()->removeItem(comp);
        deletedComponents.push_back(static_cast<BaseComponent*>(comp));
    }
    if (deletedComponents.size() > 0)
    {
        AppendUndo(new UndoDeleteType(deletedComponents));
    }
}

void CoreLogic::AppendUndo(UndoBaseType* pUndoObject)
{
    AppendToUndoQueue(pUndoObject, mUndoQueue);
    mRedoQueue.clear();
}

void CoreLogic::AppendToUndoQueue(UndoBaseType* pUndoObject, std::deque<UndoBaseType*> &pQueue)
{
    pQueue.push_back(pUndoObject);
    if (pQueue.size() > MAX_UNDO_STACK_SIZE)
    {
        delete pQueue.front();
        pQueue.pop_front();
    }
}

void CoreLogic::Undo()
{
    if (mUndoQueue.size() > 0)
    {
        UndoBaseType* undoObject = mUndoQueue.back();
        mUndoQueue.pop_back();
        Q_ASSERT(undoObject);

        switch (undoObject->Type())
        {
            case UndoType::ADD:
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
            case UndoType::DEL:
            {
                for (const auto& comp : static_cast<UndoDeleteType*>(undoObject)->Components())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->addItem(comp);
                }
                AppendToUndoQueue(undoObject, mRedoQueue);
                break;
            }
            case UndoType::MOVE:
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
            case UndoType::CONFIGURE:
            {
                break;
            }
            default:
            {
                break;
            }
        }
    }
    mView.Scene()->clearSelection();
}

void CoreLogic::Redo()
{
    if (mRedoQueue.size() > 0)
    {
        UndoBaseType* redoObject = mRedoQueue.back();
        mRedoQueue.pop_back();
        Q_ASSERT(redoObject);

        switch (redoObject->Type())
        {
            case UndoType::ADD:
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
            case UndoType::DEL:
            {
                for (const auto& comp : static_cast<UndoDeleteType*>(redoObject)->Components())
                {
                    Q_ASSERT(comp);
                    mView.Scene()->removeItem(comp);
                }
                AppendToUndoQueue(redoObject, mUndoQueue);
                break;
            }
            case UndoType::MOVE:
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
            case UndoType::CONFIGURE:
            {
                break;
            }
            default:
            {
                break;
            }
        }
    }
    mView.Scene()->clearSelection();
}
