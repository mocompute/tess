// -- begin std --

// Thread-local storage compatibility
#ifdef _MSC_VER
#define TL_THREAD_LOCAL __declspec(thread)
#else
#define TL_THREAD_LOCAL _Thread_local
#endif

// DLL export attribute for shared library symbols
#ifdef _MSC_VER
#define TL_EXPORT __declspec(dllexport)
#else
#define TL_EXPORT __attribute__((visibility("default")))
#endif

// -- end std --
