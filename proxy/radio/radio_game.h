#pragma once
// Render-owned snapshots; commands request a dump, the next rendered frame writes it.
void RadioRecord(unsigned id, float raise, unsigned vertices, const float* wrist = nullptr);
void RadioRequestDump(unsigned token);
void RadioDumpFrame();
