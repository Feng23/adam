/** 
 * @file cesk_value.c
 * @brief the implementation of abstract value */
#include <log.h>
#include <cesk/cesk_value.h>
#include <dalvik/dalvik_instruction.h>

/** @brief the value list actuall record all values, and deallocate 
 *         the memory when exiting 
 */
static cesk_value_t*  _cesk_value_list = NULL;

/** @brief allocator */
static inline cesk_value_t* _cesk_value_alloc(uint32_t type)
{
    cesk_value_t* ret = (cesk_value_t*)malloc(sizeof(cesk_value_t));
    if(NULL == ret) return NULL;
    ret->type = type;
    ret->refcnt = 0;
    ret->next = _cesk_value_list;
    ret->prev = NULL;
    if(_cesk_value_list) 
        _cesk_value_list->prev = ret;
    _cesk_value_list = ret;
	ret->pointer._void = NULL;
    return ret;
}
void cesk_value_init()
{
    _cesk_value_list = NULL;
}
/** @brief deallocate the memory for the value */
static void _cesk_value_free(cesk_value_t* val)
{
    if(val->prev) val->prev->next = val->next;
    if(val->next) val->next->prev = val->prev;
    if(_cesk_value_list == val) _cesk_value_list = val->next;
    if(NULL == val->pointer._void)
	{
		/* for each type, we use defferent way to deallocate the object */
		switch(val->type)
		{
			case CESK_TYPE_OBJECT:
					cesk_object_free(val->pointer.object);
				break;
			case CESK_TYPE_SET:
				cesk_set_free(val->pointer.set);
				break;
			default:
				LOG_WARNING("unknown type %d, do not know how to free", val->type);
		}
	}
    free(val);
	LOG_DEBUG("a value is deleted"); 
}
void cesk_value_finalize()
{
    cesk_value_t *ptr;
    for(ptr = _cesk_value_list; NULL != ptr; )
    {
        cesk_value_t* old = ptr;
        ptr = ptr->next;
        _cesk_value_free(old);
    }
}

void cesk_value_incref(cesk_value_t* value)
{
    value->refcnt ++;
}
void cesk_value_decref(cesk_value_t* value)
{
    if(--value->refcnt == 0)
        _cesk_value_free(value);
}
cesk_value_t* cesk_value_from_classpath(const char* classpath)
{
    cesk_value_t* ret = _cesk_value_alloc(CESK_TYPE_OBJECT);
    if(NULL == ret) return ret;
    cesk_object_t* class = cesk_object_new(classpath);
    if(NULL == class) 
    {
        _cesk_value_free(ret);
		LOG_ERROR("can not create class %s", classpath);
        return NULL;
    }
    ret->pointer.object = class;
	LOG_DEBUG("an object is built from class %s", classpath);
    return ret;
}
cesk_value_t* cesk_value_empty_set()
{
	cesk_value_t* ret = _cesk_value_alloc(CESK_TYPE_SET);
	
	cesk_set_t* empty_set = cesk_set_empty_set();

	if(NULL == empty_set)
	{
		_cesk_value_free(ret);
		LOG_ERROR("failed to create an empty set for new value");
		return NULL;
	}
	ret->pointer.set = empty_set;
	return ret;
}

cesk_value_t* cesk_value_fork(const cesk_value_t* value)
{
    cesk_value_t* newval = _cesk_value_alloc(value->type);
    if(NULL == newval) goto ERROR;
	const cesk_object_t* object;
	cesk_object_t* newobj;
	const cesk_set_t* set;
	cesk_set_t* newset;
	switch (value->type)
	{
		case CESK_TYPE_OBJECT:
			object = value->pointer.object;
			newobj = cesk_object_fork(object);
			if(NULL == newobj) goto ERROR;
			newval->pointer.object = newobj;
			break;
		case CESK_TYPE_SET:
			set = value->pointer.set;
			newset = cesk_set_fork(set);
			if(NULL == newset) goto ERROR;
			newval->pointer.set = newset;
			break;
		case CESK_TYPE_ARRAY:
			LOG_INFO("fixme: array support");
			/* TODO: array support */
		default:
			LOG_ERROR("unsupported type");
			goto ERROR;
	}
    return newval;
ERROR:
	LOG_ERROR("error during forking a value");
	if(NULL != newval)
		_cesk_value_free(newval);
	return NULL;
}
hashval_t cesk_value_hashcode(const cesk_value_t* value)
{
    if(NULL == value) return (hashval_t)0x3c4fab47;
    switch(value->type)
    {
        case CESK_TYPE_OBJECT:
            /* object */
            return cesk_object_hashcode(value->pointer.object);
        case CESK_TYPE_SET:
            return cesk_set_hashcode(value->pointer.set);
        case CESK_TYPE_ARRAY:
            /* array */
            LOG_INFO("fixme: array type support");
            //return cesk_set_hashcode((*(cesk_value_array_t**)value->data)->values);
        default:
            return 0;
    }
}
hashval_t cesk_value_compute_hashcode(const cesk_value_t* value)
{
    if(NULL == value) return (hashval_t)0x3c4fab47;
    switch(value->type)
    {
        case CESK_TYPE_OBJECT:
            /* object */
            return cesk_object_compute_hashcode(value->pointer.object);
        case CESK_TYPE_SET:
            return cesk_set_compute_hashcode(value->pointer.set);
        case CESK_TYPE_ARRAY:
            /* array */
            LOG_INFO("fixme: array type support");
        default:
            return 0;
    }
}
int cesk_value_equal(const cesk_value_t* first, const cesk_value_t* second)
{
    if(NULL == first || NULL == second) return first == second;
    if(first->type != second->type) return 0;
    switch(first->type)
    {
        case CESK_TYPE_OBJECT:
            return cesk_object_equal(first->pointer.object, second->pointer.object);
        case CESK_TYPE_SET:
            return cesk_set_equal(first->pointer.set, second->pointer.set);
        case CESK_TYPE_ARRAY:
            LOG_INFO("fixme : array type support");
        default:
            LOG_WARNING("can not compare value type %d", first->type);
            return 1;
    }
}
