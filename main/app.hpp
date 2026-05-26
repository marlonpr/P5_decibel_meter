#pragma once
#include "hub75.h"

class App {
public:
    void run();

private:
    Hub75Driver* matrix = nullptr;
};