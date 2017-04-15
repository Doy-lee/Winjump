#include "..\Winjump.cpp"
#include "..\Config.cpp"

#ifdef WINJUMP_UNITY_BUILD
	#define DQN_IMPLEMENTATION
	#include "..\dqn.h"
#endif
