/**
 * @brief: The Dalvik Instruction Parser
 */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <inttypes.h>

#include <log.h>
#include <dalvik/dalvik_instruction.h>
#include <sexp.h>
#include <dalvik/dalvik_tokens.h>
#include <debug.h>


#ifdef PARSER_COUNT
int dalvik_instruction_count = 0;
#endif
/**@brief The instruction pool, all instruction is allcoated in the pool,
 *        So that we do not need to free the memory for the instruction,
 *        because all memory will be freed when the fianlization fucntion 
 *        is called
 *        NOTICE that the memory address of an instruction might be change
 *        after they are allocated, so use the instruction ID rather than 
 *        the pointer to instruction for refering the instruction.
 */
dalvik_instruction_t* dalvik_instruction_pool = NULL;

/** @brief The capacity of the pool, although the sizeof the pool can increase dynamically,
 * 		   But it's a relateively slow operation to reallocate the memory for the pool,
 * 		   so that, we must set up a proper initial value of the size. */
static size_t _dalvik_instruction_pool_capacity = DALVIK_POOL_INIT_SIZE;

static size_t _dalvik_instruction_pool_size = 0;
/** @brief double the size of  the instruction pool when there's no space */
static int _dalvik_instruction_pool_resize()
{
    LOG_DEBUG("resize dalvik instruction pool from %zu to %zu", _dalvik_instruction_pool_capacity, 
                                                              _dalvik_instruction_pool_capacity *2);
    dalvik_instruction_t* old_pool;
    assert(dalvik_instruction_pool != NULL);
    
    old_pool = dalvik_instruction_pool;
    
    dalvik_instruction_pool = realloc(old_pool, sizeof(dalvik_instruction_t) * _dalvik_instruction_pool_capacity * 2);

    if(NULL == dalvik_instruction_pool) 
    {
		LOG_ERROR("can not double the size of instruction pool");
        dalvik_instruction_pool = old_pool;
        return -1;
    }
    _dalvik_instruction_pool_capacity *= 2;
    return 0;
}
int dalvik_instruction_init( void )
{
    _dalvik_instruction_pool_size = 0;
    if(NULL == dalvik_instruction_pool) 
	{
		LOG_DEBUG("there's no space for instruction pool, create a new pool");
        dalvik_instruction_pool = 
            (dalvik_instruction_t*)malloc(sizeof(dalvik_instruction_t) * _dalvik_instruction_pool_capacity);
	}
    if(NULL == dalvik_instruction_pool)
	{
		LOG_ERROR("can not allocate instruction pool");
        return -1;
	}
    LOG_DEBUG("dalvik instruction pool initialized");
    return 0;
}

int dalvik_instruction_finalize( void )
{
    int i;
	if(NULL != dalvik_instruction_pool)
	{
		for(i = 0; i < _dalvik_instruction_pool_size; i ++)
			dalvik_instruction_free(dalvik_instruction_pool + i);

    	free(dalvik_instruction_pool);
	}
    return 0;
}

dalvik_instruction_t* dalvik_instruction_new( void )
{
    if(_dalvik_instruction_pool_size >= _dalvik_instruction_pool_capacity)
    {
        if(_dalvik_instruction_pool_resize() < 0) 
        {
            LOG_ERROR("can't resize the instruction pool, allocation failed");
            return NULL;
        }
    }
    dalvik_instruction_t* val = dalvik_instruction_pool + (_dalvik_instruction_pool_size ++);
    memset(val, 0, sizeof(dalvik_instruction_t));
	/* TODO: because we assume the next instruction is the following instruction in the pool, so 
	 * is this field a meaningful one?
	 */
    val->next = DALVIK_INSTRUCTION_INVALID;   
    return val;
}
/** @brief setup an operand */
static inline void _dalvik_instruction_operand_setup(dalvik_operand_t* operand, uint8_t flags, uint64_t payload)
{
    operand->header.flags = flags;
    operand->payload.uint64 = payload;
}
/** @brief write an annotation to the instruction. The annotation is 
 * 	some additional information that is stored in the unused space
 * 	in a instruction
 */
static inline int _dalvik_instruction_write_annotation(dalvik_instruction_t* inst, const void* value, size_t size)
{
    char* mem_start = (char*)(inst->annotation_begin + inst->num_operands);
    if(mem_start + size < inst->annotation_end)
    {
        memcpy(mem_start, value, size);
        return 0;
    }
    else
        LOG_WARNING("no space for annotation for instruction(opcode = 0x%x, flags = 0x%x)", inst->opcode, inst->flags);
    return -1;
}
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"  /* turn off the annoying warning options */
/** 
 * @brief this macro is used to define a function that is used to initialize a instruction. 
 * 		  `next' is the S-Expression that has not been parsed yet, `buf' is the output buffer
 **/
#define __DI_CONSTRUCTOR(kw) static inline int _dalvik_instruction_##kw(const sexpression_t* next, dalvik_instruction_t* buf)
/**
 * @brief setup a operand, should be used in a instruction constructor, the operand will be write to
 * 		  variable buf.
 * @param id the index of the operand
 * @param flag the flag of this operand, see definition of dalvik_operand_t for details 
 **/
#define __DI_SETUP_OPERAND(id, flag, value) do{_dalvik_instruction_operand_setup(buf->operands + (id), (flag), (uint64_t)(value));}while(0)
/**
 * @brief write an annotation to the buffer
 * @param what what to write to the instruction
 * @param sz the size of the instruction
 **/
#define __DI_WRITE_ANNOTATION(what, sz) do{_dalvik_instruction_write_annotation(buf, &(what), (sz));}while(0)
/**
 * @brief get the register name
 * @param buf the input buffer
 **/
#define __DI_REGNUM(buf) (atoi((buf)+1))
#define __DI_INSNUM(buf) (atoi(buf))
#define __DI_INSNUMLL(buf) (atoll(buf))
__DI_CONSTRUCTOR(NOP)
{
    buf->opcode = DVM_NOP;
    buf->num_operands = 0;
    buf->flags = 0;
    return 0;
}
__DI_CONSTRUCTOR(MOVE)
{
    const char *sour, *dest;
    buf->opcode = DVM_MOVE;
    buf->num_operands = 2;
    const char* curlit;
    int rc;
	/* try to peek the first literal */
    rc = sexp_match(next, "(L?A", &curlit, &next);
    if(rc == 0) return -1;
    else if(curlit == DALVIK_TOKEN_FROM16 || curlit == DALVIK_TOKEN_16)  /* move/from16 or move/16 */
    {
        rc = sexp_match(next, "(L?L?", &dest, &sour);
        if(rc == 0) return -1;
        __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(dest));
        __DI_SETUP_OPERAND(1, 0, __DI_REGNUM(sour));
    }
    else if(curlit == DALVIK_TOKEN_WIDE)  /* move-wide */
    {
        next = sexp_strip(next, DALVIK_TOKEN_FROM16, DALVIK_TOKEN_16, NULL); /* strip 'from16' and '16', because we don't distinguish the range of register */
        if(sexp_match(next, "(L?L?", &dest, &sour)) 
        {
            __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_WIDE, __DI_REGNUM(dest));
            __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_WIDE, __DI_REGNUM(sour));
        }
        else 
		{
			LOG_ERROR("invalid operand");
			return -1; 
		}
    }
    else if(curlit == DALVIK_TOKEN_OBJECT)  /*move-object*/
    {
        next = sexp_strip(next, DALVIK_TOKEN_FROM16, DALVIK_TOKEN_16, NULL);  /* for the same reason, strip it frist */
        if(sexp_match(next, "(L?L?", &dest, &sour)) 
        {
            __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_OBJECT), __DI_REGNUM(dest));
            __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_OBJECT), __DI_REGNUM(sour));
        }
    }
    else if(curlit == DALVIK_TOKEN_RESULT)  /* move-result */
    {
        if(sexp_match(next, "(L=L?", DALVIK_TOKEN_WIDE, &dest))  /* move-result/wide */
        {
            __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_WIDE, __DI_REGNUM(dest));
            __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_WIDE | DVM_OPERAND_FLAG_RESULT, 0);
        }
        else if(sexp_match(next, "(L=L?", DALVIK_TOKEN_OBJECT, &dest)) /* move-result/object */
        {
            __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_OBJECT), __DI_REGNUM(dest));
            __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_OBJECT) | DVM_OPERAND_FLAG_RESULT, 0);
        }
        else if(sexp_match(next, "(L?", &dest))  /* move-result */
        {
            __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(dest));
            __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_RESULT, 0);
        }
        else 
		{
			LOG_ERROR("invalid operand");
			return -1;
		}
    }
    else if(curlit == DALVIK_TOKEN_EXCEPTION)  /* move-exception */
    {
        if(sexp_match(next, "(L?", &dest))
        {
            __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(dest));
            __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_EXCEPTION), 0);
        }
        else 
		{
			LOG_ERROR("invalid operand");
			return -1;
		}
    }
    else
    {
        if(sexp_match(next, "(L?", &sour))
        {
            dest = curlit;
            __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(dest));
            __DI_SETUP_OPERAND(1, 0, __DI_REGNUM(sour));
        }
    }
    return 0;
}
__DI_CONSTRUCTOR(RETURN)
{
    buf->opcode = DVM_RETURN;
    buf->num_operands = 1;
    const char* curlit, *dest;
    int rc;
    rc = sexp_match(next, "(L?A", &curlit, &next);
    if(0 == rc) return -1;
    if(curlit == DALVIK_TOKEN_VOID) /* return-void */
    {
        __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_VOID), 0);
    }
    else if(curlit == DALVIK_TOKEN_WIDE) /* return wide */
    {
        if(sexp_match(next, "(L?", &dest))
            __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_WIDE, __DI_REGNUM(dest));
        else 
		{
			LOG_ERROR("can not get the source register");
			return -1;
		}
    }
    else if(curlit == DALVIK_TOKEN_OBJECT) /* return-object */
    {
        if(sexp_match(next, "(L?", &dest))
            __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_OBJECT), __DI_REGNUM(dest));
        else 
		{
			LOG_ERROR("can not get the source register");
			return -1;
		}
    }
    else  /* return */
    {
        __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(curlit));
    }
    return 0;
}
__DI_CONSTRUCTOR(CONST)
{
    buf->opcode = DVM_CONST;
    buf->num_operands = 2;
    const char* curlit, *dest, *sour;
    int rc;
    next = sexp_strip(next, DALVIK_TOKEN_4, DALVIK_TOKEN_16, NULL);     /* We don't care the size of instance number */
    rc = sexp_match(next, "(L?A", &curlit, &next);
    if(0 == rc) 
	{
		LOG_ERROR("can not get next literal");
		return -1;
	}
    if(curlit == DALVIK_TOKEN_HIGH16) /* const/high16 */
    {
        if(sexp_match(next, "(L?L?", &dest, &sour))
        {
            __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(dest));
            __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_CONST | DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_INT), __DI_INSNUM(sour) << 16);
        }
        else 
		{
			LOG_ERROR("invalid operands");
			return -1;
		}
    }
    else if(curlit == DALVIK_TOKEN_WIDE) /* const-wide */
    {
		/* again, we don't care the either */
        next = sexp_strip(next, DALVIK_TOKEN_16, DALVIK_TOKEN_32, NULL);
        if(sexp_match(next, "(L=L?L?", DALVIK_TOKEN_HIGH16, &dest, &sour))  /* const-wide/high16 */
        {
           __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_WIDE, __DI_REGNUM(dest));
           __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_INT) | 
                                 DVM_OPERAND_FLAG_WIDE | 
                                 DVM_OPERAND_FLAG_CONST, ((uint64_t)__DI_INSNUM(sour)) << 48);
        }
        else if(sexp_match(next, "(L?L?", &dest, &sour))
        {
            /* const-wide */
            __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_WIDE, __DI_REGNUM(dest));
            __DI_SETUP_OPERAND(1,DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_INT) |
                                 DVM_OPERAND_FLAG_CONST | 
                                 DVM_OPERAND_FLAG_WIDE, __DI_INSNUMLL(sour));
        }
        else 
		{
			LOG_ERROR("invalid instruction format");
			return -1;
		}
    }
    else if(curlit == DALVIK_TOKEN_STRING) /* const-string */
    {
       next = sexp_strip(next, DALVIK_TOKEN_JUMBO, NULL);   /* Jumbo is useless for us */
       if(sexp_match(next, "(L?S?", &dest, &sour))
       {
           __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_STRING), __DI_REGNUM(dest));
           __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_STRING) | DVM_OPERAND_FLAG_CONST, sour);
       }
       else
	   {
		   LOG_ERROR("invalid instruction format");
		   return -1;
	   }
    }
    else if(curlit == DALVIK_TOKEN_CLASS)  /* const-class */
    {
        if(sexp_match(next, "(L?A", &dest, &next))
        {
           __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_OBJECT), __DI_REGNUM(dest));
           __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_CLASS) | DVM_OPERAND_FLAG_CONST, sexp_get_object_path(next, NULL));
        }
        else 
		{
			LOG_ERROR("invalid instruction format");
			return -1;
		}
    }
    else /* const or const/4 or const/16 */
    {
        dest = curlit;
        if(sexp_match(next, "(L?", &sour))
        {
            __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(dest));
            __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_INT) | DVM_OPERAND_FLAG_CONST, __DI_INSNUM(sour));
        }
        else 
		{
			LOG_ERROR("invalid instruction format");
			return -1;
		}
    }
    return 0;
}
__DI_CONSTRUCTOR(MONITOR)
{
    buf->opcode = DVM_MONITOR;
    buf->num_operands = 1;
    const char* curlit, *arg;
    int rc;
    rc = sexp_match(next, "(L?A", &curlit, &next);
    if(0 == rc) return -1;
    if(curlit == DALVIK_TOKEN_ENTER)  /* monitor-enter */
    {
        buf->flags = DVM_FLAG_MONITOR_ENT;
        if(sexp_match(next, "(L?", &arg))
        {
            __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(arg));
        }
        else return -1;
    }
    else if(curlit == DALVIK_TOKEN_EXIT) /* monitor-exit */
    {
        buf->flags = DVM_FLAG_MONITOR_EXT;
        if(sexp_match(next, "(L?", &arg))
        {
            __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(arg));
        }
        else return -1;
    }
    return 0;
}
__DI_CONSTRUCTOR(CHECK)
{
    buf->opcode = DVM_CHECK_CAST;
    buf->num_operands = 2;
    const char* curlit, *sour;
    int rc;
    rc = sexp_match(next, "(L?A", &curlit, &next);
    if(0 == rc) return -1;
    if(curlit == DALVIK_TOKEN_CAST)  /* check-cast */
    {
        if(sexp_match(next, "(L?A", &sour, &next))
        {
            __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_OBJECT), __DI_REGNUM(sour));
            __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_CLASS) |
                                  DVM_OPERAND_FLAG_CONST , 
                                  sexp_get_object_path(next, NULL));
        }
        else 
		{
			LOG_ERROR("invalid operands");
			return -1;
		}
    }
    else 
	{
		LOG_ERROR("invalid instruction format");
		return -1;
	}
    return 0;
}
__DI_CONSTRUCTOR(THROW)
{
    buf->opcode = DVM_THROW;
    buf->num_operands = 1;
    const char* sour;
    if(sexp_match(next, "(L?", &sour))  /* throw */
    {
        __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_OBJECT), __DI_REGNUM(sour));
    }
    else 
	{
		LOG_ERROR("invalid operand");
		return -1;
	}
    return 0;
}
__DI_CONSTRUCTOR(GOTO)
{
    buf->opcode = DVM_GOTO;
    buf->num_operands = 1;
    const char* label;
    next = sexp_strip(next, DALVIK_TOKEN_16, DALVIK_TOKEN_32, NULL);   /* We don't care the size of offest */
    if(sexp_match(next, "(L?", &label))   /* goto label */
    {
        int lid = dalvik_label_get_label_id(label);
        __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_LABEL) | DVM_OPERAND_FLAG_CONST, lid);
    }
    else 
	{
		LOG_ERROR("invalid label");
		return -1;
	}
    return 0;
}
__DI_CONSTRUCTOR(PACKED)
{
    buf->opcode = DVM_SWITCH;
    buf->num_operands = 3;
    buf->flags = DVM_FLAG_SWITCH_PACKED;
    const char *reg, *begin;
    if(sexp_match(next, "(L=L?L?A", DALVIK_TOKEN_SWITCH, &reg, &begin, &next))/*(packed-switch reg begin label1..label N)*/
    {
        const char* label;
        vector_t*   jump_table;
        __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(reg));
        __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_CONST |
                              DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_INT),
                              __DI_INSNUM(begin));
        __DI_SETUP_OPERAND(2, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_LABELVECTOR) |
                              DVM_OPERAND_FLAG_CONST,
                              jump_table = vector_new(sizeof(uint32_t)));
        if(NULL == jump_table) 
		{
			LOG_ERROR("can not allocate a vector for jump table");
			return -1;
		}
        while(SEXP_NIL != next)
        {
            if(!sexp_match(next, "(L?A", &label, &next)) 
            {
				LOG_ERROR("invalid instruction format");
                vector_free(jump_table);
                return -1;
            }
            
            int lid = dalvik_label_get_label_id(label);

            if(lid < 0) 
            {
				LOG_ERROR("label %s does not exist", label);
                vector_free(jump_table);
                return -1;
            }

            vector_pushback(jump_table, &lid);
        }
    }
    else 
	{
		LOG_ERROR("invalid instruction format");
		return -1;
	}
    return 0;
}
__DI_CONSTRUCTOR(SPARSE)
{
    buf->opcode = DVM_SWITCH;
    buf->flags  = DVM_FLAG_SWITCH_SPARSE;
    buf->num_operands = 2;
    const char* reg;
    if(sexp_match(next, "(L=L?A", DALVIK_TOKEN_SWITCH, &reg, &next))
    {
        const char *label;
        const char *cond;
        vector_t* jump_table;
        __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(reg));
        __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_CONST |
                              DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_SPARSE),
                              jump_table = vector_new(sizeof(dalvik_sparse_switch_branch_t)));
        if(NULL == jump_table) 
		{
			LOG_ERROR("can not allocate vector to store the jump table");
			return -1;
		}
        while(SEXP_NIL != next)
        {
            sexpression_t* this;
            if(!sexp_match(next, "(C?A", &this, &next))
            {
				LOG_ERROR("invalid operands");
                vector_free(jump_table);
                return -1;
            }

            if(sexp_match(this, "(L?L?", &cond, &label))
            {
                int lid = dalvik_label_get_label_id(label);
                int cit = __DI_INSNUM(cond);
                if(lid < 0)
                {
					LOG_ERROR("label %s does not exist", label);
                    vector_free(jump_table);
                    return -1;
                }
                dalvik_sparse_switch_branch_t branch;
                if(cond == DALVIK_TOKEN_DEFAULT)
                    branch.is_default = 1;
                else
                    branch.is_default = 0;
                branch.cond = cit;
                branch.labelid = lid;
                vector_pushback(jump_table, &branch);
            }
            else
            {
				LOG_ERROR("invalid operand format");
                vector_free(jump_table);
                return -1;
            }
        }
    }
    else 
	{
		LOG_ERROR("invalid instruction format");
		return -1;
	}
    return 0;
}
__DI_CONSTRUCTOR(CMP)
{
    buf->opcode = DVM_CMP;
    buf->num_operands = 3;
    const char *type, *dest, *sourA, *sourB; 
    if(sexp_match(next, "(L?L?L?L?", &type, &dest, &sourA, &sourB))   /* cmp-type dest, sourA, sourB */
    {
        __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(dest));
        uint32_t flag;
        if(DALVIK_TOKEN_FLOAT == type)
            flag = DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_FLOAT);
        else if(DALVIK_TOKEN_DOUBLE == type)
            flag = DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_DOUBLE) | DVM_OPERAND_FLAG_WIDE;
        else if(DALVIK_TOKEN_LONG == type)
            flag = DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_LONG);
        else 
		{
			LOG_ERROR("invaild type");
			return -1;
		}
        __DI_SETUP_OPERAND(1, flag, __DI_REGNUM(sourA));
        __DI_SETUP_OPERAND(2, flag, __DI_REGNUM(sourB));
    }
    else 
	{
		LOG_ERROR("invalid instruction format");
		return -1;
	}
    return 0;
}
__DI_CONSTRUCTOR(IF)
{
    buf->opcode = DVM_IF;
    buf->num_operands = 3;
    const char* how, *sourA, *sourB, *label;
    int rc, lid;
    rc = sexp_match(next, "(L?L?A", &how, &sourA ,&next);
    if(!rc) 
	{
		LOG_ERROR("can not peek the first literal");
		return -1;
	}
    /* setup the first operand */
    __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(sourA));
    if(DALVIK_TOKEN_EQZ == how ||
       DALVIK_TOKEN_LTZ == how ||
       DALVIK_TOKEN_GTZ == how ||
       DALVIK_TOKEN_GEZ == how ||
       DALVIK_TOKEN_LEZ == how ||
       DALVIK_TOKEN_NEZ == how)
        __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_CONST | DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_INT), 0);   /* use 0 as the second operand */
    else
    {
        /* read the second operand */
        rc = sexp_match(next, "(L?A", &sourB, &next);
        if(!rc) 
		{
			LOG_ERROR("invalid operands");
			return -1;
		}
        __DI_SETUP_OPERAND(1, 0, __DI_REGNUM(sourB));
    }
    rc = sexp_match(next, "(L?", &label);
    if(!rc) 
	{
		LOG_ERROR("invalid label");
		return -1;
	}
    lid = dalvik_label_get_label_id(label);
    if(lid < 0) 
	{
		LOG_ERROR("label %s does not exist", label);
		return -1;
	}
    /* setup the third operand */
    __DI_SETUP_OPERAND(2, DVM_OPERAND_FLAG_CONST |
                          DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_LABEL),
                       lid);
    /* setup the instruction flags */
    if     (DALVIK_TOKEN_NEZ == how || DALVIK_TOKEN_NE == how) buf->flags = DVM_FLAG_IF_NE;
    else if(DALVIK_TOKEN_LEZ == how || DALVIK_TOKEN_LE == how) buf->flags = DVM_FLAG_IF_LE;
    else if(DALVIK_TOKEN_GEZ == how || DALVIK_TOKEN_GE == how) buf->flags = DVM_FLAG_IF_GE;
    else if(DALVIK_TOKEN_GTZ == how || DALVIK_TOKEN_GT == how) buf->flags = DVM_FLAG_IF_GT;
    else if(DALVIK_TOKEN_LTZ == how || DALVIK_TOKEN_LT == how) buf->flags = DVM_FLAG_IF_LT;
    else if(DALVIK_TOKEN_EQZ == how || DALVIK_TOKEN_EQ == how) buf->flags = DVM_FLAG_IF_EQ;
    else 
	{
		LOG_ERROR("invalid instruction format");
		return -1;
	}
    return 0;
}
/** @brief: This function is used for build a group of similar instructions :
 *          
 *          iget iput aget aput sget sput
 */
static inline int _dalvik_instruction_setup_object_operations(
        int opcode,                 /* opcode we what to set */
        int flags,                  /* opcode flags */
        const sexpression_t* next,        /* the sexpression */
        dalvik_instruction_t* buf   /* the output */
        )
{
    int ins_kind = 0;        /* Indicates what kind of instruction we have 
                              * 0: 3 operands <dest, obj, idx>
                              * 1: 3 operands <dest, path, type>
                              * 2: 4 operands <dest, obj, path, type>
                              */
    buf->opcode = opcode;
    if(opcode == DVM_ARRAY)
    {
        buf->num_operands = 3;
        ins_kind = 0;
    }
    else
    {
        if(flags == DVM_FLAG_INSTANCE_SGET ||
           flags == DVM_FLAG_INSTANCE_SPUT)
        {
            ins_kind = 1;            /* For static method we need <dest, path, field, type> */
            buf->num_operands = 4;   /* Although one more operand for type, but we don't need object reg */
        }
        else
        {
            ins_kind = 2;
            buf->num_operands = 5;   /* one more operand for type specifier, <dest, obj, path, field, type> */
        }

    }
    buf->flags = flags;
    const char* curlit;
    const char *dest, *obj, *idx, *path, *field;
    dalvik_type_t* type;
    int opflags = 0;    /* Indicates the property of oprands */
    const sexpression_t *previous;
    previous = next;   /* store the previous S-Expression, because we might be want to go back */
    if(sexp_match(next, "(L?A", &curlit, &next) == 0)
	{
		LOG_ERROR("invalid instruction format");
        return -1;
	}
    /* Determine the type flags */
    if(curlit == DALVIK_TOKEN_WIDE   ||
       curlit == DALVIK_TOKEN_OBJECT ||
       curlit == DALVIK_TOKEN_BOOLEAN||
       curlit == DALVIK_TOKEN_BYTE   ||
       curlit == DALVIK_TOKEN_CHAR   ||
       curlit == DALVIK_TOKEN_SHORT) /* xyyy-type */
    {
        if(curlit == DALVIK_TOKEN_WIDE) 
            opflags = DVM_OPERAND_FLAG_WIDE;
        else if(curlit == DALVIK_TOKEN_OBJECT)  
            opflags = DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_OBJECT);
        else if(curlit == DALVIK_TOKEN_BOOLEAN) 
            opflags = DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_BOOLEAN);
        else if(curlit == DALVIK_TOKEN_BYTE) 
            opflags = DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_BYTE);
        else if(curlit == DALVIK_TOKEN_CHAR) 
            opflags = DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_CHAR);
        else if(curlit == DALVIK_TOKEN_SHORT) 
            opflags = DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_SHORT);
    }
    else
    {
        opflags = 0;
        next = previous;   /* we go back */
    }
    
    /* Get the destination register */
    if(sexp_match(next, "(L?A", &dest, &next) == 0)
	{
		LOG_ERROR("invalid destination register");
        return -1;
	}
    __DI_SETUP_OPERAND(0, opflags, __DI_REGNUM(dest));
    /* Move on to the next operand */
    if(ins_kind != 1)   /* We need a object register */
    {
        if(sexp_match(next, "(L?A", &obj, &next) == 0)
		{
			LOG_ERROR("invalid object register");
            return -1;
		}
        __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_OBJECT), __DI_REGNUM(obj));
    }
    /* Move on to the next one/two operand(s) */
    if(ins_kind != 0)   /* We need a path and a type */
    {
        if(NULL == (path = sexp_get_object_path(next, &next)))
		{
			LOG_ERROR("invalid path");
            return -1;
		}
        if(!sexp_match(next, "(L?A", &field, &next))
		{
			LOG_ERROR("invalid field name");
            return -1;
		}
        if(SEXP_NIL == next) 
		{
			LOG_ERROR("invalid instruction format");
            return -1;
		}
        if(!sexp_match(next, "(_?", &next))
		{
			LOG_ERROR("invalid instruction format");
            return -1;
		}
		/* so the last operand is the type */
        if(NULL == (type = dalvik_type_from_sexp(next)))
		{
			LOG_ERROR("invalid type");
            return -1;
		}
        __DI_SETUP_OPERAND(ins_kind==1?1:2, 
                           DVM_OPERAND_FLAG_CONST |
                           DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_CLASS),
                           path);
        __DI_SETUP_OPERAND(ins_kind==1?2:3,
                           DVM_OPERAND_FLAG_CONST |
                           DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_FIELD),
                           field);
        __DI_SETUP_OPERAND(ins_kind==1?3:4, 
                           DVM_OPERAND_FLAG_CONST |
                           DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_TYPEDESC),
                           type);
    }
    else                /* We need a index */
    {
        if(sexp_match(next, "(L?", &idx))
        {
            __DI_SETUP_OPERAND(2, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_INT), __DI_REGNUM(idx));
        }
        else 
		{
			LOG_ERROR("invalid index");
			return -1;
		}
    }
    return 0;
}
__DI_CONSTRUCTOR(AGET)
{
    return _dalvik_instruction_setup_object_operations(DVM_ARRAY, DVM_FLAG_ARRAY_GET, next, buf);
}
__DI_CONSTRUCTOR(APUT)
{
    return _dalvik_instruction_setup_object_operations(DVM_ARRAY, DVM_FLAG_ARRAY_PUT, next, buf);
}
__DI_CONSTRUCTOR(IGET)
{
    return _dalvik_instruction_setup_object_operations(DVM_INSTANCE, DVM_FLAG_INSTANCE_GET, next, buf);
}
__DI_CONSTRUCTOR(IPUT)
{
    return _dalvik_instruction_setup_object_operations(DVM_INSTANCE, DVM_FLAG_INSTANCE_PUT, next, buf);
}
__DI_CONSTRUCTOR(SGET)
{
    return _dalvik_instruction_setup_object_operations(DVM_INSTANCE, DVM_FLAG_INSTANCE_SGET, next, buf);
}
__DI_CONSTRUCTOR(SPUT)
{
    return _dalvik_instruction_setup_object_operations(DVM_INSTANCE, DVM_FLAG_INSTANCE_SPUT, next, buf);
}
__DI_CONSTRUCTOR(INVOKE)
{
    buf->opcode = DVM_INVOKE;
    const char* curlit;
    if(!sexp_match(next, "(L?A", &curlit, &next)) 
	{
		LOG_ERROR("invalid instruction format");
		return -1;
	}
    if(curlit == DALVIK_TOKEN_VIRTUAL)
        buf->flags = DVM_FLAG_INVOKE_VIRTUAL;
    else if(curlit == DALVIK_TOKEN_SUPER)
        buf->flags = DVM_FLAG_INVOKE_SUPER;
    else if(curlit == DALVIK_TOKEN_DIRECT)
        buf->flags = DVM_FLAG_INVOKE_DIRECT;
    else if(curlit == DALVIK_TOKEN_STATIC)
        buf->flags = DVM_FLAG_INVOKE_STATIC;
    else if(curlit == DALVIK_TOKEN_INTERFACE)
        buf->flags = DVM_FLAG_INVOKE_INTERFACE;
    else 
	{
		LOG_ERROR("invalid call type %s", curlit);
		return -1;
	}
    sexpression_t* args;
    const char* path , *field, *reg1, *reg2;
    if(sexp_match(next, "(L=A", DALVIK_TOKEN_RANGE, &next)) /* invoke-xxx/range */
    {
        int reg_from, reg_to;

        buf->flags |= DVM_FLAG_INVOKE_RANGE;
        buf->num_operands = 5;
        if(sexp_match(next, "(C?A", &args, &next))
        {
            if(sexp_get_method_address(next, &next, &path, &field) < 0)
            {
                LOG_ERROR("can not parse the method path");
                return -1;
            }
            
            /* TODO: We actually care about the type, because the function may be overloaded */
            __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_CONST |
                                  DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_CLASS),
                                  path);
            __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_CONST |
                                  DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_FIELD),
                                  field);
            if(sexp_match(args, "(L?L?", &reg1, &reg2))
            {
                reg_from = __DI_REGNUM(reg1);
                reg_to   = __DI_REGNUM(reg2);
            }
            else if(sexp_match(args, "(L?", &reg1))
            {
                reg_from = reg_to = __DI_REGNUM(reg1);
            }
            else 
            {
                LOG_ERROR("invalid instruction format");
                return -1;
            }
            /* We use a constant indicates the range */
            __DI_SETUP_OPERAND(3, DVM_OPERAND_FLAG_CONST | DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_INT), reg_from);
            __DI_SETUP_OPERAND(4, DVM_OPERAND_FLAG_CONST | DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_INT), reg_to);
            /* here we parse the type */
            size_t nparam = sexp_length(next) + 1; /* because we need a NULL pointer in the end */
            dalvik_type_t** array = (dalvik_type_t**) malloc(sizeof(dalvik_type_t*) * nparam);

            if(NULL == array) 
            {
                LOG_ERROR("can not allocate memory for type array");
                return -1;
            }

            memset(array, 0, sizeof(dalvik_type_t*) * nparam);
            int i;
            sexpression_t *type_sexp;
            for(i = 0; sexp_match(next, "(_?A", &type_sexp, &next) && i < nparam - 1; i ++)
            {
                array[i] = dalvik_type_from_sexp(type_sexp);
                if(NULL == array[i])
                {
                    LOG_ERROR("can not parse type %s", sexp_to_string(type_sexp, NULL));
                    int j;
                    for(j = 0; j < i; j ++)
                        dalvik_type_free(array[j]);
                    free(array);
                    return -1;
                }
            }

            __DI_SETUP_OPERAND(2, DVM_OPERAND_FLAG_CONST | DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_TYPELIST), array);
        }
        else return -1;
    }
    else   /* invoke-xxx */
    {
        if(sexp_match(next, "(C?A", &args, &next))
        {
            if(sexp_get_method_address(next, &next, &path, &field) < 0)
            {
                LOG_ERROR("can not parse the method path");
                return -1;
            }

            /* TODO: We actually care about the type, because the function may be overloaded */
            __DI_SETUP_OPERAND(0, DVM_OPERAND_FLAG_CONST |
                                  DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_CLASS),
                                  path);
            __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_CONST |
                                  DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_FIELD),
                                  field);

            /* now we parse the parameters */
            buf->num_operands = 3;
            for(;args != SEXP_NIL;)
            {
                if(sexp_match(args, "(L?A", &reg1, &args))
                {
                    __DI_SETUP_OPERAND(buf->num_operands ++, 0, __DI_REGNUM(reg1));
                    if(buf->num_operands == 0) 
                    {
                        LOG_WARNING("num_operands overflow");
                    }
                }
                else 
                {
                    LOG_ERROR("invalid operand format");
                    return -1;
                }
            }
            
            /* here we parse the type */
            size_t nparam = sexp_length(next) + 1; /* because we need a NULL pointer in the end */
            dalvik_type_t** array = (dalvik_type_t**) malloc(sizeof(dalvik_type_t*) * nparam);

            if(NULL == array) 
            {
                LOG_ERROR("can not allocate memory for type array");
                return -1;
            }
            memset(array, 0, sizeof(dalvik_type_t*) * nparam);
            int i;
            sexpression_t *type_sexp;
            for(i = 0; sexp_match(next, "(_?A", &type_sexp, &next) && i < nparam - 1; i ++)
            {
                array[i] = dalvik_type_from_sexp(type_sexp);
                if(NULL == array[i])
                {
                    LOG_ERROR("can not parse type %s", sexp_to_string(type_sexp, NULL));
                    int j;
                    for(j = 0; j < i; j ++)
                        dalvik_type_free(array[j]);
                    free(array);
                    return -1;
                }
            }
            __DI_SETUP_OPERAND(2, DVM_OPERAND_FLAG_CONST | DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_TYPELIST), array);
        }
    }
    return 0;
}
/** @brief get the type flags from S-Expression */
static inline int32_t _dalvik_instruction_sexpression_fetch_type(const sexpression_t** psexp, int addtional_flags)
{
    int ret = addtional_flags;
    const sexpression_t* sexp = *psexp;
    const char* type;
    if(sexp_match(sexp, "(L?A", &type, psexp))
    {
        if(DALVIK_TOKEN_INT == type) 
            ret |= DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_INT);
        else if(DALVIK_TOKEN_LONG == type)
            ret |= DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_LONG);
        else if(DALVIK_TOKEN_FLOAT == type)
            ret |= DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_FLOAT);
        else if(DALVIK_TOKEN_DOUBLE == type)
            ret |= DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_DOUBLE);
        else if(DALVIK_TOKEN_BYTE == type)
            ret |= DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_BYTE);
        else if(DALVIK_TOKEN_CHAR == type)
            ret |= DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_CHAR);
        else if(DALVIK_TOKEN_SHORT == type)
            ret |= DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_SHORT);
        else return -1;
    }
    else return -1;
    return ret;
}
/** @brief set upo a arithmetic instruction */
static inline int _dalvik_instruction_setup_arithmetic(
        int opcode, 
        int flags,    /* flag of the instruction */
        int num_operands,
        int operand_flags[],   /* flag of the first operand */
        const sexpression_t* next,
        dalvik_instruction_t* buf)
{
    buf->opcode = opcode;
    buf->flags = flags;
    buf->num_operands = num_operands;
    int i;
    const char* reg;
    for(i = 0; i < num_operands; i ++)
    {
        if(operand_flags[i] == -1) 
        {
            LOG_ERROR("invalid operand flags");
            return -1;
        }
        if(sexp_match(next, "(L?A", &reg, &next))
        {
            if(operand_flags[i] & DVM_OPERAND_FLAG_CONST)
                __DI_SETUP_OPERAND(i, operand_flags[i], __DI_INSNUM(reg));
            else
                __DI_SETUP_OPERAND(i, operand_flags[i], __DI_REGNUM(reg));
        }
        else 
        {
            LOG_ERROR("invalid instruction format");
            return -1;
        }
    }
    return 0;
}
__DI_CONSTRUCTOR(NEG)
{
    int operand_flags[2];
    operand_flags[0] = operand_flags[1] = _dalvik_instruction_sexpression_fetch_type(&next, 0);
    return _dalvik_instruction_setup_arithmetic(DVM_UNOP, DVM_FLAG_UOP_NEG, 2, operand_flags, next, buf);
}
__DI_CONSTRUCTOR(NOT)
{
    int operand_flags[2];
    operand_flags[0] = operand_flags[1] = _dalvik_instruction_sexpression_fetch_type(&next, 0);
    return _dalvik_instruction_setup_arithmetic(DVM_UNOP, DVM_FLAG_UOP_NOT, 2, operand_flags, next, buf);
}
/** @brief used for converting (convert-xxx ) insturction */
static inline int _dalvik_instruction_convert_operator(const sexpression_t* next, dalvik_instruction_t* buf, int type)
{
    int operand_flags[2];
    if(!sexp_match(next, "(L=A", DALVIK_TOKEN_TO, &next)) 
    {
        LOG_ERROR("invalid instruction format");
        return -1;
    }
    operand_flags[0] = DVM_OPERAND_FLAG_TYPE(type);
    operand_flags[1] = _dalvik_instruction_sexpression_fetch_type(&next, 0);
    return _dalvik_instruction_setup_arithmetic(DVM_UNOP, DVM_FLAG_UOP_TO, 2, operand_flags, next, buf);
}
__DI_CONSTRUCTOR(INT)
{
    return _dalvik_instruction_convert_operator(next, buf, DVM_OPERAND_TYPE_INT);
}
__DI_CONSTRUCTOR(LONG)
{
    return _dalvik_instruction_convert_operator(next, buf, DVM_OPERAND_TYPE_LONG);
}
__DI_CONSTRUCTOR(FLOAT)
{
    return _dalvik_instruction_convert_operator(next, buf, DVM_OPERAND_TYPE_FLOAT);
}
__DI_CONSTRUCTOR(DOUBLE)
{
    return _dalvik_instruction_convert_operator(next, buf, DVM_OPERAND_TYPE_DOUBLE);
}
static inline int _dalvik_instruction_setup_binary_operator(int flags, const sexpression_t* next, dalvik_instruction_t *buf)
{
    buf->opcode = DVM_BINOP;
    buf->flags = flags;
    int opflags;
    opflags = _dalvik_instruction_sexpression_fetch_type(&next, 0);
    if(opflags == -1) 
    {
        LOG_ERROR("invalid instruction type");
        return -1;
    }
    const char* curlit;
    const sexpression_t *previous;
    int const_operand = 0;
    previous = next;
    if(!sexp_match(next, "(L?A", &curlit, &next)) 
    {
        LOG_ERROR("can not peek the next literal");
        return -1;
    }

    buf->num_operands = 3;

	int flag2addr = 0;

    if(curlit == DALVIK_TOKEN_2ADDR)  /* xxxx/2addr */
        flag2addr = 1;
	else if(curlit == DALVIK_TOKEN_LIT16 ||
       curlit == DALVIK_TOKEN_LIT8)   /* xxxxx/lityy */
        const_operand = 1;
    else 
        next = previous;  /* okay, nothing useful, we just go back to the previous one */ 

    /* Setup reg0 and reg1 */
    const char* reg0, *reg1;
    if(!sexp_match(next, "(L?L?A", &reg0, &reg1, &next)) return -1;
    __DI_SETUP_OPERAND(0, opflags, __DI_REGNUM(reg0));
	if(flag2addr) 
		__DI_SETUP_OPERAND(1, opflags, __DI_REGNUM(reg0));
    __DI_SETUP_OPERAND(1 + flag2addr, opflags, __DI_REGNUM(reg1));

    /* Setpu the last reg */
    const char* reg3;
    if(flag2addr) return 0;
    if(!sexp_match(next, "(L?", &reg3)) return -1;
    if(const_operand)
    {
        opflags |= DVM_OPERAND_FLAG_CONST;
        __DI_SETUP_OPERAND(2, opflags, __DI_INSNUM(reg3));
    }
    else
    {
        __DI_SETUP_OPERAND(2, opflags, __DI_REGNUM(reg3));
    }
    return 0;
}
__DI_CONSTRUCTOR(ADD)
{
    return _dalvik_instruction_setup_binary_operator(DVM_FLAG_BINOP_ADD, next, buf);
}
__DI_CONSTRUCTOR(SUB)
{
    return _dalvik_instruction_setup_binary_operator(DVM_FLAG_BINOP_SUB, next, buf);
}
__DI_CONSTRUCTOR(MUL)
{
    return _dalvik_instruction_setup_binary_operator(DVM_FLAG_BINOP_MUL, next, buf);
}
__DI_CONSTRUCTOR(DIV)
{
    return _dalvik_instruction_setup_binary_operator(DVM_FLAG_BINOP_DIV, next, buf);
}
__DI_CONSTRUCTOR(REM)
{
    return _dalvik_instruction_setup_binary_operator(DVM_FLAG_BINOP_REM, next, buf);
}
__DI_CONSTRUCTOR(AND)
{
    return _dalvik_instruction_setup_binary_operator(DVM_FLAG_BINOP_AND, next, buf);
}
__DI_CONSTRUCTOR(OR)
{
    return _dalvik_instruction_setup_binary_operator(DVM_FLAG_BINOP_OR, next, buf);
}
__DI_CONSTRUCTOR(XOR)
{
    return _dalvik_instruction_setup_binary_operator(DVM_FLAG_BINOP_XOR, next, buf);
}
__DI_CONSTRUCTOR(SHL)
{
    return _dalvik_instruction_setup_binary_operator(DVM_FLAG_BINOP_SHL, next, buf);
}
__DI_CONSTRUCTOR(SHR)
{
    return _dalvik_instruction_setup_binary_operator(DVM_FLAG_BINOP_SHR, next, buf);
}
__DI_CONSTRUCTOR(USHR)
{
    return _dalvik_instruction_setup_binary_operator(DVM_FLAG_BINOP_USHR, next, buf);
}
__DI_CONSTRUCTOR(RSUB)
{
    return _dalvik_instruction_setup_binary_operator(DVM_FLAG_BINOP_RSUB, next, buf);
}
__DI_CONSTRUCTOR(INSTANCE)
{
    buf->opcode = DVM_INSTANCE;
    buf->flags  = DVM_FLAG_INSTANCE_OF;
    buf->num_operands = 3;
    const char *dest, *sour, *path;
    if(!sexp_match(next, "(L=L?L?A",DALVIK_TOKEN_OF ,&dest, &sour, &next))  return -1;
    __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(dest));
    __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_OBJECT), __DI_REGNUM(sour));
    
    if(NULL != (path = sexp_get_object_path(next, NULL))) 
    {
        __DI_SETUP_OPERAND(2, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_CLASS) | 
                              DVM_OPERAND_FLAG_CONST ,
                              path);
    }
    else
    {
        if(sexp_match(next, "(_?", &next))
        {
            dalvik_type_t* type = dalvik_type_from_sexp(next);
            if(NULL == type)
            {
                LOG_ERROR("invalid type");
                return -1;
            }
            __DI_SETUP_OPERAND(2, DVM_OPERAND_FLAG_CONST|
                                  DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_TYPEDESC),
                                  type);
        }
        else 
        {
            LOG_ERROR("invalid instruction format");
            return -1;
        }
    }
    return 0;
}
__DI_CONSTRUCTOR(ARRAY)
{
    buf->opcode = DVM_ARRAY;
    buf->flags   = DVM_FLAG_ARRAY_LENGTH;
    buf->num_operands = 2;
    const char* dest, *sour;
    if(!sexp_match(next, "(L=L?L?", DALVIK_TOKEN_LENGTH, &dest, &sour)) 
    {
        LOG_ERROR("invalid instruction format");
        return -1;
    }
    __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(dest));
    __DI_SETUP_OPERAND(1, DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_OBJECT), __DI_REGNUM(sour));
    return 0;
}
__DI_CONSTRUCTOR(NEW)
{
    const char* curlit;
    if(!sexp_match(next, "(L?A", &curlit, &next)) 
    {
        LOG_ERROR("can not peek the next literal");
        return -1;
    }
    if(curlit == DALVIK_TOKEN_ARRAY)
        buf->opcode = DVM_ARRAY;
    else if(curlit == DALVIK_TOKEN_INSTANCE)
        buf->opcode = DVM_INSTANCE;
    else 
    {
        LOG_ERROR("invalid instruction format");
        return -1;
    }
    const char *dest, *size, *path;
    if(!sexp_match(next, "(L?A", &dest, &next)) 
    {
        LOG_ERROR("invalid instruction format");
        return -1;
    }
    __DI_SETUP_OPERAND(0, 0, __DI_REGNUM(dest));
    if(buf->opcode == DVM_INSTANCE)
    {
		buf->flags = DVM_FLAG_INSTANCE_NEW;
        if(NULL == (path = sexp_get_object_path(next, NULL))) 
        {
            LOG_ERROR("invalid class path");
            return -1;
        }
        __DI_SETUP_OPERAND(1, 
                           DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_CLASS) |
                           DVM_OPERAND_FLAG_CONST,
                           path);
        buf->num_operands = 2; 
    }
    else
    {
		buf->flags = DVM_FLAG_ARRAY_NEW;
        if(!sexp_match(next, "(L?A", &size, &next)) 
        {
            LOG_ERROR("invalid instruction format");
            return -1;
        }
        __DI_SETUP_OPERAND(1, 0, __DI_REGNUM(size));
        dalvik_type_t* type;
        sexpression_t* type_sexp;
        if(!sexp_match(next, "(_?", &type_sexp)) 
        {
            LOG_ERROR("invalid instruction format");
            return -1;
        }
        if(NULL == (type = dalvik_type_from_sexp(type_sexp))) 
        {
            LOG_ERROR("invalid type");
            return -1;
        }
        __DI_SETUP_OPERAND(2, 
                           DVM_OPERAND_FLAG_TYPE(DVM_OPERAND_TYPE_TYPEDESC) |
                           DVM_OPERAND_FLAG_CONST,
                           type);
        buf->num_operands = 3; 
    }
    static uint32_t idx = 0;
    __DI_WRITE_ANNOTATION(idx, sizeof(idx));
    idx ++;
    return 0;
}
__DI_CONSTRUCTOR(FILLED)
{
    //TODO
    return 0;
}
#undef __DI_CONSTRUCTOR
int dalvik_instruction_from_sexp(const sexpression_t* sexp, dalvik_instruction_t* buf, int line)
{
#ifdef PARSER_COUNT
    dalvik_instruction_count ++;
#endif

    if(sexp == SEXP_NIL) 
    {
        LOG_ERROR("empty input");
        return -1;
    }
    if(NULL == buf) 
    {
        LOG_ERROR("no place for output");
        return -1;
    }
    const char* firstword;
    sexpression_t* next;
    int rc;
    rc = sexp_match(sexp, "(L?A", &firstword, &next);
#define __DI_BEGIN if(0 == rc){LOG_ERROR("can not peek the first literal");return -1;}
#define __DI_CASE(kw) else if(firstword == DALVIK_TOKEN_##kw){ rc = _dalvik_instruction_##kw(next, buf); }
#define __DI_END else rc = -1;
    __DI_BEGIN
        __DI_CASE(NOP)
        __DI_CASE(MOVE)
        __DI_CASE(RETURN)
        __DI_CASE(CONST)
        __DI_CASE(MONITOR)
        __DI_CASE(CHECK)
        __DI_CASE(THROW)
        __DI_CASE(GOTO)
        __DI_CASE(PACKED)
        __DI_CASE(SPARSE)
        __DI_CASE(CMP)
        /* dirty hack, in this way, we can reuse the same handler */
#       define _dalvik_instruction_CMPL _dalvik_instruction_CMP 
#       define _dalvik_instruction_CMPG _dalvik_instruction_CMP
        __DI_CASE(CMPL)   /* Because we don't care about lt/gt bais */
        __DI_CASE(CMPG)
#       undef _dalvik_instruction_CMPL
#       undef _dalvik_instruction_CMPG
        __DI_CASE(IF)
        __DI_CASE(AGET)
        __DI_CASE(APUT)
        __DI_CASE(IGET)
        __DI_CASE(IPUT)
        __DI_CASE(SGET)
        __DI_CASE(SPUT)
        __DI_CASE(INVOKE)
        __DI_CASE(NEG)
        __DI_CASE(NOT)
        __DI_CASE(INT)
        __DI_CASE(LONG)
        __DI_CASE(FLOAT)
        __DI_CASE(DOUBLE)

        __DI_CASE(ADD)
        __DI_CASE(SUB)
        __DI_CASE(MUL)
        __DI_CASE(DIV)
        __DI_CASE(REM)
        __DI_CASE(AND)
        __DI_CASE(OR)
        __DI_CASE(XOR)
        __DI_CASE(SHL)
        __DI_CASE(SHR)
        __DI_CASE(USHR)

        __DI_CASE(RSUB)

        __DI_CASE(INSTANCE)
        __DI_CASE(ARRAY)
        __DI_CASE(NEW)

        __DI_CASE(FILLED)


    __DI_END
#undef __DI_END
#undef __DI_CASE
#undef __DI_BEGIN
    if(rc == 0)
        buf->line = line;
    else
        LOG_WARNING("failed to parse instruction");
    return rc;
}

void dalvik_instruction_free(dalvik_instruction_t* buf)
{
    if(NULL == buf) return;
    int i;
    for(i = 0; i < buf->num_operands; i ++)
    {
        if(buf->operands[i].header.info.is_const &&
           buf->operands[i].header.info.type == DVM_OPERAND_TYPE_LABELVECTOR)
            vector_free(buf->operands[i].payload.branches);
        else if(buf->operands[i].header.info.is_const &&
                buf->operands[i].header.info.type == DVM_OPERAND_TYPE_SPARSE)
            vector_free(buf->operands[i].payload.sparse);
        else if(buf->operands[i].header.info.is_const &&
                buf->operands[i].header.info.type == DVM_OPERAND_TYPE_TYPEDESC)
            dalvik_type_free(buf->operands[i].payload.type);
        else if(buf->operands[i].header.info.is_const &&
                buf->operands[i].header.info.type == DVM_OPERAND_TYPE_TYPELIST)
        {
            int j;
            for(j = 0; buf->operands[i].payload.typelist[j] != NULL; j ++)
                dalvik_type_free((dalvik_type_t*)buf->operands[i].payload.typelist[j]);
            free((dalvik_type_t*)buf->operands[i].payload.typelist);
        }
    }
}
#define __PR(fmt, args...) do{\
            p += snprintf(p, buf + sz - p, fmt, ##args);\
}while(0)
static inline int _dalvik_instruction_operand_const_to_string(const dalvik_operand_t* op, char* buf, size_t sz)
{
    char *p = buf;
    vector_t* vec;
    int i;
    switch(op->header.info.type)
    {
        case DVM_OPERAND_TYPE_CLASS:
            __PR("[class %s]", op->payload.string);
            break;
        case DVM_OPERAND_TYPE_STRING:
            __PR("\"%s\"", op->payload.string);
            break;
        case DVM_OPERAND_TYPE_INT:
            __PR("%"PRId64"%s", op->payload.int64, op->header.info.size?"I":"L");
            break;
        case DVM_OPERAND_TYPE_ANY:
            __PR("Any:%"PRId64, op->payload.int64);
            break;
        case DVM_OPERAND_TYPE_TYPEDESC:
            __PR("%s", dalvik_type_to_string(op->payload.type, NULL, 0));
            break;
        case DVM_OPERAND_TYPE_FIELD:
            __PR("%s", op->payload.field);
            break;
        case DVM_OPERAND_TYPE_TYPELIST:
            __PR("%s", dalvik_type_list_to_string(op->payload.typelist, NULL, 0));
            break;
        case DVM_OPERAND_TYPE_LABEL:
            __PR("L%u", op->payload.labelid);
            break;
        case DVM_OPERAND_TYPE_LABELVECTOR:
            vec = op->payload.branches;
            __PR("label-list(");
            for(i = 0; i < vector_size(vec); i ++)
                __PR("L%d ", *(int*)vector_get(vec,i));
            __PR(")");
            break;
        case DVM_OPERAND_TYPE_SPARSE:
            vec = op->payload.sparse;
            __PR("sparse-switch(");
            for(i = 0; i < vector_size(vec); i ++)
            {
                dalvik_sparse_switch_branch_t* branch = (dalvik_sparse_switch_branch_t*) vector_get(vec, i);
                __PR("[%d L%d] ",branch->cond, branch->labelid);
            }
            __PR(")");
            break;
        default:
            __PR("invalid-constant %d", op->header.info.type);
    }
    return p - buf;
}
static inline int _dalvik_instruction_operand_to_string(const dalvik_operand_t* opr, char* buf, size_t sz)
{
    char *p = buf;
    if(opr->header.info.is_const)
        return _dalvik_instruction_operand_const_to_string(opr, buf, sz);
    else if(opr->header.info.type == DVM_OPERAND_TYPE_VOID)
        __PR("void");
    else if(opr->header.info.is_result)
        __PR("reg-result");
    else if(opr->header.info.type == DVM_OPERAND_TYPE_EXCEPTION)
        __PR("reg-exception");
    else
        __PR("reg%u", opr->payload.uint32);
    return p - buf;
}
#define __PO(id) do{p += _dalvik_instruction_operand_to_string(inst->operands + id, p, buf + sz - p);} while(0)
#define __CI(_name) name = #_name; break
const char* dalvik_instruction_to_string(const dalvik_instruction_t* inst, char* buf, size_t sz)
{
    static char default_buf[1024];
    if(NULL == buf)
    {
        buf = default_buf;
        sz = sizeof(default_buf);
    }
    char *p = buf;
    const char* name = "nop";
    switch(inst->opcode)
    {
        case DVM_MOVE:
            __CI(move);
        case DVM_NOP:
            __CI(nop);
        case DVM_RETURN:
            __CI(return);
        case DVM_CONST:
            __CI(const);
        case DVM_MONITOR:
            __CI(monitor);
        case DVM_CHECK_CAST:
            __CI(check-cast);
        case DVM_INSTANCE:
            __CI(instance);
        case DVM_ARRAY:
            __CI(array);
        case DVM_THROW:
            __CI(throw);
        case DVM_GOTO:
            __CI(goto);
        case DVM_SWITCH:
            __CI(switch);
        case DVM_CMP:
            __CI(cmp);
        case DVM_IF:
            __CI(if);
        case DVM_INVOKE:
            __CI(invoke);
        case DVM_UNOP:
            __CI(unop);
        case DVM_BINOP:
            __CI(binop);
    }
    __PR("%s", name);
    int i;
    for(i = 0; i < inst->num_operands; i ++)
    {
        __PR(" ");
        __PO(i);
    }
    return buf;
}
#undef __PR
