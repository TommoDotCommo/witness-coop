#pragma once
void StateStart();        // starts the detector thread (polls panels and doors, sends events)
void StateApplyRemote();  // render thread: apply events received from the peer
