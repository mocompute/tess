// -- begin std --

// Thread-local storage compatibility
#ifdef _MSC_VER
#define TL_THREAD_LOCAL __declspec(thread)
#else
#define TL_THREAD_LOCAL _Thread_local
#endif

// -- end std --
