#include <anadroid.h>
void anadroid_init(void)
{
    log_init();
    stringpool_init(STRING_POOL_SIZE);
    dalvik_init();
    cesk_init();
}
void anadroid_finalize(void)
{
    dalvik_exception_finalize();
    dalvik_memberdict_finalize();
    cesk_finalize();
    log_finalize();
    dalvik_finalize();
    stringpool_fianlize();
}
