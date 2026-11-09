#pragma once
// The Witness Trainer's co-op options (trainer.cpp, ported from jbzdarkid's witness-trainer): trainer_speed, trainer_fov, trainer_noclip (key_noclip),
// trainer_no_challenge_limit, trainer_ep_overlay in witness_coop.cfg; nothing runs while all of them are off.
void TrainerFrame();                         // render thread, every frame
bool TrainerKey(unsigned vk, bool down);     // window thread: the noclip key (true: taken)
bool TrainerCommand(const char* line);       // test command: trainerstat
