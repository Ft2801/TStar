
#include "ThreadState.h"

namespace Threading {

std::atomic<bool> ThreadState::s_shouldRun{true};

}
