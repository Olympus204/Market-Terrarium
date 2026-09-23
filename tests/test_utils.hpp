#pragma once

void require(bool condition, const char* message);
void run_test(const char* name, void (*test)());