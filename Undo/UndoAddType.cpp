#include "UndoAddType.h"

UndoAddType::UndoAddType(std::vector<IBaseComponent*> &pAddedComponents):
    mAddedComponents(pAddedComponents),
    mDeletedComponents()
{}

UndoAddType::UndoAddType(std::vector<IBaseComponent*> &pAddedComponents, std::vector<IBaseComponent*> &pDeletedComponents):
    mAddedComponents(pAddedComponents),
    mDeletedComponents(pDeletedComponents)
{}

UndoAddType::~UndoAddType()
{
    for (auto& comp : mDeletedComponents)
    {
        delete comp;
    }
}

Undo::Type UndoAddType::Type() const
{
    return Undo::Type::ADD;
}

const std::vector<IBaseComponent*>& UndoAddType::AddedComponents() const
{
    return mAddedComponents;
}

const std::vector<IBaseComponent*>& UndoAddType::DeletedComponents() const
{
    return mDeletedComponents;
}
