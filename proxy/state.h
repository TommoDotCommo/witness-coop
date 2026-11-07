#pragma once
void StateStart();        // starts the detector thread (polls panels and doors, sends events)
bool StateNativeLine(unsigned panel);   // the game draws this panel's line itself (a remote solve wrote it): the overlay leaves it out
void StateApplyRemote();  // render thread: apply events received from the peer
