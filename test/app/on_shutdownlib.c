#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <luaconf.h>

#include <module.h>
#include "../unit/unit.h"

struct module {
	/*
	 * Flag of the module state, May be -1, 0, 1
	 * -1 means that module thread failed to start
	 * 0 means that module currently stop
	 * 1 menas that module currently running
	 */
	int is_running;
	/*
	 * Module fiber
	 */
	struct fiber *fiber;
};

static struct module module;

/** Shutdown function to test on_shutdown API */
static int
on_shutdown_module_bad_func(void *arg)
{
	fail_unless(0);
	return 0;
}

/** Shutdown function to stop the main fiber of the module */
static int
on_shutdown_module_stop_func(void *arg)
{
	fprintf(stderr, "stop module fiber\n");
	fiber_wakeup(module.fiber);
	fprintf(stderr, "join module fiber\n");
	fiber_join(module.fiber);
	fprintf(stderr, "join module fiber finished\n");
	return 0;
}

/** Main module thread function. */
static int
module_fiber_f(va_list arg)
{

	__atomic_store_n(&module.is_running, 1, __ATOMIC_SEQ_CST);
	fiber_yield();
	fiber_sleep(1);
	__atomic_store_n(&module.is_running, 0, __ATOMIC_SEQ_CST);
	fprintf(stderr, "module_fiber_f finished\n");
	return 0;
}

static int
cfg(lua_State *L)
{
	/** In case module already started do nothing */
	if (__atomic_load_n(&module.is_running, __ATOMIC_SEQ_CST) == 1)
		return 0;
	/** Registering the module stop function */
	fail_unless(box_on_shutdown(&module,
				    on_shutdown_module_bad_func,
				    NULL) == 0);
	/** Changing the module stop function */
	fail_unless(box_on_shutdown(&module,
				    on_shutdown_module_stop_func,
				    on_shutdown_module_bad_func) == 0);
	module.fiber = fiber_new("fiber", module_fiber_f);
	fail_unless(module.fiber != NULL);
	fiber_set_joinable(module.fiber, true);
	fiber_start(module.fiber);
	return 0;
}

static const struct luaL_Reg on_shutdownlib[] = {
	{"cfg", cfg},
	{NULL, NULL}
};

LUALIB_API int
luaopen_on_shutdownlib(lua_State *L)
{
	luaL_openlib(L, "on_shutdownlib", on_shutdownlib, 0);
	return 0;
}
