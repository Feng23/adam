#ifndef __DALVIK_INSTRUCTION_H__
#define __DALVIK_INSTRUCTION_H__
/** @file dalvik_instruction.h
 *  @brief the defination of instruction 
 */
#include <stdint.h>

/** @brief data structure for an dalvik bytecode */
typedef struct _dalvik_instruction_t dalvik_instruction_t;

#include <sexp.h>
#include <dalvik/dalvik_tokens.h>
#include <vector.h>
#include <dalvik/dalvik_label.h>
#include <dalvik/dalvik_type.h>
#include <dalvik/dalvik_exception.h> 

#include <constants.h>

#include <string.h>
/** @brief Define all opcodes for Dalvik Virtual Machine */
enum {
	/** No Operation */
    DVM_NOP,
	/** move data from register to register */
    DVM_MOVE,
	/** return from a function */
    DVM_RETURN,
	/** load a const to a register */
    DVM_CONST,
	/** thread monitor */
    DVM_MONITOR,
	/** check if cast is valid */
    DVM_CHECK_CAST,
	/** instance related operations */
    DVM_INSTANCE,
	/** array related operations */
    DVM_ARRAY,
	/** thow an exception */
    DVM_THROW,
	/** goto a label */
    DVM_GOTO,
	/** conditional branch */
    DVM_SWITCH,
	/** compare two registers */
    DVM_CMP,
	/** if statement */
    DVM_IF,
	/** invoke a function */
    DVM_INVOKE,
	/** operations with single registers */
    DVM_UNOP,
	/** operations with two registers */
    DVM_BINOP,
	/** number of opcode */
    DVM_NUM_OF_OPCODE    /* How many items in the enumerate */
};

/**@brief Size specifier */
enum{
    DVM_OPERAND_SIZE_32, /*!<32 bit*/
    DVM_OPERAND_SIZE_64  /*!<64 bit*/
};
#define DVM_OPERAND_FLAG_WIDE       0x1u
/**@brief Type Specifier */
enum {
    DVM_OPERAND_TYPE_ANY,   /*!<any type*/
    DVM_OPERAND_TYPE_BOOLEAN, 
    DVM_OPERAND_TYPE_BYTE,
    DVM_OPERAND_TYPE_CHAR,
    DVM_OPERAND_TYPE_SHORT,
    DVM_OPERAND_TYPE_INT,
    DVM_OPERAND_TYPE_LONG,
    DVM_OPERAND_TYPE_DOUBLE,
    DVM_OPERAND_TYPE_FLOAT,
    DVM_OPERAND_TYPE_OBJECT,
    DVM_OPERAND_TYPE_STRING,
    DVM_OPERAND_TYPE_CLASS,     /*!<class path */
    DVM_OPERAND_TYPE_VOID,
    DVM_OPERAND_TYPE_LABEL,
    DVM_OPERAND_TYPE_LABELVECTOR,
    DVM_OPERAND_TYPE_SPARSE,
    DVM_OPERAND_TYPE_TYPEDESC,
    DVM_OPERAND_TYPE_TYPELIST,
    DVM_OPERAND_TYPE_FIELD,
    DVM_OPERAND_TYPE_EXCEPTION     /*!<If a operand is this type, that means the operand won't use the payload, this means data is in exception register*/
};
/** @brief convert a type to flag
 *  @param what the type we want to convert
 *  @return the flag
 */
#define DVM_OPERAND_FLAG_TYPE(what) ((what)<<1)
/** @brief const bit */
#define DVM_OPERAND_FLAG_CONST      0x40
/** @brief result bit */
#define DVM_OPERAND_FLAG_RESULT     0x80
/** @brief the header of the operand*/
typedef union {
    uint8_t    flags;  /*!<flag byte*/
	/** @brief the infomation of the operand */
    struct {
        uint8_t size:1;            /*!<Size of the operand, 32 bit or 64 bit */
        uint8_t type:5;            /*!<Type specified by the instruction. If it's unspecified, it should be DVM_OPERAND_TYPE_ANY */
        uint8_t is_const:1;        /*!<Wether or not the oprand a constant */
        uint8_t is_result:1;       /*!move-result instruction actually take 2 args, but one is a result */
    } info;
} dalvik_operand_header;

struct _dalvik_instruction_t;

/**@brief this structure is used for reprsenting 
 * a conditonal brach.
 * if the register euqals the `cond'
 * then the instruction makes the DVM
 * jump to labelid 
 */
typedef struct {
    int32_t     cond;	/*!<condition*/
    uint8_t     is_default:1;/*!<is default breanch?*/
    int32_t     labelid:31; /*!<the label id*/
} dalvik_sparse_switch_branch_t; 

/** @brief General operand type */
typedef struct {
    dalvik_operand_header header;   /*!<the header of this operand*/
    union{
        const char*        string;  /*!<for a string constant */
        const char*        methpath; /*!<for a method path */
        uint8_t            uint8;     
        uint16_t           uint16;
        uint32_t           uint32;
        uint64_t           uint64;
        int8_t             int8;
        int16_t            int16;
        int32_t            int32;
        int64_t            int64;
        double             real64;
        float              real32;
        int32_t            labelid;             /*!<label id in label pool */
        vector_t*          branches;            /*!<a group of branch */
        vector_t*          sparse;              /*!<a sparse-switch oprand */
        dalvik_type_t*     type;                /*!<this operand is a type, if the type code is DVM_OPERAND_TYPE_TYPEDESC */
        const dalvik_type_t *const*    typelist;/*!<if the type code is DVM_OPERAND_TYPE_TYPELIST, the pointer points an array of 
                                                 * points, which ends with a null pointer
                                                 */
        const char*        field;               /*!<The field we what to operate */
    } payload;
} dalvik_operand_t;

/** @brief the DVM instruction */
struct _dalvik_instruction_t{
    uint8_t            opcode:4;        /*!<Opcode of the instruction */
    uint8_t            num_operands:4;  /*!<How many operand ? */
    uint16_t           flags:8;         /*!<Additional flags for instruction, DVM_FLAG_OPTYPE_NAME */
    int                line;            /*!<Line number of this instruction */
    uint32_t next;                      /*!<The next instruction offset in the pool */
    dalvik_exception_handler_set_t* handler_set;   /*!<The handler set for exception */
    dalvik_operand_t   annotation_begin[0];        /*!<we reuse operand space for additional infomation,
                                                      the first address we can safely use is 
                                                      (void*)(annotation_begin + num_operands) */
    dalvik_operand_t   operands[16];        /*!<Operand array */
    char               annotation_end[0];   /*!<the limit address of this insturction */
};

/** @brief flags for monitor instruction */
enum {
    DVM_FLAG_MONITOR_ENT,       /*!<monitor-enter */
    DVM_FLAG_MONITOR_EXT        /*!<monitor-end */
};

/** @brief flags for switch instruction */
enum {
    DVM_FLAG_SWITCH_PACKED,   /*!<packed switch*/
    DVM_FLAG_SWITCH_SPARSE    /*!<sparse switch*/
};

/** @brief flags for instance instruction */
enum {
    DVM_FLAG_INSTANCE_OF,  /*!<instance-of instruction */
    DVM_FLAG_INSTANCE_NEW, /*!<new-instance instruction */
    DVM_FLAG_INSTANCE_GET, /*!<instance-get instruction */
    DVM_FLAG_INSTANCE_PUT, /*!<instance-put instruction */
    DVM_FLAG_INSTANCE_SGET,        /*!<Get a static member */
    DVM_FLAG_INSTANCE_SPUT         /*!<Put a static member */
};

/** @brief flags for array instruction */
enum {
    DVM_FLAG_ARRAY_LENGTH,  /*!<array-length */
    DVM_FLAG_ARRAY_NEW,     /*!<new-array*/
    DVM_FLAG_ARRAY_FILLED_NEW, /*!<new-filled-array*/
    DVM_FLAG_ARRAY_FILLED_NEW_RANGE, /*!<new-filled-array-range*/
    DVM_FLAG_ARRAY_GET, /*!<array-get*/
    DVM_FLAG_ARRAY_PUT /*!<array-put*/
};

/** @brief flags for invoke instruction */
enum {
    DVM_FLAG_INVOKE_VIRTUAL,
    DVM_FLAG_INVOKE_SUPER,
    DVM_FLAG_INVOKE_DIRECT,
    DVM_FLAG_INVOKE_STATIC,
    DVM_FLAG_INVOKE_INTERFACE,
    DVM_FLAG_INVOKE_RANGE = 0x80
};

/** @brief flags for if instruction */
enum {
    DVM_FLAG_IF_EQ,
    DVM_FLAG_IF_NE,
    DVM_FLAG_IF_GT,
    DVM_FLAG_IF_GE,
    DVM_FLAG_IF_LE,
    DVM_FLAG_IF_LT
};

/** @brief flags for unop instruction */
enum {
    DVM_FLAG_UOP_NEG,
    DVM_FLAG_UOP_NOT,
    DVM_FLAG_UOP_TO,
};

/** @brief flags for binary ops instruction */
enum {
    DVM_FLAG_BINOP_ADD,
    DVM_FLAG_BINOP_SUB,
    DVM_FLAG_BINOP_MUL,
    DVM_FLAG_BINOP_DIV,
    DVM_FLAG_BINOP_REM,
    DVM_FLAG_BINOP_AND,
    DVM_FLAG_BINOP_OR,
    DVM_FLAG_BINOP_XOR,
    DVM_FLAG_BINOP_SHR,
    DVM_FLAG_BINOP_SHL,
    DVM_FLAG_BINOP_USHR,
    DVM_FLAG_BINOP_RSUB
};

/** @brief the instruction allocation pool */
extern dalvik_instruction_t* dalvik_instruction_pool;
/** @brief Return a new empty dalvik instruction */
dalvik_instruction_t* dalvik_instruction_new( void );
/** @brief initialization */
int dalvik_instruction_init( void );
/** @brief finalization */
int dalvik_instruction_finalize( void );

/** 
 * @brief make a new dalvik instruction from a S-Expression
 * @param sexp The S-Expression to be convert
 * @param buf  Store the result
 * @param line the line number
 * @return <0 error, 0 success
 */ 
int dalvik_instruction_from_sexp(const sexpression_t* sexp, dalvik_instruction_t* buf, int line);

/**
 * @brief free the instruction 
 */
void dalvik_instruction_free(dalvik_instruction_t* buf);

/** @brief read the annotation from the instruction 
 * @param ins the instruction
 * @param buf buf of the annotation
 * @param count  the size of buf 
 * @return nothing
 */
static inline void dalvik_instruction_read_annotation(const dalvik_instruction_t* ins, void* buf, size_t count)
{
    memcpy(buf, ins->annotation_begin + ins->num_operands, count);
}
/**@brief get a instruction by instruction index */
static inline uint32_t dalvik_instruction_get_index(const dalvik_instruction_t* inst)
{
    extern dalvik_instruction_t* dalvik_instruction_pool;
    return inst - dalvik_instruction_pool;
}
/**@brief get instruction of instruction */
static inline const dalvik_instruction_t* dalvik_instruction_get(uint32_t offset)
{
    extern dalvik_instruction_t* dalvik_instruction_pool;
    return dalvik_instruction_pool + offset;
}
static inline void dalvik_instruction_set_next(uint32_t last, const dalvik_instruction_t* inst)
{
    extern dalvik_instruction_t* dalvik_instruction_pool;
	dalvik_instruction_pool[last].next = dalvik_instruction_get_index(inst);
}
/** @brief print the instruction to a string */
const char* dalvik_instruction_to_string(const dalvik_instruction_t* inst, char* buf, size_t sz);

/** @brief invalid instruction index */
#define DALVIK_INSTRUCTION_INVALID 0xfffffffful
#endif /* __DALVIK_INSTRCTION_H__ */
