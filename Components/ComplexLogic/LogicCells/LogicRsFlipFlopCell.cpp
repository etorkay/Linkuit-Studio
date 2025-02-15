#include "LogicRsFlipFlopCell.h"

LogicRsFlipFlopCell::LogicRsFlipFlopCell():
    LogicBaseCell(2, 2),
    mCurrentState(2, LogicState::LOW),
    mStateChanged(true)
{}

void LogicRsFlipFlopCell::LogicFunction()
{
    if (mInputStates[0] == LogicState::HIGH) // S high
    {
        if (mCurrentState[0] != LogicState::HIGH || mCurrentState[1] != LogicState::LOW)
        {
            mCurrentState[0] = LogicState::HIGH; // Q
            mCurrentState[1] = LogicState::LOW; // Not Q
            mStateChanged = true;
        }
    }
    if (mInputStates[1] == LogicState::HIGH) // R high
    {
        if (mCurrentState[0] != LogicState::LOW || mCurrentState[1] != LogicState::HIGH)
        {
            mCurrentState[0] = LogicState::LOW; // Q
            mCurrentState[1] = LogicState::HIGH; // Not Q
            mStateChanged = true;
        }
    }
}

LogicState LogicRsFlipFlopCell::GetOutputState(uint32_t pOutput) const
{
    Q_ASSERT(pOutput <= 1);
    if (mOutputInverted[pOutput] && mIsActive)
    {
        return InvertState(mCurrentState[pOutput]);
    }
    else
    {
        return mCurrentState[pOutput];
    }
}

void LogicRsFlipFlopCell::OnSimulationAdvance()
{
    AdvanceUpdateTime();

    if (mStateChanged)
    {
        mStateChanged = false;
        NotifySuccessor(0, mCurrentState[0]);
        NotifySuccessor(1, mCurrentState[1]);

        emit StateChangedSignal();
    }
}

void LogicRsFlipFlopCell::OnWakeUp()
{
    mInputStates = std::vector<LogicState>(mInputStates.size(), LogicState::LOW);

    for (size_t i = 0; i < mInputStates.size(); i++)
    {
        mInputStates[i] = mInputInverted[i] ? LogicState::HIGH : LogicState::LOW;
    }

    mCurrentState[0] = LogicState::LOW; // Q
    mCurrentState[1] = LogicState::HIGH; // Not Q
    mNextUpdateTime = UpdateTime::NOW;

    mStateChanged = true; // Successors should be notified about wake up
    mIsActive = true;
    emit StateChangedSignal();
}

void LogicRsFlipFlopCell::OnShutdown()
{
    mOutputCells = std::vector<std::pair<std::shared_ptr<LogicBaseCell>, uint32_t>>(mOutputCells.size(), std::make_pair(nullptr, 0));
    mInputStates = std::vector<LogicState>(mInputStates.size(), LogicState::LOW);
    mInputConnected = std::vector<bool>(mInputConnected.size(), false);
    mCurrentState = std::vector<LogicState>(mInputStates.size(), LogicState::LOW);
    mIsActive = false;
    emit StateChangedSignal();
}
