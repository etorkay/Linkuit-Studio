#ifndef LOGICINPUTCELL_H
#define LOGICINPUTCELL_H

#include "Components/LogicBaseCell.h"

class LogicInputCell : public LogicBaseCell
{
public:
    LogicInputCell(void);

    void LogicFunction(void) override;

    void ToggleState(void);
    LogicState GetOutputState(uint32_t pOutput = 0) const override;

public slots:
    void OnSimulationAdvance(void) override;
    void OnShutdown(void) override;

protected:
    LogicState mState;
    bool mStateChanged;
};

#endif // LOGICINPUTCELL_H
