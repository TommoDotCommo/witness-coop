#pragma once
void StateStart();        // starts the detector thread (polls panels and doors, sends events)
bool StatePanelSolved(unsigned panel);   // solved in this game (another player's finished line is drawn only while it is)
void StateApplyRemote();  // render thread: apply events received from the peer
