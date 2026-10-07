#pragma once

// The bench builds on the app's precompiled header and adds what only the bench
// uses (console output, timer resolution and the synthetic stroke path).
#include "greenflame/pch.h"

#include <cmath>
#include <format>
#include <iostream>
#include <iterator>
#include <timeapi.h>
